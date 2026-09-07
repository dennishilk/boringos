#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/pty.h>
#include <boring/task.h>

struct pty_ring {
    uint8_t bytes[KERNEL_PTY_RING_CAPACITY];
    size_t read_index;
    size_t write_index;
    size_t count;
};

struct pty_pair {
    struct pty_ring master_to_slave;
    struct pty_ring slave_to_master;
    uint32_t generation;
    uint32_t refs[2];
    uint64_t read_waiter_pid[2];
    bool used;
};

static struct pty_pair pty_pairs[KERNEL_PTY_MAX];
static bool pty_initialized;

static void ring_clear(struct pty_ring *ring) {
    size_t index;
    if (ring == NULL) {
        return;
    }
    for (index = 0U; index < (size_t)KERNEL_PTY_RING_CAPACITY; ++index) {
        ring->bytes[index] = 0U;
    }
    ring->read_index = 0U;
    ring->write_index = 0U;
    ring->count = 0U;
}

static void pair_reset(struct pty_pair *pair, bool preserve_generation) {
    uint32_t generation;
    if (pair == NULL) {
        return;
    }
    generation = pair->generation;
    ring_clear(&pair->master_to_slave);
    ring_clear(&pair->slave_to_master);
    pair->refs[KERNEL_PTY_ENDPOINT_MASTER] = 0U;
    pair->refs[KERNEL_PTY_ENDPOINT_SLAVE] = 0U;
    pair->read_waiter_pid[KERNEL_PTY_ENDPOINT_MASTER] = 0ULL;
    pair->read_waiter_pid[KERNEL_PTY_ENDPOINT_SLAVE] = 0ULL;
    pair->used = false;
    pair->generation = preserve_generation ? generation : 0U;
}

static struct pty_pair *pair_from_handle(struct pty_handle handle) {
    struct pty_pair *pair;
    if ((handle.slot >= KERNEL_PTY_MAX) ||
        (handle.endpoint > KERNEL_PTY_ENDPOINT_SLAVE) ||
        (handle.generation == 0U)) {
        return NULL;
    }
    pair = &pty_pairs[handle.slot];
    if (!pair->used || (pair->generation != handle.generation) ||
        (pair->refs[handle.endpoint] == 0U)) {
        return NULL;
    }
    return pair;
}

static struct pty_ring *incoming_ring(struct pty_pair *pair,
                                      uint16_t endpoint) {
    return (endpoint == KERNEL_PTY_ENDPOINT_MASTER) ?
        &pair->slave_to_master : &pair->master_to_slave;
}

static struct pty_ring *outgoing_ring(struct pty_pair *pair,
                                      uint16_t endpoint) {
    return (endpoint == KERNEL_PTY_ENDPOINT_MASTER) ?
        &pair->master_to_slave : &pair->slave_to_master;
}

static uint16_t peer_endpoint(uint16_t endpoint) {
    return (endpoint == KERNEL_PTY_ENDPOINT_MASTER) ?
        KERNEL_PTY_ENDPOINT_SLAVE : KERNEL_PTY_ENDPOINT_MASTER;
}

static void wake_endpoint(struct pty_pair *pair, uint16_t endpoint) {
    const uint64_t pid = pair->read_waiter_pid[endpoint];
    if (pid != 0ULL) {
        pair->read_waiter_pid[endpoint] = 0ULL;
        (void)task_wake_pid(pid);
    }
}

bool pty_init(void) {
    size_t index;
    if (pty_initialized) {
        return false;
    }
    for (index = 0U; index < (size_t)KERNEL_PTY_MAX; ++index) {
        pair_reset(&pty_pairs[index], false);
    }
    pty_initialized = true;
    return true;
}

bool pty_get_stats(struct pty_stats *stats) {
    size_t index;
    if (!pty_initialized || (stats == NULL)) { return false; }
    *stats = (struct pty_stats){0};
    for (index = 0U; index < (size_t)KERNEL_PTY_MAX; ++index) {
        const struct pty_pair *const pair = &pty_pairs[index];
        if (pair->used) { ++stats->active_pairs; }
        stats->references += (uint64_t)pair->refs[0] + (uint64_t)pair->refs[1];
        stats->read_waiters += (pair->read_waiter_pid[0] != 0ULL) ? 1U : 0U;
        stats->read_waiters += (pair->read_waiter_pid[1] != 0ULL) ? 1U : 0U;
        stats->queued_bytes += pair->master_to_slave.count + pair->slave_to_master.count;
    }
    return true;
}

enum pty_result pty_create_pair(struct pty_handle *master_out,
                                struct pty_handle *slave_out) {
    size_t index;
    struct pty_pair *pair = NULL;
    if (!pty_initialized || (master_out == NULL) || (slave_out == NULL)) {
        return PTY_RESULT_INVALID;
    }
    for (index = 0U; index < (size_t)KERNEL_PTY_MAX; ++index) {
        if (!pty_pairs[index].used) {
            pair = &pty_pairs[index];
            break;
        }
    }
    if (pair == NULL) {
        return PTY_RESULT_NO_SPACE;
    }
    pair_reset(pair, true);
    ++pair->generation;
    if (pair->generation == 0U) {
        ++pair->generation;
    }
    pair->refs[KERNEL_PTY_ENDPOINT_MASTER] = 1U;
    pair->refs[KERNEL_PTY_ENDPOINT_SLAVE] = 1U;
    pair->used = true;
    master_out->slot = (uint16_t)index;
    master_out->endpoint = KERNEL_PTY_ENDPOINT_MASTER;
    master_out->generation = pair->generation;
    slave_out->slot = (uint16_t)index;
    slave_out->endpoint = KERNEL_PTY_ENDPOINT_SLAVE;
    slave_out->generation = pair->generation;
    return PTY_RESULT_OK;
}

