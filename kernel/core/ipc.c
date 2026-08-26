#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/ipc.h>
#include <boring/process.h>
#include <boring/task.h>

#define IPC_HANDLE_SLOT_BITS 5U
#define IPC_HANDLE_SLOT_MASK ((1U << IPC_HANDLE_SLOT_BITS) - 1U)
#define IPC_HANDLE_GENERATION_MAX ((1U << (32U - IPC_HANDLE_SLOT_BITS)) - 1U)
#define IPC_SIDE_CLIENT 0U
#define IPC_SIDE_SERVER 1U
#define IPC_PROCESS_STATE_MAX KERNEL_PROCESS_MAX
#define IPC_INDEX_INVALID UINT16_MAX

struct ipc_handle_entry {
    uint32_t generation;
    uint16_t object_index;
    uint8_t type;
    uint8_t side;
    bool active;
};

struct ipc_process_state {
    struct process *owner;
    uint64_t owner_pid;
    struct ipc_handle_entry handles[BORING_IPC_HANDLES_PER_PROCESS];
};

struct ipc_message {
    size_t payload_length;
    uint8_t payload[BORING_IPC_INLINE_PAYLOAD_MAX];
    struct user_buffer_retained_ref attachment;
    bool active;
};

struct ipc_queue {
    struct ipc_message messages[BORING_IPC_MESSAGES_PER_DIRECTION];
    uint8_t head;
    uint8_t count;
};

struct ipc_connection {
    struct process *client_owner;
    struct process *server_owner;
    uint64_t client_pid;
    uint64_t server_pid;
    struct ipc_queue to_client;
    struct ipc_queue to_server;
    bool client_open;
    bool server_open;
    bool server_accepted;
    bool active;
};

struct ipc_service {
    struct process *owner;
    uint64_t owner_pid;
    char name[BORING_IPC_SERVICE_NAME_MAX + 1U];
    uint16_t pending[BORING_IPC_PENDING_PER_SERVICE];
    uint8_t pending_head;
    uint8_t pending_count;
    bool active;
};

static struct ipc_process_state process_states[IPC_PROCESS_STATE_MAX];
static struct ipc_connection connections[BORING_IPC_GLOBAL_CONNECTION_MAX];
static struct ipc_service services[BORING_IPC_GLOBAL_SERVICE_MAX];
static bool ipc_initialized;

static uint32_t next_generation(uint32_t generation) {
    if ((generation == 0U) || (generation >= IPC_HANDLE_GENERATION_MAX)) {
        return 1U;
    }
    return generation + 1U;
}

static uint32_t encode_handle(size_t slot, uint32_t generation) {
    return (generation << IPC_HANDLE_SLOT_BITS) | (uint32_t)slot;
}

static bool decode_handle(uint32_t handle, size_t *slot_out,
                          uint32_t *generation_out) {
    const uint32_t generation = handle >> IPC_HANDLE_SLOT_BITS;
    const size_t slot = (size_t)(handle & IPC_HANDLE_SLOT_MASK);

    if ((handle == BORING_IPC_HANDLE_INVALID) || (generation == 0U) ||
        (slot >= (size_t)BORING_IPC_HANDLES_PER_PROCESS) ||
        (slot_out == NULL) || (generation_out == NULL)) {
        return false;
    }
    *slot_out = slot;
    *generation_out = generation;
    return true;
}

static void clear_message(struct ipc_message *message) {
    size_t index;

    if (message == NULL) {
        return;
    }
    message->payload_length = 0U;
    for (index = 0U; index < sizeof(message->payload); ++index) {
        message->payload[index] = 0U;
    }
    user_buffer_retained_ref_clear(&message->attachment);
    message->active = false;
}

static void clear_queue(struct ipc_queue *queue) {
    size_t index;

    if (queue == NULL) {
        return;
    }
    for (index = 0U; index < (size_t)BORING_IPC_MESSAGES_PER_DIRECTION;
         ++index) {
        clear_message(&queue->messages[index]);
    }
    queue->head = 0U;
    queue->count = 0U;
}

static void clear_connection(struct ipc_connection *connection) {
    if (connection == NULL) {
        return;
    }
    connection->client_owner = NULL;
    connection->server_owner = NULL;
    connection->client_pid = 0ULL;
    connection->server_pid = 0ULL;
    clear_queue(&connection->to_client);
    clear_queue(&connection->to_server);
    connection->client_open = false;
    connection->server_open = false;
    connection->server_accepted = false;
    connection->active = false;
}

