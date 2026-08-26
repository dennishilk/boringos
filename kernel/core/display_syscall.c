#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/cpu.h>
#include <boring/display_abi.h>
#include <boring/display_syscall.h>
#include <boring/display_test.h>
#include <boring/framebuffer_user.h>
#include <boring/input.h>
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

static void display_syscall_fatal(const char *reason) __attribute__((noreturn));
static void display_syscall_fatal(const char *reason) {
    serial_write_string("BoringKernel M34 syscall fatal: ");
    serial_write_string(reason);
    serial_write_string("\n");
    x86_64_halt_forever();
}

static uint64_t display_error(int error_number) {
    return (uint64_t)(-(int64_t)error_number);
}

static int memory_result_errno(enum user_memory_result result) {
    switch (result) {
        case USER_MEMORY_RESULT_OK:
            return 0;
        case USER_MEMORY_RESULT_INVALID:
            return BORING_SYSCALL_EINVAL;
        case USER_MEMORY_RESULT_NO_SPACE:
            return BORING_SYSCALL_ENOSPC;
        case USER_MEMORY_RESULT_NO_MEMORY:
            return BORING_SYSCALL_ENOMEM;
        case USER_MEMORY_RESULT_INTERNAL:
        case USER_MEMORY_RESULT_NOT_INITIALIZED:
        default:
            return BORING_SYSCALL_EIO;
    }
}

