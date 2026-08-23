#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/cpu.h>
#include <boring/descriptor.h>
#include <boring/process.h>
#include <boring/ring3_memory.h>
#include <boring/serial.h>
#include <boring/syscall.h>
#include <boring/vmm.h>

#define IA32_EFER 0xc0000080U
#define IA32_STAR 0xc0000081U
#define IA32_LSTAR 0xc0000082U
#define IA32_FMASK 0xc0000084U
#define EFER_SCE (1ULL << 0)

#define RFLAGS_CF (1ULL << 0)
#define RFLAGS_RESERVED1 (1ULL << 1)
#define RFLAGS_PF (1ULL << 2)
#define RFLAGS_AF (1ULL << 4)
#define RFLAGS_ZF (1ULL << 6)
#define RFLAGS_SF (1ULL << 7)
#define RFLAGS_TF (1ULL << 8)
#define RFLAGS_IF (1ULL << 9)
#define RFLAGS_DF (1ULL << 10)
#define RFLAGS_OF (1ULL << 11)
#define RFLAGS_IOPL (3ULL << 12)
#define RFLAGS_NT (1ULL << 14)
#define RFLAGS_VM (1ULL << 17)
#define RFLAGS_AC (1ULL << 18)
#define SYSCALL_FMASK_VALUE (RFLAGS_TF | RFLAGS_IF | RFLAGS_DF | RFLAGS_NT | RFLAGS_AC)
#define SYSRET_ALLOWED_STATUS_FLAGS (RFLAGS_CF | RFLAGS_PF | RFLAGS_AF | RFLAGS_ZF | RFLAGS_SF | RFLAGS_OF)

#define STAR_KERNEL_SELECTOR ((uint64_t)(X86_64_GDT_KERNEL_CODE_SELECTOR & 0xfffcU))
#define STAR_USER_DATA_BASE ((uint64_t)(X86_64_GDT_USER_DATA_SELECTOR & 0xfffcU))
#define STAR_SYSRET_BASE (STAR_USER_DATA_BASE - 8ULL)
#define STAR_VALUE ((STAR_SYSRET_BASE << 48U) | (STAR_KERNEL_SELECTOR << 32U))

_Static_assert(sizeof(struct x86_64_syscall_frame) == 144U,
               "syscall frame must match x86_64 entry assembly");
_Static_assert(offsetof(struct x86_64_syscall_frame, user_rsp) == 0U,
               "syscall user RSP offset mismatch");
_Static_assert(offsetof(struct x86_64_syscall_frame, user_rip) == 8U,
               "syscall user RIP offset mismatch");
_Static_assert(offsetof(struct x86_64_syscall_frame, user_rflags) == 16U,
               "syscall user RFLAGS offset mismatch");
_Static_assert(offsetof(struct x86_64_syscall_frame, syscall_number) == 24U,
               "syscall number offset mismatch");
_Static_assert(offsetof(struct x86_64_syscall_frame, result) == 128U,
               "syscall result offset mismatch");
_Static_assert((X86_64_GDT_KERNEL_DATA_SELECTOR ==
                (X86_64_GDT_KERNEL_CODE_SELECTOR + 8U)),
               "SYSCALL kernel SS must follow kernel CS");
_Static_assert((((STAR_SYSRET_BASE + 8ULL) | 3ULL) ==
                (uint64_t)X86_64_GDT_USER_DATA_SELECTOR),
               "SYSRET user SS relationship mismatch");
_Static_assert((((STAR_SYSRET_BASE + 16ULL) | 3ULL) ==
                (uint64_t)X86_64_GDT_USER_CODE_SELECTOR),
               "SYSRET user CS relationship mismatch");

extern void x86_64_syscall_entry(void);

uint8_t x86_64_syscall_stack[X86_64_SYSCALL_STACK_SIZE]
    __attribute__((aligned(16)));
uint64_t x86_64_syscall_user_rsp_scratch __attribute__((aligned(8)));

static struct syscall_stats syscall_state;
static bool syscall_initialized;

static void syscall_fatal(const char *reason) __attribute__((noreturn));
static void syscall_fatal(const char *reason) {
    serial_write_string("BoringKernel syscall fatal: ");
    serial_write_string(reason);
    serial_write_string("\n");
    x86_64_halt_forever();
}

static uint64_t syscall_error(int error_number) {
    return (uint64_t)(-(int64_t)error_number);
}