static void clear_service(struct ipc_service *service) {
    size_t index;

    if (service == NULL) {
        return;
    }
    service->owner = NULL;
    service->owner_pid = 0ULL;
    for (index = 0U; index < sizeof(service->name); ++index) {
        service->name[index] = '\0';
    }
    for (index = 0U; index < (size_t)BORING_IPC_PENDING_PER_SERVICE;
         ++index) {
        service->pending[index] = IPC_INDEX_INVALID;
    }
    service->pending_head = 0U;
    service->pending_count = 0U;
    service->active = false;
}

static bool process_identity_valid(const struct process *process) {
    return (process != NULL) && process_is_alive(process) &&
           (process->pid != KERNEL_BOOTSTRAP_PID);
}

static struct ipc_process_state *state_for_process(struct process *process,
                                                   bool create) {
    struct ipc_process_state *free_state = NULL;
    size_t index;

    if (!process_identity_valid(process)) {
        return NULL;
    }
    for (index = 0U; index < (size_t)IPC_PROCESS_STATE_MAX; ++index) {
        struct ipc_process_state *state = &process_states[index];
        if (state->owner == process) {
            if (state->owner_pid != process->pid) {
                size_t slot;
                for (slot = 0U;
                     slot < (size_t)BORING_IPC_HANDLES_PER_PROCESS; ++slot) {
                    if (state->handles[slot].active) {
                        return NULL;
                    }
                }
                state->owner_pid = process->pid;
            }
            return state;
        }
        if ((free_state == NULL) && (state->owner == NULL)) {
            free_state = state;
        }
    }
    if (!create || (free_state == NULL)) {
        return NULL;
    }
    free_state->owner = process;
    free_state->owner_pid = process->pid;
    return free_state;
}

static struct ipc_handle_entry *find_free_handle(
    struct ipc_process_state *state, size_t *slot_out) {
    size_t slot;

    if ((state == NULL) || (slot_out == NULL)) {
        return NULL;
    }
    for (slot = 0U; slot < (size_t)BORING_IPC_HANDLES_PER_PROCESS; ++slot) {
        if (!state->handles[slot].active) {
            *slot_out = slot;
            return &state->handles[slot];
        }
    }
    return NULL;
}

static bool install_handle(struct ipc_process_state *state,
                           uint8_t type,
                           uint16_t object_index,
                           uint8_t side,
                           uint32_t *handle_out) {
    struct ipc_handle_entry *entry;
    size_t slot;

    if ((handle_out == NULL) ||
        ((type != (uint8_t)BORING_IPC_HANDLE_LISTENER) &&
         (type != (uint8_t)BORING_IPC_HANDLE_ENDPOINT))) {
        return false;
    }
    entry = find_free_handle(state, &slot);
    if (entry == NULL) {
        return false;
    }
    if (entry->generation == 0U) {
        entry->generation = 1U;
    }
    entry->type = type;
    entry->object_index = object_index;
    entry->side = side;
    entry->active = true;
    *handle_out = encode_handle(slot, entry->generation);
    return true;
}

static struct ipc_handle_entry *lookup_handle(struct process *process,
                                              uint32_t handle,
                                              uint8_t expected_type) {
    struct ipc_process_state *state;
    struct ipc_handle_entry *entry;
    size_t slot;
    uint32_t generation;

    if (!decode_handle(handle, &slot, &generation)) {
        return NULL;
    }
    state = state_for_process(process, false);
    if (state == NULL) {
        return NULL;
    }
    entry = &state->handles[slot];
    if (!entry->active || (entry->generation != generation) ||
        (entry->type != expected_type)) {
        return NULL;
    }
    return entry;
}

static void invalidate_handle_entry(struct ipc_handle_entry *entry) {
    if (entry == NULL) {
        return;
    }
    entry->active = false;
    entry->type = (uint8_t)BORING_IPC_HANDLE_NONE;
    entry->object_index = IPC_INDEX_INVALID;
    entry->side = 0U;
    entry->generation = next_generation(entry->generation);
}

