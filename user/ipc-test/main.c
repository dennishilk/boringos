#include <stddef.h>
#include <stdint.h>

#include <boring/ipc.h>
#include <boring/string.h>
#include <boring/syscall.h>
#include <boring/syscall_abi.h>

#define TEST_SERVICE "boring.test"
#define TEST_SERVICE_LEN 11U
#define TEST_MARKER 0x4d33334950435453ULL
#define IPC_QUEUE_DEPTH 16U

struct test_message {
    uint32_t sequence;
    uint32_t sender_buffer_handle;
    uint64_t marker;
};

_Static_assert(sizeof(struct test_message) == 16U,
               "M33 test message must be fixed-size");

static void say(const char *text) {
    (void)boring_debug_write(text, boring_strlen(text));
}

static void fail(const char *text) __attribute__((noreturn));
static void fail(const char *text) {
    say("ipc-test: FAILED: ");
    say(text);
    say("\n");
    boring_exit(99);
}

static void require_long(long actual, long expected, const char *name) {
    if (actual != expected) {
        fail(name);
    }
}

static void require_true(int condition, const char *name) {
    if (condition == 0) {
        fail(name);
    }
}

static void fill_message(struct test_message *message,
                         uint32_t sequence,
                         uint32_t sender_buffer_handle) {
    message->sequence = sequence;
    message->sender_buffer_handle = sender_buffer_handle;
    message->marker = TEST_MARKER;
}

static int message_valid(const struct test_message *message,
                         uint32_t expected_sequence) {
    return (message->sequence == expected_sequence) &&
           (message->marker == TEST_MARKER);
}