bool syscall_stack_contains(uintptr_t stack_pointer) {
    const uintptr_t base = (uintptr_t)&x86_64_syscall_stack[0];
    const uintptr_t top = base + (uintptr_t)X86_64_SYSCALL_STACK_SIZE;

    return (stack_pointer >= base) && (stack_pointer < top);
}

static uint64_t sanitize_user_rflags(uint64_t user_rflags) {
    return (user_rflags & SYSRET_ALLOWED_STATUS_FLAGS) | RFLAGS_RESERVED1;
}

static bool syscall_copy_from_user(void *destination,
                                   uintptr_t user_address,
                                   size_t length) {
    struct process *process;
    struct vmm_stats vmm_stats;
    uint8_t *output = (uint8_t *)destination;
    uintptr_t current;
    size_t remaining;

    if ((destination == NULL) || !ring3_user_range_valid(user_address, length) ||
        !vmm_get_stats(&vmm_stats)) {
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
        uint64_t kernel_virtual;
        const size_t page_offset =
            (size_t)((uint64_t)current & (VMM_PAGE_SIZE - 1ULL));
        size_t chunk = (size_t)VMM_PAGE_SIZE - page_offset;
        size_t index;
        const uint8_t *source;

        if (chunk > remaining) {
            chunk = remaining;
        }

        if (!ring3_user_translate(&process->address_space, current, false,
                                  &physical) ||
            (physical > UINT64_MAX - vmm_stats.hhdm_offset)) {
            return false;
        }

        kernel_virtual = vmm_stats.hhdm_offset + physical;
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

static uint64_t syscall_getpid(void) {
    const struct process *process = process_current();

    if ((process == NULL) || !process_is_alive(process)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }
    return process->pid;
}

static uint64_t syscall_debug_write(uint64_t user_buffer, uint64_t length) {
    char buffer[BORING_SYSCALL_DEBUG_WRITE_MAX + 1U];
    size_t safe_length;

    if ((length == 0ULL) ||
        (length > (uint64_t)BORING_SYSCALL_DEBUG_WRITE_MAX)) {
        return syscall_error(BORING_SYSCALL_EINVAL);
    }

    safe_length = (size_t)length;
    if (!syscall_copy_from_user(&buffer[0], (uintptr_t)user_buffer,
                                safe_length)) {
        return syscall_error(BORING_SYSCALL_EFAULT);
    }

    buffer[safe_length] = '\0';
    serial_write_string("Syscall DEBUG_WRITE: ");
    serial_write_string(&buffer[0]);
    serial_write_string("\n");
    return length;
}

static bool syscall_return_state_valid(struct x86_64_syscall_frame *frame) {
    struct process *process;
    struct descriptor_stats descriptors;
    uint64_t translated;

    if ((frame == NULL) || !descriptor_get_stats(&descriptors) ||
        (descriptors.kernel_code_selector !=
         (uint16_t)X86_64_GDT_KERNEL_CODE_SELECTOR) ||
        (descriptors.kernel_data_selector !=
         (uint16_t)X86_64_GDT_KERNEL_DATA_SELECTOR) ||
        (descriptors.user_code_selector !=
         (uint16_t)X86_64_GDT_USER_CODE_SELECTOR) ||
        (descriptors.user_data_selector !=
         (uint16_t)X86_64_GDT_USER_DATA_SELECTOR) ||
        !ring3_user_range_valid((uintptr_t)frame->user_rip, 1U) ||
        (frame->user_rsp == 0ULL) ||
        !ring3_user_range_valid((uintptr_t)(frame->user_rsp - 1ULL), 1U)) {
        return false;
    }

    process = process_current();
    if ((process == NULL) || !process_is_alive(process) ||
        process->address_space.bootstrap ||
        !ring3_user_translate(&process->address_space,
                              (uintptr_t)frame->user_rip, false,
                              &translated) ||
        !ring3_user_translate(&process->address_space,
                              (uintptr_t)(frame->user_rsp - 1ULL), true,
                              &translated)) {
        return false;
    }

    frame->user_rflags = sanitize_user_rflags(frame->user_rflags);
    return ((frame->user_rflags & (RFLAGS_IOPL | RFLAGS_NT | RFLAGS_VM |
                                   RFLAGS_TF | RFLAGS_IF | RFLAGS_DF |
                                   RFLAGS_AC)) == 0ULL) &&
           ((frame->user_rflags & RFLAGS_RESERVED1) != 0ULL);
}

bool syscall_init(void) {
    struct descriptor_stats descriptors;
    const uintptr_t stack_base = (uintptr_t)&x86_64_syscall_stack[0];
    const uintptr_t stack_top =
        stack_base + (uintptr_t)X86_64_SYSCALL_STACK_SIZE;
    const uint64_t lstar = (uint64_t)(uintptr_t)&x86_64_syscall_entry;
    uint64_t efer;
    uint64_t active_efer;
    uint64_t active_star;
    uint64_t active_lstar;
    uint64_t active_fmask;

    if (syscall_initialized) {
        return true;
    }

    syscall_state.supported = false;
    syscall_state.initialized = false;
    syscall_state.dispatch_count = 0ULL;
    syscall_state.last_kernel_rsp = 0U;
    syscall_state.last_user_rsp = 0U;

    if (!x86_64_syscall_supported() || !descriptor_get_stats(&descriptors) ||
        (descriptors.kernel_code_selector !=
         (uint16_t)X86_64_GDT_KERNEL_CODE_SELECTOR) ||
        (descriptors.kernel_data_selector !=
         (uint16_t)X86_64_GDT_KERNEL_DATA_SELECTOR) ||
        (descriptors.user_code_selector !=
         (uint16_t)X86_64_GDT_USER_CODE_SELECTOR) ||
        (descriptors.user_data_selector !=
         (uint16_t)X86_64_GDT_USER_DATA_SELECTOR) ||
        ((stack_base & 0x0fU) != 0U) || ((stack_top & 0x0fU) != 0U) ||
        ((lstar >> 48U) != 0xffffULL)) {
        return false;
    }

    syscall_state.supported = true;
    efer = x86_64_read_msr(IA32_EFER);

    x86_64_write_msr(IA32_STAR, STAR_VALUE);
    x86_64_write_msr(IA32_LSTAR, lstar);
    x86_64_write_msr(IA32_FMASK, SYSCALL_FMASK_VALUE);
    x86_64_write_msr(IA32_EFER, efer | EFER_SCE);

    active_efer = x86_64_read_msr(IA32_EFER);
    active_star = x86_64_read_msr(IA32_STAR);
    active_lstar = x86_64_read_msr(IA32_LSTAR);
    active_fmask = x86_64_read_msr(IA32_FMASK);

    if (((active_efer & EFER_SCE) == 0ULL) ||
        (active_star != STAR_VALUE) || (active_lstar != lstar) ||
        (active_fmask != SYSCALL_FMASK_VALUE) ||
        ((active_efer & ~EFER_SCE) != (efer & ~EFER_SCE))) {
        return false;
    }

    syscall_state.efer = active_efer;
    syscall_state.star = active_star;
    syscall_state.lstar = active_lstar;
    syscall_state.fmask = active_fmask;
    syscall_state.stack_base = stack_base;
    syscall_state.stack_top = stack_top;
    syscall_state.initialized = true;
    syscall_initialized = true;
    return true;
}

bool syscall_get_stats(struct syscall_stats *stats) {
    if ((stats == NULL) || !syscall_initialized) {
        return false;
    }

    *stats = syscall_state;
    return true;
}

void x86_64_syscall_dispatch(struct x86_64_syscall_frame *frame) {
    const uintptr_t live_rsp = x86_64_read_rsp();
    const uintptr_t frame_address = (uintptr_t)frame;
    uint64_t result;

    if (!syscall_initialized || (frame == NULL) ||
        !syscall_stack_contains(live_rsp) ||
        !syscall_stack_contains(frame_address) ||
        (frame_address > syscall_state.stack_top -
                         (uintptr_t)sizeof(*frame))) {
        syscall_fatal("invalid trusted entry stack");
    }

    syscall_state.last_kernel_rsp = live_rsp;
    syscall_state.last_user_rsp = (uintptr_t)frame->user_rsp;
    ++syscall_state.dispatch_count;

    switch (frame->syscall_number) {
        case BORING_SYS_GETPID:
            result = syscall_getpid();
            break;
        case BORING_SYS_DEBUG_WRITE:
            result = syscall_debug_write(frame->rdi, frame->rsi);
            break;
        default:
            result = syscall_error(BORING_SYSCALL_ENOSYS);
            break;
    }

    frame->result = result;
    if (!syscall_return_state_valid(frame)) {
        syscall_fatal("invalid SYSRETQ user return state");
    }
}