static bool same_name(const struct ipc_service *service,
                      const char *name, size_t length) {
    size_t index;

    if ((service == NULL) || !service->active || (name == NULL)) {
        return false;
    }
    for (index = 0U; index < length; ++index) {
        if (service->name[index] != name[index]) {
            return false;
        }
    }
    return service->name[length] == '\0';
}

static struct ipc_service *find_service(const char *name, size_t length,
                                        size_t *index_out) {
    size_t index;

    for (index = 0U; index < (size_t)BORING_IPC_GLOBAL_SERVICE_MAX; ++index) {
        if (same_name(&services[index], name, length)) {
            if (index_out != NULL) {
                *index_out = index;
            }
            return &services[index];
        }
    }
    return NULL;
}

static struct ipc_service *find_free_service(size_t *index_out) {
    size_t index;

    for (index = 0U; index < (size_t)BORING_IPC_GLOBAL_SERVICE_MAX; ++index) {
        if (!services[index].active) {
            if (index_out != NULL) {
                *index_out = index;
            }
            return &services[index];
        }
    }
    return NULL;
}

static struct ipc_connection *find_free_connection(size_t *index_out) {
    size_t index;

    for (index = 0U; index < (size_t)BORING_IPC_GLOBAL_CONNECTION_MAX;
         ++index) {
        if (!connections[index].active) {
            if (index_out != NULL) {
                *index_out = index;
            }
            return &connections[index];
        }
    }
    return NULL;
}

static bool service_owner_alive(const struct ipc_service *service) {
    return (service != NULL) && service->active &&
           (service->owner != NULL) &&
           process_is_alive(service->owner) &&
           (service->owner->pid == service->owner_pid);
}

static bool connection_side_open(const struct ipc_connection *connection,
                                 uint8_t side) {
    return (side == IPC_SIDE_CLIENT) ? connection->client_open :
           connection->server_open;
}

static bool connection_peer_open(const struct ipc_connection *connection,
                                 uint8_t side) {
    return (side == IPC_SIDE_CLIENT) ? connection->server_open :
           connection->client_open;
}

static struct process *connection_peer_owner(
    const struct ipc_connection *connection, uint8_t side) {
    return (side == IPC_SIDE_CLIENT) ? connection->server_owner :
           connection->client_owner;
}

static struct ipc_queue *incoming_queue(struct ipc_connection *connection,
                                        uint8_t side) {
    return (side == IPC_SIDE_CLIENT) ? &connection->to_client :
           &connection->to_server;
}

static struct ipc_queue *peer_incoming_queue(struct ipc_connection *connection,
                                             uint8_t side) {
    return (side == IPC_SIDE_CLIENT) ? &connection->to_server :
           &connection->to_client;
}

static void release_message(struct ipc_message *message) {
    if ((message != NULL) && message->active &&
        user_buffer_retained_ref_active(&message->attachment)) {
        (void)user_buffer_release_retained(&message->attachment);
    }
    clear_message(message);
}

static void release_queue(struct ipc_queue *queue) {
    while ((queue != NULL) && (queue->count != 0U)) {
        struct ipc_message *message = &queue->messages[queue->head];
        release_message(message);
        queue->head = (uint8_t)((queue->head + 1U) %
                                BORING_IPC_MESSAGES_PER_DIRECTION);
        --queue->count;
    }
    if (queue != NULL) {
        queue->head = 0U;
    }
}

static bool connection_maybe_destroy(struct ipc_connection *connection) {
    if ((connection == NULL) || !connection->active ||
        connection->client_open || connection->server_open ||
        (connection->to_client.count != 0U) ||
        (connection->to_server.count != 0U)) {
        return false;
    }
    clear_connection(connection);
    return true;
}

static void close_connection_side(struct ipc_connection *connection,
                                  uint8_t side) {
    struct ipc_queue *queue_for_closing_side;
    struct process *peer;

    if ((connection == NULL) || !connection->active ||
        !connection_side_open(connection, side)) {
        return;
    }
    queue_for_closing_side = incoming_queue(connection, side);
    release_queue(queue_for_closing_side);
    if (side == IPC_SIDE_CLIENT) {
        connection->client_open = false;
    } else {
        connection->server_open = false;
    }
    peer = connection_peer_owner(connection, side);
    if (peer != NULL) {
        (void)task_wake_process(peer);
    }
    (void)connection_maybe_destroy(connection);
}