static void run_server(void) __attribute__((noreturn));
static void run_server(void) {
    uint32_t filler[BORING_BUFFER_HANDLE_MAX];
    struct test_message message;
    struct boring_ipc_receive_result received;
    long listener_raw;
    long endpoint_raw;
    uint32_t listener;
    uint32_t endpoint;
    uint32_t index;
    uint8_t *shared;

    listener_raw = boring_service_register(TEST_SERVICE, TEST_SERVICE_LEN);
    require_true(listener_raw > 0L, "server register");
    listener = (uint32_t)listener_raw;
    require_long(boring_service_register(TEST_SERVICE, TEST_SERVICE_LEN),
                 -(long)BORING_SYSCALL_EEXIST,
                 "duplicate service register");
    require_long(boring_service_accept(BORING_IPC_HANDLE_INVALID),
                 -(long)BORING_SYSCALL_EINVAL,
                 "invalid accept handle");
    say("ipc-test: service registered\n");

    /* No client exists yet: this call must block and later wake on CONNECT. */
    endpoint_raw = boring_service_accept(listener);
    require_true(endpoint_raw > 0L, "blocking accept wake");
    endpoint = (uint32_t)endpoint_raw;
    say("ipc-test: blocking accept wake passed\n");

    /* Fill the receiver M32 capability table before the first attachment. */
    for (index = 0U; index < BORING_BUFFER_HANDLE_MAX; ++index) {
        const long raw = boring_buffer_create((size_t)BORING_MEMORY_PAGE_SIZE);
        require_true(raw > 0L, "receiver handle-table fill");
        filler[index] = (uint32_t)raw;
    }

    require_long(boring_ipc_receive(endpoint, &message, 1U, &received),
                 -(long)BORING_SYSCALL_ENOSPC,
                 "short receive rollback");
    require_long(boring_ipc_receive(endpoint, &message, sizeof(message),
                                    (struct boring_ipc_receive_result *)(uintptr_t)1U),
                 -(long)BORING_SYSCALL_EFAULT,
                 "receive result pointer rollback");
    require_long(boring_ipc_receive(endpoint, &message, sizeof(message),
                                    &received),
                 -(long)BORING_SYSCALL_ENOSPC,
                 "full receiver handle table");
    require_long(boring_buffer_close(filler[BORING_BUFFER_HANDLE_MAX - 1U]),
                 0L, "free receiver handle slot");

    require_long(boring_ipc_receive(endpoint, &message, sizeof(message),
                                    &received),
                 0L, "retry attached receive");
    require_true(message_valid(&message, 0U), "fifo first message");
    require_true(received.payload_length == sizeof(message),
                 "first payload length");
    require_true(received.buffer_handle != BORING_BUFFER_HANDLE_INVALID,
                 "first attachment exists");
    require_true(received.buffer_handle != message.sender_buffer_handle,
                 "sender receiver handles differ");
    shared = (uint8_t *)boring_buffer_map(received.buffer_handle);
    require_true(shared != NULL, "map received shared buffer");
    require_true((shared[0] == 0x11U) && (shared[1] == 0x22U),
                 "same backing initial bytes");
    shared[0] = 0x7aU;
    shared[1] = 0xb4U;
    require_long(boring_buffer_unmap(shared), 0L,
                 "unmap received shared buffer");
    require_long(boring_buffer_close(received.buffer_handle), 0L,
                 "close received shared buffer");
    say("ipc-test: M32 shared-buffer grant passed\n");

    /* Message 1 owns only a queued retain: sender already closed its handle. */
    require_long(boring_ipc_receive(endpoint, &message, sizeof(message),
                                    &received),
                 0L, "queued retain receive");
    require_true(message_valid(&message, 1U), "fifo second message");
    require_true(received.buffer_handle != BORING_BUFFER_HANDLE_INVALID,
                 "queued retain attachment");
    shared = (uint8_t *)boring_buffer_map(received.buffer_handle);
    require_true(shared != NULL, "queued retain map");
    require_true((shared[0] == 0x55U) && (shared[1] == 0xaaU),
                 "queued reference kept backing alive");
    require_long(boring_buffer_unmap(shared), 0L, "queued retain unmap");
    require_long(boring_buffer_close(received.buffer_handle), 0L,
                 "queued retain close");
    say("ipc-test: queued buffer lifetime passed\n");

    for (index = 2U; index < IPC_QUEUE_DEPTH; ++index) {
        require_long(boring_ipc_receive(endpoint, &message, sizeof(message),
                                        &received),
                     0L, "fifo drain");
        require_true(message_valid(&message, index), "fifo order");
        require_true(received.buffer_handle == BORING_BUFFER_HANDLE_INVALID,
                     "unexpected attachment");
    }
    say("ipc-test: FIFO and queue-full transaction passed\n");

    for (index = 0U; index + 1U < BORING_BUFFER_HANDLE_MAX; ++index) {
        require_long(boring_buffer_close(filler[index]), 0L,
                     "receiver filler cleanup");
    }

    fill_message(&message, 100U, 0U);
    require_long(boring_ipc_send(endpoint, &message, sizeof(message),
                                 BORING_IPC_NO_ATTACHED_BUFFER),
                 0L, "pong A send");
    require_long(boring_ipc_close(endpoint), 0L, "server close A endpoint");

    /* Client B is already pending. */
    endpoint_raw = boring_service_accept(listener);
    require_true(endpoint_raw > 0L, "accept client B");
    endpoint = (uint32_t)endpoint_raw;
    fill_message(&message, 200U, 0U);
    require_long(boring_ipc_send(endpoint, &message, sizeof(message),
                                 BORING_IPC_NO_ATTACHED_BUFFER),
                 0L, "client B ready");

    /* B waits for READY before sending, so this RECEIVE must really block. */
    require_long(boring_ipc_receive(endpoint, &message, sizeof(message),
                                    &received),
                 0L, "blocking receive wake");
    require_true(message_valid(&message, 201U), "client B request");
    say("ipc-test: blocking receive wake passed\n");

    fill_message(&message, 202U, 0U);
    require_long(boring_ipc_send(endpoint, &message, sizeof(message),
                                 BORING_IPC_NO_ATTACHED_BUFFER),
                 0L, "pong B send");

    /* Deliberately leak listener + endpoint to process-exit cleanup. */
    say("ipc-test: server exiting with live service\n");
    boring_exit(0);
}