enum pty_result pty_retain(struct pty_handle handle) {
    struct pty_pair *const pair = pair_from_handle(handle);
    if ((pair == NULL) || (pair->refs[handle.endpoint] == UINT32_MAX)) {
        return PTY_RESULT_INVALID;
    }
    ++pair->refs[handle.endpoint];
    return PTY_RESULT_OK;
}

enum pty_result pty_close(struct pty_handle handle) {
    struct pty_pair *const pair = pair_from_handle(handle);
    uint16_t peer;
    if (pair == NULL) {
        return PTY_RESULT_INVALID;
    }
    --pair->refs[handle.endpoint];
    if (pair->refs[handle.endpoint] == 0U) {
        pair->read_waiter_pid[handle.endpoint] = 0ULL;
        peer = peer_endpoint(handle.endpoint);
        wake_endpoint(pair, peer);
    }
    if ((pair->refs[KERNEL_PTY_ENDPOINT_MASTER] == 0U) &&
        (pair->refs[KERNEL_PTY_ENDPOINT_SLAVE] == 0U)) {
        pair_reset(pair, true);
    }
    return PTY_RESULT_OK;
}

enum pty_result pty_read(struct pty_handle handle, void *buffer, size_t length,
                         size_t *transferred_out) {
    struct pty_pair *const pair = pair_from_handle(handle);
    struct pty_ring *ring;
    uint8_t *bytes = (uint8_t *)buffer;
    size_t transferred = 0U;
    const uint16_t peer = peer_endpoint(handle.endpoint);
    if (transferred_out != NULL) {
        *transferred_out = 0U;
    }
    if ((pair == NULL) || (transferred_out == NULL) ||
        ((buffer == NULL) && (length != 0U))) {
        return PTY_RESULT_INVALID;
    }
    if (length == 0U) {
        return PTY_RESULT_OK;
    }
    ring = incoming_ring(pair, handle.endpoint);
    while ((transferred < length) && (ring->count != 0U)) {
        bytes[transferred] = ring->bytes[ring->read_index];
        ring->read_index = (ring->read_index + 1U) %
            (size_t)KERNEL_PTY_RING_CAPACITY;
        --ring->count;
        ++transferred;
    }
    *transferred_out = transferred;
    if (transferred != 0U) {
        return PTY_RESULT_OK;
    }
    return (pair->refs[peer] == 0U) ? PTY_RESULT_HUP : PTY_RESULT_WOULD_BLOCK;
}

enum pty_result pty_write(struct pty_handle handle, const void *buffer,
                          size_t length, size_t *transferred_out) {
    struct pty_pair *const pair = pair_from_handle(handle);
    struct pty_ring *ring;
    const uint8_t *bytes = (const uint8_t *)buffer;
    size_t transferred = 0U;
    const uint16_t peer = peer_endpoint(handle.endpoint);
    if (transferred_out != NULL) {
        *transferred_out = 0U;
    }
    if ((pair == NULL) || (transferred_out == NULL) ||
        ((buffer == NULL) && (length != 0U))) {
        return PTY_RESULT_INVALID;
    }
    if (pair->refs[peer] == 0U) {
        return PTY_RESULT_HUP;
    }
    ring = outgoing_ring(pair, handle.endpoint);
    while ((transferred < length) &&
           (ring->count < (size_t)KERNEL_PTY_RING_CAPACITY)) {
        ring->bytes[ring->write_index] = bytes[transferred];
        ring->write_index = (ring->write_index + 1U) %
            (size_t)KERNEL_PTY_RING_CAPACITY;
        ++ring->count;
        ++transferred;
    }
    *transferred_out = transferred;
    if (transferred != 0U) {
        wake_endpoint(pair, peer);
        return PTY_RESULT_OK;
    }
    return PTY_RESULT_WOULD_BLOCK;
}

enum pty_result pty_poll(struct pty_handle handle,
                         struct pty_poll_state *state_out) {
    struct pty_pair *const pair = pair_from_handle(handle);
    const uint16_t peer = peer_endpoint(handle.endpoint);
    if ((pair == NULL) || (state_out == NULL)) {
        return PTY_RESULT_INVALID;
    }
    state_out->readable = (incoming_ring(pair, handle.endpoint)->count != 0U);
    state_out->hup = (pair->refs[peer] == 0U);
    return PTY_RESULT_OK;
}

enum pty_result pty_arm_read_waiter(struct pty_handle handle, uint64_t pid) {
    struct pty_pair *const pair = pair_from_handle(handle);
    if ((pair == NULL) || (pid == 0ULL)) {
        return PTY_RESULT_INVALID;
    }
    pair->read_waiter_pid[handle.endpoint] = pid;
    return PTY_RESULT_OK;
}

void pty_cancel_read_waiter(struct pty_handle handle, uint64_t pid) {
    struct pty_pair *const pair = pair_from_handle(handle);
    if ((pair != NULL) && (pair->read_waiter_pid[handle.endpoint] == pid)) {
        pair->read_waiter_pid[handle.endpoint] = 0ULL;
    }
}