static bool pending_push(struct ipc_service *service, uint16_t connection_index) {
    size_t tail;

    if ((service == NULL) ||
        (service->pending_count >= BORING_IPC_PENDING_PER_SERVICE)) {
        return false;
    }
    tail = ((size_t)service->pending_head + (size_t)service->pending_count) %
           (size_t)BORING_IPC_PENDING_PER_SERVICE;
    service->pending[tail] = connection_index;
    ++service->pending_count;
    return true;
}

static bool pending_peek(struct ipc_service *service, uint16_t *index_out) {
    if ((service == NULL) || (index_out == NULL) ||
        (service->pending_count == 0U)) {
        return false;
    }
    *index_out = service->pending[service->pending_head];
    return true;
}

static bool pending_pop(struct ipc_service *service, uint16_t *index_out) {
    if (!pending_peek(service, index_out)) {
        return false;
    }
    service->pending[service->pending_head] = IPC_INDEX_INVALID;
    service->pending_head = (uint8_t)((service->pending_head + 1U) %
                                      BORING_IPC_PENDING_PER_SERVICE);
    --service->pending_count;
    return true;
}

static void unregister_service(struct ipc_service *service) {
    uint16_t connection_index;

    if ((service == NULL) || !service->active) {
        return;
    }
    while (pending_pop(service, &connection_index)) {
        if (connection_index < BORING_IPC_GLOBAL_CONNECTION_MAX) {
            struct ipc_connection *connection = &connections[connection_index];
            if (connection->active && !connection->server_accepted) {
                close_connection_side(connection, IPC_SIDE_SERVER);
            }
        }
    }
    clear_service(service);
}

bool boring_ipc_system_init(void) {
    size_t index;
    size_t slot;

    if (ipc_initialized) {
        return true;
    }
    for (index = 0U; index < (size_t)IPC_PROCESS_STATE_MAX; ++index) {
        process_states[index].owner = NULL;
        process_states[index].owner_pid = 0ULL;
        for (slot = 0U; slot < (size_t)BORING_IPC_HANDLES_PER_PROCESS;
             ++slot) {
            process_states[index].handles[slot].generation = 1U;
            process_states[index].handles[slot].object_index = IPC_INDEX_INVALID;
            process_states[index].handles[slot].type =
                (uint8_t)BORING_IPC_HANDLE_NONE;
            process_states[index].handles[slot].side = 0U;
            process_states[index].handles[slot].active = false;
        }
    }
    for (index = 0U; index < (size_t)BORING_IPC_GLOBAL_CONNECTION_MAX;
         ++index) {
        clear_connection(&connections[index]);
    }
    for (index = 0U; index < (size_t)BORING_IPC_GLOBAL_SERVICE_MAX; ++index) {
        clear_service(&services[index]);
    }
    ipc_initialized = true;
    return true;
}

bool boring_ipc_service_name_valid(const char *name, size_t length) {
    size_t index;

    if ((name == NULL) || (length == 0U) ||
        (length > (size_t)BORING_IPC_SERVICE_NAME_MAX)) {
        return false;
    }
    for (index = 0U; index < length; ++index) {
        const uint8_t character = (uint8_t)name[index];
        const bool alpha = (character >= (uint8_t)'a') &&
                           (character <= (uint8_t)'z');
        const bool digit = (character >= (uint8_t)'0') &&
                           (character <= (uint8_t)'9');
        const bool punctuation = (character == (uint8_t)'.') ||
                                 (character == (uint8_t)'_') ||
                                 (character == (uint8_t)'-');
        if ((!alpha && !digit && !punctuation) || (character == 0U)) {
            return false;
        }
    }
    return true;
}

