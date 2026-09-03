#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/io.h>
#include <boring/ipc.h>
#include <boring/process.h>

#ifndef BORING_M61_PHYSICAL_BREADCRUMBS
#error "M61 WM/display gap witness must stay candidate-build gated"
#endif

#define M61_WM_DISPLAY_POST_PORT 0x80U
#define M61_DISPLAY_CONTROL_VERSION 2U
#define M61_DISPLAY_MANAGER 11U
#define M61_DISPLAY_PRESENT 16U
#define M61_DISPLAY_REPLY 18U
#define M61_DISPLAY_STATUS_OK 0U
#define M61_DISPLAY_MESSAGE_SIZE 56U

/*
 * Tiny physical boundary witness for the remaining WM -> display gap.
 * These codes are intentionally separate from all established M61 POST maps.
 */
enum m61_wm_display_post_code {
    M61_WM_DISPLAY_CONNECT_OK = 0x40,
    M61_WM_MANAGER_REQUEST_SENT = 0x41,
    M61_WM_MANAGER_REPLY_OK = 0x42,
    M61_WM_DISPLAY_PRESENT_SENT = 0x43,
    M61_DISPLAY_PRESENT_RECEIVED = 0x44,
    M61_WM_MANAGER_REPLY_REJECTED = 0x45
};

const char boring_m61_wm_display_gap_enabled[] =
    "M61 WM to display IPC boundary witness enabled";
const uint8_t boring_m61_wm_display_gap_post_codes[] = {
    0x40U, 0x41U, 0x42U, 0x43U, 0x44U, 0x45U
};

static uint64_t wm_pid;
static uint32_t wm_display_endpoint;
static bool connect_posted;
static bool manager_request_posted;
static bool manager_reply_posted;
static bool present_sent_posted;
static bool present_received_posted;

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

static bool process_name_ends_with(const struct process *process,
                                   const char *ending) {
    size_t value_length = 0U;
    size_t ending_length = 0U;
    size_t index;

    if ((process == NULL) || (ending == NULL)) {
        return false;
    }
    while ((value_length <= KERNEL_PROCESS_NAME_MAX) &&
           (process->name[value_length] != '\0')) {
        ++value_length;
    }
    while ((ending_length <= KERNEL_PROCESS_NAME_MAX) &&
           (ending[ending_length] != '\0')) {
        ++ending_length;
    }
    if ((value_length > KERNEL_PROCESS_NAME_MAX) ||
        (ending_length > KERNEL_PROCESS_NAME_MAX) ||
        (value_length < ending_length)) {
        return false;
    }
    for (index = 0U; index < ending_length; ++index) {
        if (process->name[value_length - ending_length + index] !=
            ending[index]) {
            return false;
        }
    }
    return true;
}

static uint32_t read_u32(const uint8_t *value) {
    return (uint32_t)value[0] |
           ((uint32_t)value[1] << 8U) |
           ((uint32_t)value[2] << 16U) |
           ((uint32_t)value[3] << 24U);
}

static bool display_message(const uint8_t *payload, size_t length,
                            uint32_t type) {
    return (payload != NULL) && (length == M61_DISPLAY_MESSAGE_SIZE) &&
           (read_u32(payload) == M61_DISPLAY_CONTROL_VERSION) &&
           (read_u32(payload + 4U) == type);
}

static void post(uint8_t code) {
    x86_64_out8((uint16_t)M61_WM_DISPLAY_POST_PORT, code);
}

enum boring_ipc_result __real_boring_ipc_service_connect(
    struct process *, const char *, size_t, uint32_t *);
enum boring_ipc_result __wrap_boring_ipc_service_connect(
    struct process *, const char *, size_t, uint32_t *);
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

enum boring_ipc_result __wrap_boring_ipc_service_connect(
    struct process *process, const char *name, size_t length,
    uint32_t *handle_out) {
    enum boring_ipc_result result;

    result = __real_boring_ipc_service_connect(
        process, name, length, handle_out);
    if ((result == BORING_IPC_RESULT_OK) && (handle_out != NULL) &&
        (*handle_out != BORING_IPC_HANDLE_INVALID) &&
        process_name_ends_with(process, "boringwm") &&
        same_bytes(name, length, "boring.display", 14U)) {
        wm_pid = process->pid;
        wm_display_endpoint = *handle_out;
        if (!connect_posted) {
            connect_posted = true;
            post((uint8_t)M61_WM_DISPLAY_CONNECT_OK);
        }
    }
    return result;
}

enum boring_ipc_result __wrap_boring_ipc_send(
    struct process *process, uint32_t endpoint_handle,
    const uint8_t *payload, size_t payload_length, uint32_t buffer_handle) {
    enum boring_ipc_result result;

    result = __real_boring_ipc_send(process, endpoint_handle, payload,
                                    payload_length, buffer_handle);
    if ((result != BORING_IPC_RESULT_OK) || (process == NULL) ||
        (process->pid != wm_pid) ||
        (endpoint_handle != wm_display_endpoint)) {
        return result;
    }
    if (!manager_request_posted &&
        display_message(payload, payload_length, M61_DISPLAY_MANAGER)) {
        manager_request_posted = true;
        post((uint8_t)M61_WM_MANAGER_REQUEST_SENT);
    } else if (!present_sent_posted &&
               display_message(payload, payload_length,
                               M61_DISPLAY_PRESENT)) {
        present_sent_posted = true;
        post((uint8_t)M61_WM_DISPLAY_PRESENT_SENT);
    }
    return result;
}

enum boring_ipc_result __wrap_boring_ipc_receive(
    struct process *process, uint32_t endpoint_handle, uint8_t *payload,
    size_t payload_capacity,
    struct boring_ipc_receive_kernel_result *result_out) {
    enum boring_ipc_result result;
    size_t payload_length;

    result = __real_boring_ipc_receive(process, endpoint_handle, payload,
                                       payload_capacity, result_out);
    if ((result != BORING_IPC_RESULT_OK) || (process == NULL) ||
        (result_out == NULL)) {
        return result;
    }
    payload_length = result_out->payload_length;

    if ((process->pid == wm_pid) &&
        (endpoint_handle == wm_display_endpoint) &&
        !manager_reply_posted &&
        (payload != NULL) && (payload_length == M61_DISPLAY_MESSAGE_SIZE) &&
        (read_u32(payload + 4U) == M61_DISPLAY_REPLY)) {
        manager_reply_posted = true;
        if ((read_u32(payload) == M61_DISPLAY_CONTROL_VERSION) &&
            (read_u32(payload + 8U) == M61_DISPLAY_STATUS_OK)) {
            post((uint8_t)M61_WM_MANAGER_REPLY_OK);
        } else {
            post((uint8_t)M61_WM_MANAGER_REPLY_REJECTED);
        }
    }

    if (!present_received_posted &&
        process_name_ends_with(process, "boring-display") &&
        display_message(payload, payload_length, M61_DISPLAY_PRESENT)) {
        present_received_posted = true;
        post((uint8_t)M61_DISPLAY_PRESENT_RECEIVED);
    }
    return result;
}
