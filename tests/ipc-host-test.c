#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <boring/heap.h>
#include <boring/ipc.h>
#include <boring/event_abi.h>
#include <boring/process.h>
#include <boring/task.h>
#include <boring/user_memory.h>

static unsigned retained_references;
static bool receiver_handle_table_full;
static uint32_t next_receiver_handle = 0x1000U;

void *kmalloc(size_t size) {
    return malloc(size);
}

bool kfree(void *ptr) {
    free(ptr);
    return true;
}

static void fail(const char *message) {
    (void)fprintf(stderr, "ipc-host-test: FAIL: %s\n", message);
    exit(1);
}

static void require(bool condition, const char *message) {
    if (!condition) {
        fail(message);
    }
}

static void reset_process(struct process *process, uint64_t pid) {
    (void)memset(process, 0, sizeof(*process));
    process->pid = pid;
    process->state = PROCESS_ALIVE;
    process->slot_used = true;
}

bool process_is_alive(const struct process *process) {
    return (process != NULL) && process->slot_used &&
           (process->state == PROCESS_ALIVE);
}

bool task_wake_process(struct process *process) {
    return process_is_alive(process);
}

void user_buffer_retained_ref_clear(struct user_buffer_retained_ref *reference) {
    if (reference != NULL) {
        reference->object_index = UINT32_MAX;
        reference->active = false;
    }
}

bool user_buffer_retained_ref_active(
    const struct user_buffer_retained_ref *reference) {
    return (reference != NULL) && reference->active;
}

enum user_memory_result user_buffer_retain(
    struct process *process,
    uint32_t encoded_handle,
    struct user_buffer_retained_ref *reference_out) {
    if (!process_is_alive(process) || (encoded_handle == 0U) ||
        (reference_out == NULL)) {
        return USER_MEMORY_RESULT_INVALID;
    }
    reference_out->object_index = encoded_handle;
    reference_out->active = true;
    ++retained_references;
    return USER_MEMORY_RESULT_OK;
}

enum user_memory_result user_buffer_release_retained(
    struct user_buffer_retained_ref *reference) {
    if (!user_buffer_retained_ref_active(reference) ||
        (retained_references == 0U)) {
        return USER_MEMORY_RESULT_INVALID;
    }
    --retained_references;
    user_buffer_retained_ref_clear(reference);
    return USER_MEMORY_RESULT_OK;
}

void user_buffer_install_ticket_clear(struct user_buffer_install_ticket *ticket) {
    if (ticket != NULL) {
        ticket->object_index = UINT32_MAX;
        ticket->generation = 0U;
        ticket->slot = UINT16_MAX;
        ticket->active = false;
    }
}

enum user_memory_result user_buffer_prepare_install(
    struct process *process,
    const struct user_buffer_retained_ref *reference,
    struct user_buffer_install_ticket *ticket_out,
    uint32_t *handle_out) {
    if (!process_is_alive(process) ||
        !user_buffer_retained_ref_active(reference) ||
        (ticket_out == NULL) || (handle_out == NULL)) {
        return USER_MEMORY_RESULT_INVALID;
    }
    if (receiver_handle_table_full) {
        return USER_MEMORY_RESULT_NO_SPACE;
    }
    ticket_out->object_index = reference->object_index;
    ticket_out->generation = 1U;
    ticket_out->slot = 1U;
    ticket_out->active = true;
    *handle_out = next_receiver_handle++;
    return USER_MEMORY_RESULT_OK;
}

enum user_memory_result user_buffer_commit_install(
    struct process *process,
    struct user_buffer_retained_ref *reference,
    struct user_buffer_install_ticket *ticket) {
    if (!process_is_alive(process) ||
        !user_buffer_retained_ref_active(reference) ||
        (ticket == NULL) || !ticket->active ||
        (retained_references == 0U)) {
        return USER_MEMORY_RESULT_INVALID;
    }
    --retained_references;
    user_buffer_retained_ref_clear(reference);
    user_buffer_install_ticket_clear(ticket);
    return USER_MEMORY_RESULT_OK;
}