enum boring_ipc_result boring_ipc_service_register(
    struct process *process, const char *name, size_t length,
    uint32_t *handle_out) {
    struct ipc_process_state *state;
    struct ipc_service *service;
    size_t service_index;
    size_t name_index;
    uint32_t handle;

    if (!ipc_initialized || !process_identity_valid(process) ||
        !boring_ipc_service_name_valid(name, length) ||
        (handle_out == NULL)) {
        return BORING_IPC_RESULT_INVALID;
    }
    if (find_service(name, length, NULL) != NULL) {
        return BORING_IPC_RESULT_EXISTS;
    }
    state = state_for_process(process, true);
    service = find_free_service(&service_index);
    if ((state == NULL) || (service == NULL) ||
        (find_free_handle(state, &name_index) == NULL)) {
        return BORING_IPC_RESULT_NO_SPACE;
    }
    clear_service(service);
    service->owner = process;
    service->owner_pid = process->pid;
    for (name_index = 0U; name_index < length; ++name_index) {
        service->name[name_index] = name[name_index];
    }
    service->name[length] = '\0';
    service->active = true;
    if (!install_handle(state, (uint8_t)BORING_IPC_HANDLE_LISTENER,
                        (uint16_t)service_index, 0U, &handle)) {
        clear_service(service);
        return BORING_IPC_RESULT_NO_SPACE;
    }
    *handle_out = handle;
    return BORING_IPC_RESULT_OK;
}

enum boring_ipc_result boring_ipc_service_connect(
    struct process *process, const char *name, size_t length,
    uint32_t *handle_out) {
    struct ipc_process_state *state;
    struct ipc_service *service;
    struct ipc_connection *connection;
    size_t connection_index;
    size_t ignored_slot;
    uint32_t client_handle;

    if (!ipc_initialized || !process_identity_valid(process) ||
        !boring_ipc_service_name_valid(name, length) ||
        (handle_out == NULL)) {
        return BORING_IPC_RESULT_INVALID;
    }
    service = find_service(name, length, NULL);
    if ((service == NULL) || !service_owner_alive(service)) {
        return BORING_IPC_RESULT_NOT_FOUND;
    }
    if (service->pending_count >= BORING_IPC_PENDING_PER_SERVICE) {
        return BORING_IPC_RESULT_NO_SPACE;
    }
    state = state_for_process(process, true);
    connection = find_free_connection(&connection_index);
    if ((state == NULL) || (connection == NULL) ||
        (find_free_handle(state, &ignored_slot) == NULL)) {
        return BORING_IPC_RESULT_NO_SPACE;
    }

    clear_connection(connection);
    connection->client_owner = process;
    connection->server_owner = service->owner;
    connection->client_pid = process->pid;
    connection->server_pid = service->owner_pid;
    connection->client_open = true;
    connection->server_open = true;
    connection->active = true;

    if (!install_handle(state, (uint8_t)BORING_IPC_HANDLE_ENDPOINT,
                        (uint16_t)connection_index, IPC_SIDE_CLIENT,
                        &client_handle)) {
        clear_connection(connection);
        return BORING_IPC_RESULT_NO_SPACE;
    }
    if (!pending_push(service, (uint16_t)connection_index)) {
        struct ipc_handle_entry *entry = lookup_handle(
            process, client_handle, (uint8_t)BORING_IPC_HANDLE_ENDPOINT);
        invalidate_handle_entry(entry);
        clear_connection(connection);
        return BORING_IPC_RESULT_NO_SPACE;
    }
    *handle_out = client_handle;
    (void)task_wake_process(service->owner);
    return BORING_IPC_RESULT_OK;
}

