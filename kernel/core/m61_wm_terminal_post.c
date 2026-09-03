#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/io.h>
#include <boring/ipc.h>
#include <boring/m36_syscall.h>
#include <boring/process.h>
#include <boring/serial.h>
#include <boring/syscall.h>
#include <boring/syscall_abi.h>

#ifndef BORING_M61_PHYSICAL_BREADCRUMBS
#error "M61 WM-to-terminal POST trace must stay candidate-build gated"
#endif

#define M61_DISPLAY_CONTROL_VERSION 2U
#define M61_DISPLAY_MANAGER 11U
#define M61_DISPLAY_REPLY 18U
#define M61_DISPLAY_STATUS_OK 0U

enum m61_wm_terminal_post_code {
    M61_POST_WM_DISPLAY_CONNECT_OK = 0x9a,
    M61_POST_WM_DISPLAY_PEER_OK = 0x9b,
    M61_POST_WM_MANAGER_SEND_OK = 0x9c,
    M61_POST_WM_MANAGER_REPLY_OK = 0x9d,
    M61_POST_WM_AUTOSPAWN_ENTER = 0x9e,
    M61_POST_WM_AUTOSPAWN_POSITIVE = 0x9f,
    M61_POST_WM_AUTOSPAWN_FAILED = 0x5e
};

const uint8_t boring_m61_wm_terminal_post_codes[] = {
    0x9aU, 0x9bU, 0x9cU, 0x9dU, 0x9eU, 0x9fU, 0x5eU
};

static uint8_t wm_terminal_state;

static bool same_bytes(const char *value, size_t value_length,
                       const char *expected, size_t expected_length) {
    size_t index;

    if ((value == NULL) || (expected == NULL) ||
        (value_length != expected_length)) {
        return false;
    }
    for (index = 0U; index < value_length; ++index) {
        if (value[index] != expected[index]) {
            return false;
        }
    }
    return true;
}

static bool process_is_boringwm(const struct process *process) {
    static const char expected[] = "boringwm";
    size_t index;

    if ((process == NULL) || !process_is_alive(process)) {
        return false;
    }
    for (index = 0U; index < sizeof(expected); ++index) {
        if (process->name[index] != expected[index]) {
            return false;
        }
    }
    return true;
}

static uint32_t load_u32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) |
           ((uint32_t)bytes[3] << 24U);
}

static bool display_control_is_manager(const uint8_t *payload,
                                       size_t payload_length) {
    return (payload != NULL) && (payload_length >= 8U) &&
           (load_u32(&payload[0]) == M61_DISPLAY_CONTROL_VERSION) &&
           (load_u32(&payload[4]) == M61_DISPLAY_MANAGER);
}

static bool display_event_is_ok_reply(const uint8_t *payload,
                                      size_t payload_length) {
    return (payload != NULL) && (payload_length >= 12U) &&
           (load_u32(&payload[0]) == M61_DISPLAY_CONTROL_VERSION) &&
           (load_u32(&payload[4]) == M61_DISPLAY_REPLY) &&
           (load_u32(&payload[8]) == M61_DISPLAY_STATUS_OK);
}

static void emit_post(uint8_t code, const char *label) {
    x86_64_out8((uint16_t)0x80U, code);
    serial_write_string("M61 WM->TERMINAL POST ");
    serial_write_hex_u64((uint64_t)code);
    serial_write_string(" ");
    serial_write_string(label);
    serial_write_string("\n");
}

enum boring_ipc_result __real_boring_ipc_service_connect(
    struct process *, const char *, size_t, uint32_t *);
enum boring_ipc_result __wrap_boring_ipc_service_connect(
    struct process *, const char *, size_t, uint32_t *);
enum boring_ipc_result __real_boring_ipc_poll(
    struct process *, uint32_t, uint32_t *, uint64_t *);
enum boring_ipc_result __wrap_boring_ipc_poll(
    struct process *, uint32_t, uint32_t *, uint64_t *);
enum boring_ipc_result __real_boring_ipc_send(
    struct process *, uint32_t, const uint8_t *, size_t, uint32_t);
enum boring_ipc_result __wrap_boring_ipc_send(
    struct process *, uint32_t, const uint8_t *, size_t, uint32_t);
enum boring_ipc_result __real_boring_ipc_receive(
    struct process *, uint32_t, uint8_t *, size_t,
    struct boring_ipc_receive_kernel_result *);
