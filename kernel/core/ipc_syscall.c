#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/cpu.h>
#include <boring/ipc.h>
#include <boring/ipc_syscall.h>
#include <boring/process.h>
#include <boring/ring3_memory.h>
#include <boring/serial.h>
#include <boring/syscall.h>
#include <boring/syscall_abi.h>
#include <boring/task.h>
#include <boring/user_memory.h>
#include <boring/vmm.h>

#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
#include <boring/io.h>

#define M61_WM_DISPLAY_POST_PORT 0x80U
#define M61_DISPLAY_CONTROL_VERSION 2U
#define M61_DISPLAY_MANAGER 11U
#define M61_DISPLAY_PRESENT 16U
#define M61_DISPLAY_REPLY 18U
#define M61_DISPLAY_STATUS_OK 0U
#define M61_DISPLAY_MESSAGE_SIZE 56U

enum m61_wm_display_post_code {
    M61_WM_DISPLAY_CONNECT_OK = 0x9a,
    M61_WM_MANAGER_REQUEST_SENT = 0x9b,
    M61_WM_MANAGER_REPLY_OK = 0x9c,
    M61_WM_DISPLAY_PRESENT_SENT = 0x9d,
    M61_DISPLAY_PRESENT_RECEIVED = 0x9e,
    M61_WM_MANAGER_REPLY_REJECTED = 0x9f,
    M61_DISPLAY_WM_ACCEPTED = 0xc4,
    M61_DISPLAY_MANAGER_RECEIVED = 0xc5,
    M61_DISPLAY_MANAGER_PEER_OK = 0xc6,
    M61_DISPLAY_MANAGER_AUTH_OK = 0xc7,
    M61_DISPLAY_MANAGER_REPLY_SENT = 0xc8
};

static uint64_t m61_wm_pid;
static uint64_t m61_display_pid;
static uint32_t m61_wm_display_endpoint;
static uint32_t m61_display_wm_endpoint;
static uint32_t m61_display_wm_probe;
static bool m61_connect_posted;
static bool m61_manager_request_posted;
static bool m61_manager_reply_posted;
static bool m61_manager_bound;
static bool m61_present_sent_posted;
static bool m61_present_received_posted;
static bool m61_display_accept_posted;
static bool m61_display_manager_received_posted;
static bool m61_display_manager_peer_posted;
static bool m61_display_manager_auth_posted;
static bool m61_display_manager_reply_posted;

void boring_m61_note_event_query(struct process *process, uint32_t handle,
                                 long result, uint64_t peer_pid);