static void run_client_a(void) __attribute__((noreturn));
static void run_client_a(void) {
    struct test_message message;
    struct boring_ipc_receive_result received;
    long endpoint_raw;
    long buffer_raw;
    long queued_raw;
    uint32_t endpoint;
    uint32_t buffer_handle;
    uint32_t queued_handle;
    uint8_t *shared;
    uint8_t *queued;
    uint32_t index;

    require_long(boring_service_register("Bad!", 4U),
                 -(long)BORING_SYSCALL_EINVAL,
                 "invalid service name");
    require_long(boring_service_connect("missing.service", 15U),
                 -(long)BORING_SYSCALL_ENOENT,
                 "missing service");
    require_long(boring_service_connect((const char *)(uintptr_t)1U, 4U),
                 -(long)BORING_SYSCALL_EFAULT,
                 "service pointer fault");

    endpoint_raw = boring_service_connect(TEST_SERVICE, TEST_SERVICE_LEN);
    require_true(endpoint_raw > 0L, "client A connect");
    endpoint = (uint32_t)endpoint_raw;
    require_long(boring_service_accept(endpoint),
                 -(long)BORING_SYSCALL_EINVAL,
                 "endpoint cannot accept");
    require_long(boring_ipc_send(BORING_IPC_HANDLE_INVALID, &message,
                                 sizeof(message), BORING_IPC_NO_ATTACHED_BUFFER),
                 -(long)BORING_SYSCALL_EINVAL,
                 "send invalid endpoint");
    require_long(boring_ipc_send(endpoint, (const void *)(uintptr_t)1U,
                                 sizeof(message), BORING_IPC_NO_ATTACHED_BUFFER),
                 -(long)BORING_SYSCALL_EFAULT,
                 "send pointer fault");
    require_long(boring_ipc_send(endpoint, &message,
                                 BORING_IPC_INLINE_PAYLOAD_MAX + 1U,
                                 BORING_IPC_NO_ATTACHED_BUFFER),
                 -(long)BORING_SYSCALL_EINVAL,
                 "send oversized payload");
    require_long(boring_ipc_receive(BORING_IPC_HANDLE_INVALID, &message,
                                    sizeof(message), &received),
                 -(long)BORING_SYSCALL_EINVAL,
                 "receive invalid endpoint");
    require_long(boring_ipc_receive(endpoint, &message,
                                    BORING_IPC_INLINE_PAYLOAD_MAX + 1U,
                                    &received),
                 -(long)BORING_SYSCALL_EINVAL,
                 "receive oversized capacity");
    require_long(boring_ipc_close(BORING_IPC_HANDLE_INVALID),
                 -(long)BORING_SYSCALL_EINVAL,
                 "close invalid handle");
    say("ipc-test: negative syscall paths passed\n");

    buffer_raw = boring_buffer_create((size_t)BORING_MEMORY_PAGE_SIZE);
    require_true(buffer_raw > 0L, "client A buffer create");
    buffer_handle = (uint32_t)buffer_raw;
    shared = (uint8_t *)boring_buffer_map(buffer_handle);
    require_true(shared != NULL, "client A buffer map");
    shared[0] = 0x11U;
    shared[1] = 0x22U;

    queued_raw = boring_buffer_create((size_t)BORING_MEMORY_PAGE_SIZE);
    require_true(queued_raw > 0L, "queued buffer create");
    queued_handle = (uint32_t)queued_raw;
    queued = (uint8_t *)boring_buffer_map(queued_handle);
    require_true(queued != NULL, "queued buffer map");
    queued[0] = 0x55U;
    queued[1] = 0xaaU;

    fill_message(&message, 0U, buffer_handle);
    require_long(boring_ipc_send(endpoint, &message, sizeof(message),
                                 buffer_handle),
                 0L, "send attached buffer");
    fill_message(&message, 1U, queued_handle);
    require_long(boring_ipc_send(endpoint, &message, sizeof(message),
                                 queued_handle),
                 0L, "send queued-only buffer");
    require_long(boring_buffer_unmap(queued), 0L, "queued sender unmap");
    require_long(boring_buffer_close(queued_handle), 0L,
                 "queued sender close");

    for (index = 2U; index < IPC_QUEUE_DEPTH; ++index) {
        fill_message(&message, index, 0U);
        require_long(boring_ipc_send(endpoint, &message, sizeof(message),
                                     BORING_IPC_NO_ATTACHED_BUFFER),
                     0L, "fill FIFO queue");
    }
    fill_message(&message, IPC_QUEUE_DEPTH, buffer_handle);
    require_long(boring_ipc_send(endpoint, &message, sizeof(message),
                                 buffer_handle),
                 -(long)BORING_SYSCALL_ENOSPC,
                 "queue full transaction");
    require_true((shared[0] == 0x11U) && (shared[1] == 0x22U),
                 "sender capability after full queue");

    require_long(boring_ipc_receive(endpoint, &message, sizeof(message),
                                    &received),
                 0L, "client A pong receive");
    require_true(message_valid(&message, 100U), "client A pong");
    require_true((shared[0] == 0x7aU) && (shared[1] == 0xb4U),
                 "cross-process alias visible");
    require_long(boring_ipc_receive(endpoint, &message, sizeof(message),
                                    &received),
                 -(long)BORING_SYSCALL_EPIPE,
                 "client A peer close");
    say("ipc-test: sender retains capability and alias passed\n");
    say("ipc-test: peer close passed\n");

    require_long(boring_buffer_unmap(shared), 0L, "client A unmap");
    require_long(boring_buffer_close(buffer_handle), 0L, "client A close buffer");
    require_long(boring_ipc_close(endpoint), 0L, "client A endpoint close");
    require_long(boring_ipc_close(endpoint), -(long)BORING_SYSCALL_EINVAL,
                 "stale IPC handle rejected");
    boring_exit(0);
}