enum boring_ipc_result boring_ipc_service_accept(
    struct process *process, uint32_t listener_handle,
    uint32_t *endpoint_handle_out) {
    struct ipc_handle_entry *listener_entry;
    struct ipc_process_state *state;
    struct ipc_service *service;
    struct ipc_connection *connection;
    uint16_t connection_index;
    size_t ignored_slot;
    uint32_t endpoint_handle;

    if (!ipc_initialized || !process_identity_valid(process) ||
        (endpoint_handle_out == NULL)) {
        return BORING_IPC_RESULT_INVALID;
    }
    listener_entry = lookup_handle(process, listener_handle,
                                   (uint8_t)BORING_IPC_HANDLE_LISTENER);
    if ((listener_entry == NULL) ||
        (listener_entry->object_index >= BORING_IPC_GLOBAL_SERVICE_MAX)) {
        return BORING_IPC_RESULT_INVALID;
    }
    service = &services[listener_entry->object_index];
    if (!service_owner_alive(service) || (service->owner != process) ||
        (service->owner_pid != process->pid)) {
        return BORING_IPC_RESULT_INVALID;
    }
    if (!pending_peek(service, &connection_index)) {
        return BORING_IPC_RESULT_WOULD_BLOCK;
    }
    if (connection_index >= BORING_IPC_GLOBAL_CONNECTION_MAX) {
        return BORING_IPC_RESULT_INTERNAL;
    }
    connection = &connections[connection_index];
    if (!connection->active || connection->server_accepted ||
        (connection->server_owner != process) ||
        (connection->server_pid != process->pid)) {
        return BORING_IPC_RESULT_INTERNAL;
    }
    state = state_for_process(process, true);
    if ((state == NULL) || (find_free_handle(state, &ignored_slot) == NULL)) {
        return BORING_IPC_RESULT_NO_SPACE;
    }
    if (!install_handle(state, (uint8_t)BORING_IPC_HANDLE_ENDPOINT,
                        connection_index, IPC_SIDE_SERVER,
                        &endpoint_handle)) {
        return BORING_IPC_RESULT_NO_SPACE;
    }
    {
        uint16_t popped;
        if (!pending_pop(service, &popped) || (popped != connection_index)) {
            struct ipc_handle_entry *entry = lookup_handle(
                process, endpoint_handle,
                (uint8_t)BORING_IPC_HANDLE_ENDPOINT);
            invalidate_handle_entry(entry);
            return BORING_IPC_RESULT_INTERNAL;
        }
    }
    connection->server_accepted = true;
    *endpoint_handle_out = endpoint_handle;
    return BORING_IPC_RESULT_OK;
}

enum boring_ipc_result boring_ipc_send(
    struct process *process, uint32_t endpoint_handle,
    const uint8_t *payload, size_t payload_length, uint32_t buffer_handle) {
    struct ipc_handle_entry *endpoint_entry;
    struct ipc_connection *connection;
    struct ipc_queue *queue;
    struct ipc_message *message;
    struct user_buffer_retained_ref retained;
    size_t tail;
    size_t index;

    user_buffer_retained_ref_clear(&retained);
    if (!ipc_initialized || !process_identity_valid(process) ||
        (payload_length > (size_t)BORING_IPC_INLINE_PAYLOAD_MAX) ||
        ((payload_length != 0U) && (payload == NULL))) {
        return BORING_IPC_RESULT_INVALID;
    }
    endpoint_entry = lookup_handle(process, endpoint_handle,
                                   (uint8_t)BORING_IPC_HANDLE_ENDPOINT);
    if ((endpoint_entry == NULL) ||
        (endpoint_entry->object_index >= BORING_IPC_GLOBAL_CONNECTION_MAX) ||
        (endpoint_entry->side > IPC_SIDE_SERVER)) {
        return BORING_IPC_RESULT_INVALID;
    }
    connection = &connections[endpoint_entry->object_index];
    if (!connection->active ||
        !connection_side_open(connection, endpoint_entry->side)) {
        return BORING_IPC_RESULT_INVALID;
    }
    if (!connection_peer_open(connection, endpoint_entry->side)) {
        return BORING_IPC_RESULT_PEER_CLOSED;
    }
    queue = peer_incoming_queue(connection, endpoint_entry->side);
    if (queue->count >= BORING_IPC_MESSAGES_PER_DIRECTION) {
        return BORING_IPC_RESULT_NO_SPACE;
    }
    if (buffer_handle != BORING_BUFFER_HANDLE_INVALID) {
        const enum user_memory_result memory_result =
            user_buffer_retain(process, buffer_handle, &retained);
        if (memory_result != USER_MEMORY_RESULT_OK) {
            return (memory_result == USER_MEMORY_RESULT_NO_SPACE) ?
                BORING_IPC_RESULT_NO_SPACE : BORING_IPC_RESULT_INVALID;
        }
    }

    tail = ((size_t)queue->head + (size_t)queue->count) %
           (size_t)BORING_IPC_MESSAGES_PER_DIRECTION;
    message = &queue->messages[tail];
    if (message->active) {
        if (user_buffer_retained_ref_active(&retained)) {
            (void)user_buffer_release_retained(&retained);
        }
        return BORING_IPC_RESULT_INTERNAL;
    }
    clear_message(message);
    message->payload_length = payload_length;
    for (index = 0U; index < payload_length; ++index) {
        message->payload[index] = payload[index];
    }
    message->attachment = retained;
    message->active = true;
    ++queue->count;
    {
        struct process *peer = connection_peer_owner(connection,
                                                     endpoint_entry->side);
        if (peer != NULL) {
            (void)task_wake_process(peer);
        }
    }
    return BORING_IPC_RESULT_OK;
}