static bool m61_same_bytes(const char *value, size_t value_length,
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

static bool m61_process_name_ends_with(const struct process *process,
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

static uint32_t m61_read_u32(const uint8_t *value) {
    return (uint32_t)value[0] |
           ((uint32_t)value[1] << 8U) |
           ((uint32_t)value[2] << 16U) |
           ((uint32_t)value[3] << 24U);
}

static bool m61_display_message(const uint8_t *payload, size_t length,
                                uint32_t type) {
    return (payload != NULL) && (length == M61_DISPLAY_MESSAGE_SIZE) &&
           (m61_read_u32(payload) == M61_DISPLAY_CONTROL_VERSION) &&
           (m61_read_u32(payload + 4U) == type);
}

static void m61_wm_display_post(uint8_t code) {
    x86_64_out8((uint16_t)M61_WM_DISPLAY_POST_PORT, code);
}

static void m61_note_display_connect(struct process *process,
                                     const char *name, size_t length,
                                     enum boring_ipc_result result,
                                     uint32_t handle) {
    if ((result == BORING_IPC_RESULT_OK) &&
        (handle != BORING_IPC_HANDLE_INVALID) &&
        m61_process_name_ends_with(process, "boringwm") &&
        m61_same_bytes(name, length, "boring.display", 14U)) {
        m61_wm_pid = process->pid;
        m61_wm_display_endpoint = handle;
        if (!m61_connect_posted) {
            m61_connect_posted = true;
            m61_wm_display_post((uint8_t)M61_WM_DISPLAY_CONNECT_OK);
        }
    }
    if ((result == BORING_IPC_RESULT_OK) &&
        m61_display_manager_received_posted &&
        !m61_display_manager_peer_posted &&
        (handle != BORING_IPC_HANDLE_INVALID) &&
        (process != NULL) && (process->pid == m61_display_pid) &&
        m61_same_bytes(name, length, "boring.wm", 9U)) {
        m61_display_wm_probe = handle;
        m61_display_manager_peer_posted = true;
        m61_wm_display_post((uint8_t)M61_DISPLAY_MANAGER_PEER_OK);
    }
}

static void m61_note_service_accept(struct process *process,
                                    enum boring_ipc_result result,
                                    uint32_t endpoint) {
    uint32_t events = 0U;
    uint64_t peer_pid = 0ULL;

    if ((result != BORING_IPC_RESULT_OK) || m61_display_accept_posted ||
        !m61_manager_request_posted || (process == NULL) ||
        !m61_process_name_ends_with(process, "boring-display") ||
        (boring_ipc_poll(process, endpoint, &events, &peer_pid) !=
         BORING_IPC_RESULT_OK) || (peer_pid != m61_wm_pid)) {
        return;
    }
    m61_display_pid = process->pid;
    m61_display_wm_endpoint = endpoint;
    m61_display_accept_posted = true;
    m61_wm_display_post((uint8_t)M61_DISPLAY_WM_ACCEPTED);
}

static void m61_note_ipc_send(struct process *process, uint32_t endpoint,
                              const uint8_t *payload, size_t length,
                              enum boring_ipc_result result) {
    if ((result != BORING_IPC_RESULT_OK) || (process == NULL)) {
        return;
    }
    if ((process->pid == m61_wm_pid) &&
        (endpoint == m61_wm_display_endpoint)) {
        if (!m61_manager_request_posted &&
            m61_display_message(payload, length, M61_DISPLAY_MANAGER)) {
            m61_manager_request_posted = true;
            m61_wm_display_post((uint8_t)M61_WM_MANAGER_REQUEST_SENT);
        } else if (m61_manager_bound && !m61_present_sent_posted &&
                   m61_display_message(payload, length, M61_DISPLAY_PRESENT)) {
            m61_present_sent_posted = true;
            m61_wm_display_post((uint8_t)M61_WM_DISPLAY_PRESENT_SENT);
        }
    } else if ((process->pid == m61_display_pid) &&
               (endpoint == m61_display_wm_endpoint) &&
               m61_display_manager_auth_posted &&
               !m61_display_manager_reply_posted &&
               m61_display_message(payload, length, M61_DISPLAY_REPLY)) {
        m61_display_manager_reply_posted = true;
        m61_wm_display_post((uint8_t)M61_DISPLAY_MANAGER_REPLY_SENT);
    }
}

static void m61_note_ipc_receive(struct process *process, uint32_t endpoint,
                                 const uint8_t *payload, size_t length) {
    if ((process != NULL) && m61_display_accept_posted &&
        !m61_display_manager_received_posted &&
        (process->pid == m61_display_pid) &&
        (endpoint == m61_display_wm_endpoint) &&
        m61_display_message(payload, length, M61_DISPLAY_MANAGER)) {
        m61_display_manager_received_posted = true;
        m61_wm_display_post((uint8_t)M61_DISPLAY_MANAGER_RECEIVED);
    }

    if ((process != NULL) && m61_manager_request_posted &&
        (process->pid == m61_wm_pid) &&
        (endpoint == m61_wm_display_endpoint) &&
        !m61_manager_reply_posted && (payload != NULL) &&
        (length == M61_DISPLAY_MESSAGE_SIZE) &&
        (m61_read_u32(payload + 4U) == M61_DISPLAY_REPLY)) {
        m61_manager_reply_posted = true;
        if ((m61_read_u32(payload) == M61_DISPLAY_CONTROL_VERSION) &&
            (m61_read_u32(payload + 8U) == M61_DISPLAY_STATUS_OK)) {
            m61_manager_bound = true;
            m61_wm_display_post((uint8_t)M61_WM_MANAGER_REPLY_OK);
        } else {
            m61_wm_display_post((uint8_t)M61_WM_MANAGER_REPLY_REJECTED);
        }
    }

    if (!m61_present_received_posted &&
        m61_process_name_ends_with(process, "boring-display") &&
        m61_display_message(payload, length, M61_DISPLAY_PRESENT)) {
        m61_present_received_posted = true;
        m61_wm_display_post((uint8_t)M61_DISPLAY_PRESENT_RECEIVED);
    }
}

void boring_m61_note_event_query(struct process *process, uint32_t handle,
                                 long result, uint64_t peer_pid) {
    if ((result >= 0L) && (process != NULL) &&
        m61_display_manager_peer_posted &&
        !m61_display_manager_auth_posted &&
        (process->pid == m61_display_pid) &&
        (handle == m61_display_wm_probe) && (peer_pid == m61_wm_pid)) {
        m61_display_manager_auth_posted = true;
        m61_wm_display_post((uint8_t)M61_DISPLAY_MANAGER_AUTH_OK);
    }
}
#endif

extern uint8_t x86_64_syscall_stack[X86_64_SYSCALL_STACK_SIZE];

uintptr_t x86_64_syscall_active_stack_top =
    (uintptr_t)&x86_64_syscall_stack[X86_64_SYSCALL_STACK_SIZE];

static void ipc_syscall_fatal(const char *reason) __attribute__((noreturn));
static void ipc_syscall_fatal(const char *reason) {
    serial_write_string("BoringKernel M33 syscall fatal: ");
    serial_write_string(reason);
    serial_write_string("\n");
    x86_64_halt_forever();
}

void boring_ipc_syscall_use_task_stack(uintptr_t stack_top) {
    x86_64_syscall_active_stack_top = stack_top;
}

void boring_ipc_syscall_use_bootstrap_stack(void) {
    x86_64_syscall_active_stack_top =
        (uintptr_t)&x86_64_syscall_stack[X86_64_SYSCALL_STACK_SIZE];
}

static uint64_t ipc_error(int error_number) {
    return (uint64_t)(-(int64_t)error_number);
}

static int ipc_result_errno(enum boring_ipc_result result) {
    switch (result) {
        case BORING_IPC_RESULT_OK:
            return 0;
        case BORING_IPC_RESULT_INVALID:
            return BORING_SYSCALL_EINVAL;
        case BORING_IPC_RESULT_NOT_FOUND:
            return BORING_SYSCALL_ENOENT;
        case BORING_IPC_RESULT_EXISTS:
            return BORING_SYSCALL_EEXIST;
        case BORING_IPC_RESULT_NO_SPACE:
            return BORING_SYSCALL_ENOSPC;
        case BORING_IPC_RESULT_PEER_CLOSED:
            return BORING_SYSCALL_EPIPE;
        case BORING_IPC_RESULT_WOULD_BLOCK:
            return BORING_SYSCALL_EBUSY;
        case BORING_IPC_RESULT_INTERNAL:
        default:
            return BORING_SYSCALL_EIO;
    }
}

static bool user_range_accessible(uintptr_t user_address,
                                  size_t length,
                                  bool require_writable) {
    struct process *process;
    uintptr_t current;
    size_t remaining;

    if (!ring3_user_range_valid(user_address, length)) {
        return false;
    }
    process = process_current();
    if ((process == NULL) || !process_is_alive(process) ||
        process->address_space.bootstrap) {
        return false;
    }
    current = user_address;
    remaining = length;
    while (remaining != 0U) {
        uint64_t physical;
        const size_t page_offset =
            (size_t)((uint64_t)current & (VMM_PAGE_SIZE - 1ULL));
        size_t chunk = (size_t)VMM_PAGE_SIZE - page_offset;

        if (chunk > remaining) {
            chunk = remaining;
        }
        if (!ring3_user_translate(&process->address_space, current,
                                  require_writable, &physical)) {
            return false;
        }
        current += (uintptr_t)chunk;
        remaining -= chunk;
    }
    return true;
}

static bool copy_from_user(void *destination,
                           uintptr_t user_address,
                           size_t length) {
    struct process *process;
    struct vmm_stats stats;
    uint8_t *output = (uint8_t *)destination;
    uintptr_t current = user_address;
    size_t remaining = length;

    if ((destination == NULL) ||
        !ring3_user_range_valid(user_address, length) ||
        !vmm_get_stats(&stats)) {
        return false;
    }
    process = process_current();
    if ((process == NULL) || !process_is_alive(process) ||
        process->address_space.bootstrap) {
        return false;
    }
    while (remaining != 0U) {
        uint64_t physical;
        uint64_t kernel_virtual;
        const size_t page_offset =
            (size_t)((uint64_t)current & (VMM_PAGE_SIZE - 1ULL));
        size_t chunk = (size_t)VMM_PAGE_SIZE - page_offset;
        const uint8_t *source;
        size_t index;

        if (chunk > remaining) {
            chunk = remaining;
        }
        if (!ring3_user_translate(&process->address_space, current, false,
                                  &physical) ||
            (physical > UINT64_MAX - stats.hhdm_offset)) {
            return false;
        }
        kernel_virtual = stats.hhdm_offset + physical;
        if ((kernel_virtual >> 48U) != 0xffffULL) {
            return false;
        }
        source = (const uint8_t *)(uintptr_t)kernel_virtual;
        for (index = 0U; index < chunk; ++index) {
            output[index] = source[index];
        }
        output += chunk;
        current += (uintptr_t)chunk;
        remaining -= chunk;
    }
    return true;
}

static bool copy_to_user(uintptr_t user_address,
                         const void *source_buffer,
                         size_t length) {
    struct process *process;
    struct vmm_stats stats;
    const uint8_t *input = (const uint8_t *)source_buffer;
    uintptr_t current = user_address;
    size_t remaining = length;

    if ((source_buffer == NULL) ||
        !ring3_user_range_valid(user_address, length) ||
        !vmm_get_stats(&stats)) {
        return false;
    }
    process = process_current();
    if ((process == NULL) || !process_is_alive(process) ||
        process->address_space.bootstrap) {
        return false;
    }
    while (remaining != 0U) {
        uint64_t physical;
        uint64_t kernel_virtual;
        const size_t page_offset =
            (size_t)((uint64_t)current & (VMM_PAGE_SIZE - 1ULL));
        size_t chunk = (size_t)VMM_PAGE_SIZE - page_offset;
        uint8_t *destination;
        size_t index;

        if (chunk > remaining) {
            chunk = remaining;
        }
        if (!ring3_user_translate(&process->address_space, current, true,
                                  &physical) ||
            (physical > UINT64_MAX - stats.hhdm_offset)) {
            return false;
        }
        kernel_virtual = stats.hhdm_offset + physical;
        if ((kernel_virtual >> 48U) != 0xffffULL) {
            return false;
        }
        destination = (uint8_t *)(uintptr_t)kernel_virtual;
        for (index = 0U; index < chunk; ++index) {
            destination[index] = input[index];
        }
        input += chunk;
        current += (uintptr_t)chunk;
        remaining -= chunk;
    }
    return true;
}

static uint64_t service_register(uint64_t user_name, uint64_t raw_length) {
    char name[BORING_IPC_SERVICE_NAME_MAX + 1U];
    struct process *const process = process_current();
    uint32_t handle = BORING_IPC_HANDLE_INVALID;
    enum boring_ipc_result result;
    size_t length;

    if ((raw_length == 0ULL) ||
        (raw_length > (uint64_t)BORING_IPC_SERVICE_NAME_MAX) ||
        (raw_length > (uint64_t)SIZE_MAX)) {
        return ipc_error(BORING_SYSCALL_EINVAL);
    }
    length = (size_t)raw_length;
    if (!user_range_accessible((uintptr_t)user_name, length, false) ||
        !copy_from_user(name, (uintptr_t)user_name, length)) {
        return ipc_error(BORING_SYSCALL_EFAULT);
    }
    name[length] = '\0';
    result = boring_ipc_service_register(process, name, length, &handle);
    return (result == BORING_IPC_RESULT_OK) ? (uint64_t)handle :
        ipc_error(ipc_result_errno(result));
}

static uint64_t service_connect(uint64_t user_name, uint64_t raw_length) {
    char name[BORING_IPC_SERVICE_NAME_MAX + 1U];
    struct process *const process = process_current();
    uint32_t handle = BORING_IPC_HANDLE_INVALID;
    enum boring_ipc_result result;
    size_t length;

    if ((raw_length == 0ULL) ||
        (raw_length > (uint64_t)BORING_IPC_SERVICE_NAME_MAX) ||
        (raw_length > (uint64_t)SIZE_MAX)) {
        return ipc_error(BORING_SYSCALL_EINVAL);
    }
    length = (size_t)raw_length;
    if (!user_range_accessible((uintptr_t)user_name, length, false) ||
        !copy_from_user(name, (uintptr_t)user_name, length)) {
        return ipc_error(BORING_SYSCALL_EFAULT);
    }
    name[length] = '\0';
    result = boring_ipc_service_connect(process, name, length, &handle);
#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
    m61_note_display_connect(process, name, length, result, handle);
#endif
    return (result == BORING_IPC_RESULT_OK) ? (uint64_t)handle :
        ipc_error(ipc_result_errno(result));
}

static uint64_t service_accept(uint64_t raw_listener) {
    struct process *const process = process_current();
    enum boring_ipc_result result;
    uint32_t endpoint = BORING_IPC_HANDLE_INVALID;

    if (raw_listener > (uint64_t)UINT32_MAX) {
        return ipc_error(BORING_SYSCALL_EINVAL);
    }
    for (;;) {
        result = boring_ipc_service_accept(process, (uint32_t)raw_listener,
                                           &endpoint);
        if (result == BORING_IPC_RESULT_OK) {
#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
            m61_note_service_accept(process, result, endpoint);
#endif
            return (uint64_t)endpoint;
        }
        if (result != BORING_IPC_RESULT_WOULD_BLOCK) {
            return ipc_error(ipc_result_errno(result));
        }
        if (!task_block_current()) {
            return ipc_error(BORING_SYSCALL_EIO);
        }
    }
}

static uint64_t ipc_send(uint64_t raw_endpoint,
                         uint64_t user_payload,
                         uint64_t raw_length,
                         uint64_t raw_buffer_handle) {
    uint8_t payload[BORING_IPC_INLINE_PAYLOAD_MAX];
    struct process *const process = process_current();
    enum boring_ipc_result result;
    size_t length;

    if ((raw_endpoint > (uint64_t)UINT32_MAX) ||
        (raw_buffer_handle > (uint64_t)UINT32_MAX) ||
        (raw_length > (uint64_t)BORING_IPC_INLINE_PAYLOAD_MAX) ||
        (raw_length > (uint64_t)SIZE_MAX)) {
        return ipc_error(BORING_SYSCALL_EINVAL);
    }
    length = (size_t)raw_length;
    if ((length != 0U) &&
        (!user_range_accessible((uintptr_t)user_payload, length, false) ||
         !copy_from_user(payload, (uintptr_t)user_payload, length))) {
        return ipc_error(BORING_SYSCALL_EFAULT);
    }
    result = boring_ipc_send(process, (uint32_t)raw_endpoint,
                             payload, length, (uint32_t)raw_buffer_handle);
#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
    m61_note_ipc_send(process, (uint32_t)raw_endpoint, payload, length, result);
#endif
    return (result == BORING_IPC_RESULT_OK) ? 0ULL :
        ipc_error(ipc_result_errno(result));
}

static uint64_t ipc_receive(uint64_t raw_endpoint,
                            uint64_t user_payload,
                            uint64_t raw_capacity,
                            uint64_t user_result) {
    uint8_t payload[BORING_IPC_INLINE_PAYLOAD_MAX];
    struct boring_ipc_receive_kernel_result kernel_result;
    struct boring_ipc_receive_result abi_result;
    struct process *const process = process_current();
    enum boring_ipc_result result;
    size_t capacity;

    if ((raw_endpoint > (uint64_t)UINT32_MAX) ||
        (raw_capacity > (uint64_t)BORING_IPC_INLINE_PAYLOAD_MAX) ||
        (raw_capacity > (uint64_t)SIZE_MAX)) {
        return ipc_error(BORING_SYSCALL_EINVAL);
    }
    capacity = (size_t)raw_capacity;
    if (!user_range_accessible((uintptr_t)user_result,
                               sizeof(abi_result), true) ||
        ((capacity != 0U) &&
         !user_range_accessible((uintptr_t)user_payload, capacity, true))) {
        return ipc_error(BORING_SYSCALL_EFAULT);
    }

    for (;;) {
        kernel_result.payload_length = 0U;
        kernel_result.buffer_handle = BORING_BUFFER_HANDLE_INVALID;
        result = boring_ipc_receive(process, (uint32_t)raw_endpoint,
                                    payload, capacity, &kernel_result);
        if (result == BORING_IPC_RESULT_WOULD_BLOCK) {
            if (!task_block_current()) {
                return ipc_error(BORING_SYSCALL_EIO);
            }
            continue;
        }
        if (result != BORING_IPC_RESULT_OK) {
            return ipc_error(ipc_result_errno(result));
        }
        break;
    }

#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
    m61_note_ipc_receive(process, (uint32_t)raw_endpoint, payload,
                         kernel_result.payload_length);
#endif
    abi_result.payload_length = (uint64_t)kernel_result.payload_length;
    abi_result.buffer_handle = kernel_result.buffer_handle;
    abi_result.flags = BORING_IPC_RECEIVE_FLAGS_NONE;
    if ((kernel_result.payload_length != 0U) &&
        !copy_to_user((uintptr_t)user_payload, payload,
                      kernel_result.payload_length)) {
        ipc_syscall_fatal("prevalidated IPC payload copy failed");
    }
    if (!copy_to_user((uintptr_t)user_result, &abi_result,
                      sizeof(abi_result))) {
        ipc_syscall_fatal("prevalidated IPC result copy failed");
    }
    return 0ULL;
}

static uint64_t ipc_close(uint64_t raw_handle) {
    struct process *const process = process_current();
    enum boring_ipc_result result;

    if (raw_handle > (uint64_t)UINT32_MAX) {
        return ipc_error(BORING_SYSCALL_EINVAL);
    }
    result = boring_ipc_close(process, (uint32_t)raw_handle);
    return (result == BORING_IPC_RESULT_OK) ? 0ULL :
        ipc_error(ipc_result_errno(result));
}

static bool is_m33_syscall(uint64_t number) {
    return (number >= (uint64_t)BORING_SYS_SERVICE_REGISTER) &&
           (number <= (uint64_t)BORING_SYS_IPC_CLOSE);
}

void x86_64_syscall_dispatch_m33(struct x86_64_syscall_frame *frame) {
    uint64_t result;
    const uint64_t number = (frame != NULL) ? frame->syscall_number : UINT64_MAX;

    if ((frame != NULL) && (number == (uint64_t)BORING_SYS_EXIT) &&
        boring_ipc_test_process_exit_prepare(process_current())) {
        struct process *const process = process_current();
        struct user_memory_cleanup_stats cleanup;

        boring_ipc_process_cleanup(process);
        if (user_memory_process_cleanup(process, &cleanup) !=
            USER_MEMORY_RESULT_OK) {
            ipc_syscall_fatal("M33 process memory cleanup failed");
        }
        serial_write_string("boring-ipc: process cleanup pid ");
        serial_write_u64(process->pid);
        serial_write_string("\n");
        task_exit_current_process();
    }

    if (!is_m33_syscall(number)) {
        x86_64_syscall_dispatch(frame);
        return;
    }

    /*
     * Run the established dispatcher first. Slots 31..36 are unknown to M32,
     * so it performs the existing trusted-stack checks, accounting and
     * SYSRET-state validation while returning ENOSYS. M33 then replaces only
     * the result for its frozen extension slots.
     */
    x86_64_syscall_dispatch(frame);

    switch (number) {
        case BORING_SYS_SERVICE_REGISTER:
            result = service_register(frame->rdi, frame->rsi);
            break;
        case BORING_SYS_SERVICE_CONNECT:
            result = service_connect(frame->rdi, frame->rsi);
            break;
        case BORING_SYS_SERVICE_ACCEPT:
            result = service_accept(frame->rdi);
            break;
        case BORING_SYS_IPC_SEND:
            result = ipc_send(frame->rdi, frame->rsi, frame->rdx,
                              frame->r10);
            break;
        case BORING_SYS_IPC_RECEIVE:
            result = ipc_receive(frame->rdi, frame->rsi, frame->rdx,
                                 frame->r10);
            break;
        case BORING_SYS_IPC_CLOSE:
            result = ipc_close(frame->rdi);
            break;
        default:
            result = ipc_error(BORING_SYSCALL_ENOSYS);
            break;
    }
    frame->result = result;
}