static void test_registry_and_fifo(void) {
    struct process server;
    struct process client_a;
    struct process client_b;
    uint32_t listener = 0U;
    uint32_t endpoint_a = 0U;
    uint32_t accepted_a = 0U;
    uint32_t endpoint_b = 0U;
    uint32_t accepted_b = 0U;
    uint8_t byte = 0U;
    struct boring_ipc_receive_kernel_result received;
    unsigned index;

    reset_process(&server, 1ULL);
    reset_process(&client_a, 2ULL);
    reset_process(&client_b, 3ULL);

    require(boring_ipc_service_name_valid("boring.test", 11U),
            "valid service name rejected");
    require(!boring_ipc_service_name_valid("Bad!", 4U),
            "invalid service name accepted");
    require(boring_ipc_service_register(&server, "boring.test", 11U,
                                        &listener) == BORING_IPC_RESULT_OK,
            "service register failed");
    require(listener != BORING_IPC_HANDLE_INVALID,
            "listener handle invalid");
    require(boring_ipc_service_register(&client_b, "boring.test", 11U,
                                        &accepted_b) == BORING_IPC_RESULT_EXISTS,
            "duplicate service not rejected");
    require(boring_ipc_service_accept(&client_a, listener, &accepted_b) ==
                BORING_IPC_RESULT_INVALID,
            "process-local listener isolation failed");

    require(boring_ipc_service_connect(&client_a, "boring.test", 11U,
                                       &endpoint_a) == BORING_IPC_RESULT_OK,
            "client A connect failed");
    require(boring_ipc_service_accept(&server, listener, &accepted_a) ==
                BORING_IPC_RESULT_OK,
            "server accept A failed");
    require(endpoint_a != accepted_a,
            "endpoint handles unexpectedly identical in this proof");
    require(boring_ipc_send(&server, endpoint_a, &byte, 1U, 0U) ==
                BORING_IPC_RESULT_INVALID,
            "foreign endpoint handle was accepted");

    for (index = 0U; index < BORING_IPC_MESSAGES_PER_DIRECTION; ++index) {
        byte = (uint8_t)index;
        require(boring_ipc_send(&client_a, endpoint_a, &byte, 1U, 0U) ==
                    BORING_IPC_RESULT_OK,
                "FIFO enqueue failed");
    }
    byte = 0xffU;
    require(boring_ipc_send(&client_a, endpoint_a, &byte, 1U, 0U) ==
                BORING_IPC_RESULT_NO_SPACE,
            "queue full was not transactional");
    for (index = 0U; index < BORING_IPC_MESSAGES_PER_DIRECTION; ++index) {
        byte = 0xffU;
        received.payload_length = 0U;
        received.buffer_handle = 0U;
        require(boring_ipc_receive(&server, accepted_a, &byte, 1U,
                                   &received) == BORING_IPC_RESULT_OK,
                "FIFO dequeue failed");
        require((byte == (uint8_t)index) &&
                    (received.payload_length == 1U) &&
                    (received.buffer_handle == 0U),
                "FIFO order corrupted");
    }

    require(boring_ipc_service_connect(&client_b, "boring.test", 11U,
                                       &endpoint_b) == BORING_IPC_RESULT_OK,
            "client B connect failed");
    require(boring_ipc_service_accept(&server, listener, &accepted_b) ==
                BORING_IPC_RESULT_OK,
            "server accept B failed");
    require(boring_ipc_close(&server, accepted_b) == BORING_IPC_RESULT_OK,
            "server endpoint close failed");
    require(boring_ipc_receive(&client_b, endpoint_b, &byte, 1U,
                               &received) == BORING_IPC_RESULT_PEER_CLOSED,
            "peer close not visible");
    require(boring_ipc_close(&client_b, endpoint_b) == BORING_IPC_RESULT_OK,
            "client B endpoint close failed");

    require(boring_ipc_close(&server, accepted_a) == BORING_IPC_RESULT_OK,
            "server endpoint A close failed");
    require(boring_ipc_close(&client_a, endpoint_a) == BORING_IPC_RESULT_OK,
            "client A endpoint close failed");
    require(boring_ipc_close(&server, listener) == BORING_IPC_RESULT_OK,
            "listener close failed");
    require(boring_ipc_service_register(&client_b, "boring.test", 11U,
                                        &listener) == BORING_IPC_RESULT_OK,
            "same-name re-registration failed");
    boring_ipc_process_cleanup(&client_b);
}

static void test_attachment_retry_and_cleanup(void) {
    struct process server;
    struct process client;
    struct boring_ipc_receive_kernel_result received;
    struct boring_ipc_stats stats;
    uint32_t listener = 0U;
    uint32_t endpoint = 0U;
    uint32_t accepted = 0U;
    uint8_t sent[3] = { 1U, 2U, 3U };
    uint8_t output[3] = { 0U, 0U, 0U };

    reset_process(&server, 1ULL);
    reset_process(&client, 2ULL);
    retained_references = 0U;
    receiver_handle_table_full = false;

    require(boring_ipc_service_register(&server, "grant.test", 10U,
                                        &listener) == BORING_IPC_RESULT_OK,
            "grant service register failed");
    require(boring_ipc_service_connect(&client, "grant.test", 10U,
                                       &endpoint) == BORING_IPC_RESULT_OK,
            "grant connect failed");
    require(boring_ipc_service_accept(&server, listener, &accepted) ==
                BORING_IPC_RESULT_OK,
            "grant accept failed");
    require(boring_ipc_send(&client, endpoint, sent, sizeof(sent), 77U) ==
                BORING_IPC_RESULT_OK,
            "attachment send failed");
    require(retained_references == 1U,
            "queued attachment was not retained");

    receiver_handle_table_full = true;
    require(boring_ipc_receive(&server, accepted, output, sizeof(output),
                               &received) == BORING_IPC_RESULT_NO_SPACE,
            "full receiver table did not preserve message");
    require(retained_references == 1U,
            "failed receive consumed queued retain");
    receiver_handle_table_full = false;
    require(boring_ipc_receive(&server, accepted, output, sizeof(output),
                               &received) == BORING_IPC_RESULT_OK,
            "attachment retry failed");
    require((memcmp(output, sent, sizeof(sent)) == 0) &&
                (received.buffer_handle != 0U) &&
                (received.buffer_handle != 77U),
            "attachment grant semantics wrong");
    require(retained_references == 0U,
            "queued retain not transferred on commit");

    require(boring_ipc_send(&client, endpoint, sent, sizeof(sent), 88U) ==
                BORING_IPC_RESULT_OK,
            "cleanup attachment send failed");
    require(retained_references == 1U,
            "cleanup attachment not retained");
    boring_ipc_process_cleanup(&server);
    require(retained_references == 0U,
            "process cleanup leaked queued attachment");
    boring_ipc_process_cleanup(&client);
    require(boring_ipc_get_stats(&stats), "stats unavailable");
    require((stats.live_services == 0U) && (stats.live_connections == 0U) &&
                (stats.queued_messages == 0U) &&
                (stats.retained_buffer_attachments == 0U),
            "IPC resources survived cleanup");
}