enum boring_ipc_result boring_ipc_receive(
    struct process *process, uint32_t endpoint_handle, uint8_t *payload,
    size_t payload_capacity, struct boring_ipc_receive_kernel_result *result_out) {
    struct ipc_handle_entry *endpoint_entry;
    struct ipc_connection *connection;
    struct ipc_queue *queue;
    struct ipc_message *message;
    struct user_buffer_install_ticket ticket;
    uint32_t received_handle = BORING_BUFFER_HANDLE_INVALID;
    size_t index;
    bool has_attachment;

    user_buffer_install_ticket_clear(&ticket);
    if (!ipc_initialized || !process_identity_valid(process) ||
        (result_out == NULL) ||
        ((payload_capacity != 0U) && (payload == NULL))) {
        return BORING_IPC_RESULT_INVALID;
    }
    endpoint_entry = lookup_handle(process, endpoint_handle,
                                   (uint8_t)BORING_IPC_HANDLE_ENDPOINT);
    if ((endpoint_entry == NULL) ||
        (endpoint_entry->object_index >= BORING_IPC_GLOBAL_CONNECTION_MAX) ||
        (endpoint_entry->side > IPC_SIDE_SERVER)) {
        return BORING_IPC_RESULT_INVALID;
    }
    connection = &connections[endpoint_entry->object_index];
    if (!connection->active ||
        !connection_side_open(connection, endpoint_entry->side)) {
        return BORING_IPC_RESULT_INVALID;
    }
    queue = incoming_queue(connection, endpoint_entry->side);
    if (queue->count == 0U) {
        return connection_peer_open(connection, endpoint_entry->side) ?
            BORING_IPC_RESULT_WOULD_BLOCK : BORING_IPC_RESULT_PEER_CLOSED;
    }
    message = &queue->messages[queue->head];
    if (!message->active) {
        return BORING_IPC_RESULT_INTERNAL;
    }
    if (payload_capacity < message->payload_length) {
        result_out->payload_length = message->payload_length;
        result_out->buffer_handle = BORING_BUFFER_HANDLE_INVALID;
        return BORING_IPC_RESULT_NO_SPACE;
    }
    has_attachment = user_buffer_retained_ref_active(&message->attachment);
    if (has_attachment) {
        const enum user_memory_result memory_result =
            user_buffer_prepare_install(process, &message->attachment,
                                        &ticket, &received_handle);
        if (memory_result != USER_MEMORY_RESULT_OK) {
            return (memory_result == USER_MEMORY_RESULT_NO_SPACE) ?
                BORING_IPC_RESULT_NO_SPACE : BORING_IPC_RESULT_INTERNAL;
        }
    }

    for (index = 0U; index < message->payload_length; ++index) {
        payload[index] = message->payload[index];
    }
    result_out->payload_length = message->payload_length;
    result_out->buffer_handle = received_handle;

    if (has_attachment &&
        (user_buffer_commit_install(process, &message->attachment, &ticket) !=
         USER_MEMORY_RESULT_OK)) {
        return BORING_IPC_RESULT_INTERNAL;
    }
    clear_message(message);
    queue->head = (uint8_t)((queue->head + 1U) %
                            BORING_IPC_MESSAGES_PER_DIRECTION);
    --queue->count;
    (void)connection_maybe_destroy(connection);
    return BORING_IPC_RESULT_OK;
}