static int framebuffer_result_errno(enum boring_framebuffer_user_result result) {
    switch (result) {
        case BORING_FRAMEBUFFER_USER_OK:
            return 0;
        case BORING_FRAMEBUFFER_USER_INVALID:
            return BORING_SYSCALL_EINVAL;
        case BORING_FRAMEBUFFER_USER_BUSY:
            return BORING_SYSCALL_EBUSY;
        case BORING_FRAMEBUFFER_USER_ACCESS:
            return BORING_SYSCALL_EACCES;
        case BORING_FRAMEBUFFER_USER_UNAVAILABLE:
            return BORING_SYSCALL_ENOTSUP;
        case BORING_FRAMEBUFFER_USER_INTERNAL:
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

static uint64_t buffer_info(uint64_t raw_handle) {
    struct process *const process = process_current();
    enum user_memory_result result;
    uint64_t size = 0ULL;

    if ((raw_handle > (uint64_t)UINT32_MAX) || (process == NULL) ||
        !process_is_alive(process)) {
        return display_error(BORING_SYSCALL_EINVAL);
    }
    result = user_buffer_size(process, (uint32_t)raw_handle, &size);
    return (result == USER_MEMORY_RESULT_OK) ? size :
        display_error(memory_result_errno(result));
}

static uint64_t framebuffer_claim(uint64_t user_info) {
    struct process *const process = process_current();
    struct boring_display_scanout_info info;
    enum boring_framebuffer_user_result result;

    if ((process == NULL) || !process_is_alive(process)) {
        return display_error(BORING_SYSCALL_EINVAL);
    }
    if (!user_range_accessible((uintptr_t)user_info, sizeof(info), true)) {
        return display_error(BORING_SYSCALL_EFAULT);
    }
    result = boring_framebuffer_user_claim(process->pid, &info);
    if (result != BORING_FRAMEBUFFER_USER_OK) {
        return display_error(framebuffer_result_errno(result));
    }
    if (!copy_to_user((uintptr_t)user_info, &info, sizeof(info))) {
        (void)boring_framebuffer_user_release(process->pid);
        return display_error(BORING_SYSCALL_EFAULT);
    }
    return 0ULL;
}

static uint64_t framebuffer_present(uint64_t raw_handle) {
    struct process *const process = process_current();
    enum boring_framebuffer_user_result result;

    if ((raw_handle > (uint64_t)UINT32_MAX) || (process == NULL) ||
        !process_is_alive(process)) {
        return display_error(BORING_SYSCALL_EINVAL);
    }
    result = boring_framebuffer_user_present(process, (uint32_t)raw_handle);
    return (result == BORING_FRAMEBUFFER_USER_OK) ? 0ULL :
        display_error(framebuffer_result_errno(result));
}

static uint64_t framebuffer_release(void) {
    struct process *const process = process_current();
    enum boring_framebuffer_user_result result;

    if ((process == NULL) || !process_is_alive(process)) {
        return display_error(BORING_SYSCALL_EINVAL);
    }
    result = boring_framebuffer_user_release(process->pid);
    return (result == BORING_FRAMEBUFFER_USER_OK) ? 0ULL :
        display_error(framebuffer_result_errno(result));
}

static bool is_m34_syscall(uint64_t number) {
    return (number >= (uint64_t)BORING_SYS_BUFFER_INFO) &&
           (number <= (uint64_t)BORING_SYS_FRAMEBUFFER_RELEASE);
}

static bool display_test_exit(struct x86_64_syscall_frame *frame) {
    struct process *process;
    struct user_memory_cleanup_stats cleanup;
    bool framebuffer_released = false;
    bool input_released = false;

    if ((frame == NULL) ||
        (frame->syscall_number != (uint64_t)BORING_SYS_EXIT)) {
        return false;
    }
    process = process_current();
    if ((process == NULL) || !boring_display_test_process_exit_prepare(process)) {
        return false;
    }

    if (!boring_framebuffer_user_process_teardown(process->pid,
                                                   &framebuffer_released) ||
        !boring_input_process_teardown(process->pid, &input_released)) {
        display_syscall_fatal("M34 device claim cleanup failed");
    }
    boring_ipc_process_cleanup(process);
    if (user_memory_process_cleanup(process, &cleanup) !=
        USER_MEMORY_RESULT_OK) {
        display_syscall_fatal("M34 process memory cleanup failed");
    }

    serial_write_string("boring-display: process cleanup pid ");
    serial_write_u64(process->pid);
    serial_write_string(" framebuffer=");
    serial_write_u64(framebuffer_released ? 1ULL : 0ULL);
    serial_write_string(" input=");
    serial_write_u64(input_released ? 1ULL : 0ULL);
    serial_write_string(" objects ");
    serial_write_u64((uint64_t)cleanup.objects_before);
    serial_write_string("->");
    serial_write_u64((uint64_t)cleanup.objects_after);
    serial_write_string("\n");

    task_exit_current_process();
}

static void normal_exit_framebuffer_cleanup(struct x86_64_syscall_frame *frame) {
    struct process *process;
    bool released = false;

    if ((frame == NULL) ||
        (frame->syscall_number != (uint64_t)BORING_SYS_EXIT)) {
        return;
    }
    process = process_current();
    if ((process == NULL) || !process_is_alive(process)) {
        return;
    }
    if (!boring_framebuffer_user_process_teardown(process->pid, &released)) {
        display_syscall_fatal("normal process framebuffer cleanup failed");
    }
    if (released) {
        serial_write_string("boring-framebuffer: owner pid ");
        serial_write_u64(process->pid);
        serial_write_string(" teardown released\n");
    }
}

void x86_64_syscall_dispatch_m34(struct x86_64_syscall_frame *frame) {
    uint64_t result;
    const uint64_t number = (frame != NULL) ? frame->syscall_number : UINT64_MAX;

    if (display_test_exit(frame)) {
        display_syscall_fatal("M34 process exit returned");
    }
    normal_exit_framebuffer_cleanup(frame);
    if (!is_m34_syscall(number)) {
        x86_64_syscall_dispatch_m33(frame);
        return;
    }

    /*
     * M33 delegates 37..40 to the established base dispatcher, which keeps
     * the trusted-stack accounting and SYSRET validation unchanged and
     * returns ENOSYS. M34 replaces only the result for its four new slots.
     */
    x86_64_syscall_dispatch_m33(frame);

    switch (number) {
        case BORING_SYS_BUFFER_INFO:
            result = buffer_info(frame->rdi);
            break;
        case BORING_SYS_FRAMEBUFFER_CLAIM:
            result = framebuffer_claim(frame->rdi);
            break;
        case BORING_SYS_FRAMEBUFFER_PRESENT:
            result = framebuffer_present(frame->rdi);
            break;
        case BORING_SYS_FRAMEBUFFER_RELEASE:
            result = framebuffer_release();
            break;
        default:
            result = display_error(BORING_SYSCALL_ENOSYS);
            break;
    }
    frame->result = result;
}