static void test_readiness_and_peer_identity(void) {
    struct process server, client, foreign;
    uint32_t listener, endpoint, accepted, events;
    uint64_t peer;
    uint8_t byte = 42U, output = 0U;
    struct boring_ipc_receive_kernel_result received;
    reset_process(&server, 1ULL); reset_process(&client, 2ULL); reset_process(&foreign, 3ULL);
    require(boring_ipc_service_register(&server, "poll", 4U, &listener) == BORING_IPC_RESULT_OK, "poll register");
    require(boring_ipc_poll(&server, listener, &events, &peer) == BORING_IPC_RESULT_OK && events == 0U && peer == 0ULL, "empty listener readiness");
    require(boring_ipc_service_connect(&client, "poll", 4U, &endpoint) == BORING_IPC_RESULT_OK, "poll connect");
    require(boring_ipc_poll(&server, listener, &events, &peer) == BORING_IPC_RESULT_OK && events == BORING_EVENT_READ, "pending listener readiness");
    require(boring_ipc_service_accept(&server, listener, &accepted) == BORING_IPC_RESULT_OK, "poll accept");
    require(boring_ipc_poll(&server, accepted, &events, &peer) == BORING_IPC_RESULT_OK && events == 0U && peer == 2ULL, "authenticated client peer");
    require(boring_ipc_poll(&client, endpoint, &events, &peer) == BORING_IPC_RESULT_OK && peer == 1ULL, "authenticated service peer");
    require(boring_ipc_poll(&foreign, accepted, &events, &peer) == BORING_IPC_RESULT_INVALID, "foreign process handle poll");
    require(boring_ipc_send(&client, endpoint, &byte, 1U, 0U) == BORING_IPC_RESULT_OK, "poll send");
    require(boring_ipc_poll(&server, accepted, &events, &peer) == BORING_IPC_RESULT_OK && events == BORING_EVENT_READ, "readable queue");
    require(boring_ipc_poll(&server, accepted, &events, &peer) == BORING_IPC_RESULT_OK && events == BORING_EVENT_READ, "poll does not consume");
    require(boring_ipc_close(&client, endpoint) == BORING_IPC_RESULT_OK, "poll close");
    require(boring_ipc_poll(&server, accepted, &events, &peer) == BORING_IPC_RESULT_OK && events == (BORING_EVENT_READ | BORING_EVENT_HUP) && peer == 0ULL, "queued message plus HUP");
    require(boring_ipc_receive(&server, accepted, &output, 1U, &received) == BORING_IPC_RESULT_OK && output == byte, "poll retains queued bytes");
    require(boring_ipc_poll(&server, accepted, &events, &peer) == BORING_IPC_RESULT_OK && events == BORING_EVENT_HUP, "drained HUP");
    require(boring_ipc_close(&server, accepted) == BORING_IPC_RESULT_OK, "poll close accepted");
    require(boring_ipc_poll(&server, accepted, &events, &peer) == BORING_IPC_RESULT_INVALID, "stale poll handle");
    boring_ipc_process_cleanup(&server); boring_ipc_process_cleanup(&client);
}

int main(void) {
    require(boring_ipc_system_init(), "IPC init failed");
    test_registry_and_fifo();
    require(boring_ipc_host_reset(), "host reset after FIFO failed");
    test_attachment_retry_and_cleanup();
    require(boring_ipc_host_reset(), "host reset after cleanup failed");
    test_readiness_and_peer_identity();
    require(boring_ipc_host_reset(), "host reset after readiness failed");
    (void)puts("ipc-host-test: all bounded registry/FIFO/grant tests passed.");
    return 0;
}