static void run_client_b(void) __attribute__((noreturn));
static void run_client_b(void) {
    struct test_message message;
    struct boring_ipc_receive_result received;
    long endpoint_raw;
    long listener_raw;
    uint32_t endpoint;

    endpoint_raw = boring_service_connect(TEST_SERVICE, TEST_SERVICE_LEN);
    require_true(endpoint_raw > 0L, "client B connect");
    endpoint = (uint32_t)endpoint_raw;

    require_long(boring_ipc_receive(endpoint, &message, sizeof(message),
                                    &received),
                 0L, "client B ready receive");
    require_true(message_valid(&message, 200U), "client B ready message");
    fill_message(&message, 201U, 0U);
    require_long(boring_ipc_send(endpoint, &message, sizeof(message),
                                 BORING_IPC_NO_ATTACHED_BUFFER),
                 0L, "client B request send");
    require_long(boring_ipc_receive(endpoint, &message, sizeof(message),
                                    &received),
                 0L, "client B pong receive");
    require_true(message_valid(&message, 202U), "client B pong");
    require_long(boring_ipc_receive(endpoint, &message, sizeof(message),
                                    &received),
                 -(long)BORING_SYSCALL_EPIPE,
                 "server exit peer close");

    /* Server exited without closing its listener. Exit cleanup must remove it. */
    listener_raw = boring_service_register(TEST_SERVICE, TEST_SERVICE_LEN);
    require_true(listener_raw > 0L, "same-name service re-register");
    require_long(boring_ipc_close((uint32_t)listener_raw), 0L,
                 "client B temporary listener close");
    require_long(boring_ipc_close(endpoint), 0L, "client B endpoint close");
    say("ipc-test: process-exit service removal passed\n");
    say("ipc-test: same-name re-registration passed\n");
    boring_exit(0);
}

int boring_main(void) {
    const uint64_t pid = boring_getpid();

    if (pid == 1ULL) {
        run_server();
    }
    if (pid == 2ULL) {
        run_client_a();
    }
    if (pid == 3ULL) {
        run_client_b();
    }
    fail("unexpected pid");
}