enum boring_ipc_result boring_ipc_close(struct process *process,
                                        uint32_t handle) {
    struct ipc_process_state *state;
    struct ipc_handle_entry *entry;
    size_t slot;
    uint32_t generation;

    if (!ipc_initialized || !process_identity_valid(process) ||
        !decode_handle(handle, &slot, &generation)) {
        return BORING_IPC_RESULT_INVALID;
    }
    state = state_for_process(process, false);
    if (state == NULL) {
        return BORING_IPC_RESULT_INVALID;
    }
    entry = &state->handles[slot];
    if (!entry->active || (entry->generation != generation)) {
        return BORING_IPC_RESULT_INVALID;
    }
    if (entry->type == (uint8_t)BORING_IPC_HANDLE_LISTENER) {
        if (entry->object_index >= BORING_IPC_GLOBAL_SERVICE_MAX) {
            return BORING_IPC_RESULT_INTERNAL;
        }
        unregister_service(&services[entry->object_index]);
    } else if (entry->type == (uint8_t)BORING_IPC_HANDLE_ENDPOINT) {
        if ((entry->object_index >= BORING_IPC_GLOBAL_CONNECTION_MAX) ||
            (entry->side > IPC_SIDE_SERVER)) {
            return BORING_IPC_RESULT_INTERNAL;
        }
        close_connection_side(&connections[entry->object_index], entry->side);
    } else {
        return BORING_IPC_RESULT_INVALID;
    }
    invalidate_handle_entry(entry);
    return BORING_IPC_RESULT_OK;
}

void boring_ipc_process_cleanup(struct process *process) {
    struct ipc_process_state *state;
    size_t slot;

    if (!ipc_initialized || (process == NULL)) {
        return;
    }
    state = state_for_process(process, false);
    if (state == NULL) {
        return;
    }
    for (slot = 0U; slot < (size_t)BORING_IPC_HANDLES_PER_PROCESS; ++slot) {
        struct ipc_handle_entry *entry = &state->handles[slot];
        if (!entry->active) {
            continue;
        }
        if (entry->type == (uint8_t)BORING_IPC_HANDLE_LISTENER) {
            if (entry->object_index < BORING_IPC_GLOBAL_SERVICE_MAX) {
                unregister_service(&services[entry->object_index]);
            }
        } else if ((entry->type == (uint8_t)BORING_IPC_HANDLE_ENDPOINT) &&
                   (entry->object_index < BORING_IPC_GLOBAL_CONNECTION_MAX) &&
                   (entry->side <= IPC_SIDE_SERVER)) {
            close_connection_side(&connections[entry->object_index],
                                  entry->side);
        }
        invalidate_handle_entry(entry);
    }
}

bool boring_ipc_get_stats(struct boring_ipc_stats *stats) {
    size_t index;

    if (!ipc_initialized || (stats == NULL)) {
        return false;
    }
    stats->live_services = 0U;
    stats->live_connections = 0U;
    stats->queued_messages = 0U;
    stats->retained_buffer_attachments = 0U;
    for (index = 0U; index < (size_t)BORING_IPC_GLOBAL_SERVICE_MAX; ++index) {
        if (services[index].active) {
            ++stats->live_services;
        }
    }
    for (index = 0U; index < (size_t)BORING_IPC_GLOBAL_CONNECTION_MAX;
         ++index) {
        size_t queue_index;
        struct ipc_connection *connection = &connections[index];
        if (!connection->active) {
            continue;
        }
        ++stats->live_connections;
        stats->queued_messages += (uint32_t)connection->to_client.count;
        stats->queued_messages += (uint32_t)connection->to_server.count;
        for (queue_index = 0U;
             queue_index < (size_t)BORING_IPC_MESSAGES_PER_DIRECTION;
             ++queue_index) {
            if (connection->to_client.messages[queue_index].active &&
                user_buffer_retained_ref_active(
                    &connection->to_client.messages[queue_index].attachment)) {
                ++stats->retained_buffer_attachments;
            }
            if (connection->to_server.messages[queue_index].active &&
                user_buffer_retained_ref_active(
                    &connection->to_server.messages[queue_index].attachment)) {
                ++stats->retained_buffer_attachments;
            }
        }
    }
    return true;
}

bool boring_ipc_host_reset(void) {
    struct boring_ipc_stats stats;
    size_t index;

    if (!boring_ipc_get_stats(&stats) || (stats.live_services != 0U) ||
        (stats.live_connections != 0U) || (stats.queued_messages != 0U) ||
        (stats.retained_buffer_attachments != 0U)) {
        return false;
    }
    for (index = 0U; index < (size_t)IPC_PROCESS_STATE_MAX; ++index) {
        size_t slot;
        process_states[index].owner = NULL;
        process_states[index].owner_pid = 0ULL;
        for (slot = 0U; slot < (size_t)BORING_IPC_HANDLES_PER_PROCESS;
             ++slot) {
            if (process_states[index].handles[slot].active) {
                return false;
            }
        }
    }
    return true;
}