enum boring_ipc_result __wrap_boring_ipc_receive(
    struct process *, uint32_t, uint8_t *, size_t,
    struct boring_ipc_receive_kernel_result *);
void __real_x86_64_syscall_dispatch_m36(struct x86_64_syscall_frame *);
void __wrap_x86_64_syscall_dispatch_m36(struct x86_64_syscall_frame *);

enum boring_ipc_result __wrap_boring_ipc_service_connect(
    struct process *process, const char *name, size_t length,
    uint32_t *handle_out) {
    const enum boring_ipc_result result = __real_boring_ipc_service_connect(
        process, name, length, handle_out);

    if ((wm_terminal_state == 0U) && process_is_boringwm(process) &&
        same_bytes(name, length, "boring.display", 14U) &&
        (result == BORING_IPC_RESULT_OK)) {
        wm_terminal_state = 1U;
        emit_post((uint8_t)M61_POST_WM_DISPLAY_CONNECT_OK,
                  "boring.display connect OK");
    }
    return result;
}

enum boring_ipc_result __wrap_boring_ipc_poll(
    struct process *process, uint32_t handle, uint32_t *events,
    uint64_t *peer_pid) {
    const enum boring_ipc_result result = __real_boring_ipc_poll(
        process, handle, events, peer_pid);

    if ((wm_terminal_state == 1U) && process_is_boringwm(process) &&
        (result == BORING_IPC_RESULT_OK) && (peer_pid != NULL) &&
        (*peer_pid != 0ULL)) {
        wm_terminal_state = 2U;
        emit_post((uint8_t)M61_POST_WM_DISPLAY_PEER_OK,
                  "boring.display peer OK");
    }
    return result;
}

enum boring_ipc_result __wrap_boring_ipc_send(
    struct process *process, uint32_t endpoint_handle,
    const uint8_t *payload, size_t payload_length, uint32_t buffer_handle) {
    const bool manager = (wm_terminal_state == 2U) &&
        process_is_boringwm(process) &&
        display_control_is_manager(payload, payload_length);
    const enum boring_ipc_result result = __real_boring_ipc_send(
        process, endpoint_handle, payload, payload_length, buffer_handle);

    if (manager && (result == BORING_IPC_RESULT_OK)) {
        wm_terminal_state = 3U;
        emit_post((uint8_t)M61_POST_WM_MANAGER_SEND_OK,
                  "DISPLAY_MANAGER request send OK");
    }
    return result;
}

enum boring_ipc_result __wrap_boring_ipc_receive(
    struct process *process, uint32_t endpoint_handle, uint8_t *payload,
    size_t payload_capacity,
    struct boring_ipc_receive_kernel_result *result_out) {
    const enum boring_ipc_result result = __real_boring_ipc_receive(
        process, endpoint_handle, payload, payload_capacity, result_out);

    if ((wm_terminal_state == 3U) && process_is_boringwm(process) &&
        (result == BORING_IPC_RESULT_OK) && (result_out != NULL) &&
        display_event_is_ok_reply(payload, result_out->payload_length)) {
        wm_terminal_state = 4U;
        emit_post((uint8_t)M61_POST_WM_MANAGER_REPLY_OK,
                  "DISPLAY_MANAGER reply OK");
    }
    return result;
}

void __wrap_x86_64_syscall_dispatch_m36(struct x86_64_syscall_frame *frame) {
    const bool terminal_spawn = (wm_terminal_state == 4U) &&
        (frame != NULL) &&
        (frame->syscall_number == (uint64_t)BORING_SYS_SPAWN) &&
        process_is_boringwm(process_current());

    if (terminal_spawn) {
        wm_terminal_state = 5U;
        emit_post((uint8_t)M61_POST_WM_AUTOSPAWN_ENTER,
                  "automatic terminal SYS_SPAWN entered");
    }
    __real_x86_64_syscall_dispatch_m36(frame);
    if (terminal_spawn) {
        if ((int64_t)frame->result > 0) {
            wm_terminal_state = 6U;
            emit_post((uint8_t)M61_POST_WM_AUTOSPAWN_POSITIVE,
                      "automatic terminal SYS_SPAWN returned positive");
        } else {
            emit_post((uint8_t)M61_POST_WM_AUTOSPAWN_FAILED,
                      "automatic terminal SYS_SPAWN failed");
        }
    }
}
