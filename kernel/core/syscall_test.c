#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/address_space.h>
#include <boring/cpu.h>
#include <boring/descriptor.h>
#include <boring/exception.h>
#include <boring/pmm.h>
#include <boring/process.h>
#include <boring/ring3_memory.h>
#include <boring/serial.h>
#include <boring/syscall.h>
#include <boring/syscall_test.h>
#include <boring/vmm.h>

#define SYSCALL_USER_CODE_VA 0x0000000040000000ULL
#define SYSCALL_USER_STACK_BASE 0x0000000040010000ULL
#define SYSCALL_USER_STACK_TOP (SYSCALL_USER_STACK_BASE + VMM_PAGE_SIZE)
#define SYSCALL_USER_RESULT_OFFSET 0x0f00U
#define SYSCALL_USER_RESULT_VA (SYSCALL_USER_STACK_BASE + SYSCALL_USER_RESULT_OFFSET)
#define SYSCALL_USER_MESSAGE_OFFSET 0x0200U
#define SYSCALL_USER_MESSAGE_VA (SYSCALL_USER_STACK_BASE + SYSCALL_USER_MESSAGE_OFFSET)
#define SYSCALL_USER_UNMAPPED_VA 0x0000000040020000ULL
#define SYSCALL_USER_AFTER_MARKER 0x5359535245544f4bULL
#define SYSCALL_USER_FINAL_MARKER 0x53595343414c4c21ULL
#define SYSCALL_GP_VECTOR 13ULL
#define SYSCALL_MESSAGE_LENGTH 25U

struct syscall_user_result {
    uint64_t initial_cs;
    uint64_t initial_rsp;
    uint64_t getpid_result;
    uint64_t after_getpid_marker;
    uint64_t post_sysret_cs;
    uint64_t post_sysret_rsp;
    uint64_t debug_write_result;
    uint64_t unmapped_result;
    uint64_t kernel_result;
    uint64_t overflow_result;
    uint64_t oversized_result;
    uint64_t unknown_result;
    uint64_t callee_saved_ok;
    uint64_t final_marker;
};

struct syscall_test_state {
    struct process *process;
    uint64_t code_frame;
    uint64_t stack_frame;
    uintptr_t expected_cli_rip;
    uintptr_t expected_user_rsp;
    bool armed;
};

_Static_assert(sizeof(struct syscall_user_result) == 112U,
               "syscall result layout must match acceptance assembly");
_Static_assert(offsetof(struct syscall_user_result, initial_cs) == 0U,
               "syscall result initial CS offset mismatch");
_Static_assert(offsetof(struct syscall_user_result, getpid_result) == 16U,
               "syscall result GETPID offset mismatch");
_Static_assert(offsetof(struct syscall_user_result, debug_write_result) == 48U,
               "syscall result DEBUG_WRITE offset mismatch");
_Static_assert(offsetof(struct syscall_user_result, callee_saved_ok) == 96U,
               "syscall result callee-saved offset mismatch");

extern const uint8_t x86_64_syscall_test_payload_start[];
extern const uint8_t x86_64_syscall_test_payload_cli[];
extern const uint8_t x86_64_syscall_test_payload_end[];
void x86_64_enter_ring3(uintptr_t user_rip,
                        uintptr_t user_rsp,
                        uint16_t user_cs,
                        uint16_t user_ss,
                        uintptr_t result_address)
    __attribute__((noreturn));

static struct syscall_test_state syscall_test_state;

static void syscall_test_fail(const char *check) __attribute__((noreturn));
static void syscall_test_fail(const char *check) {
    serial_write_string("  ");
    serial_write_string(check);
    serial_write_string(": FAIL\n");
    serial_write_string("Syscall self-test FAILED: ");
    serial_write_string(check);
    serial_write_string("\n");
    x86_64_halt_forever();
}

static uint64_t expected_error(int error_number) {
    return (uint64_t)(-(int64_t)error_number);
}

static bool physical_pointer(uint64_t physical_address, uint8_t **pointer) {
    struct vmm_stats stats;
    uint64_t virtual_address;

    if ((pointer == NULL) ||
        ((physical_address & (VMM_PAGE_SIZE - 1ULL)) != 0ULL) ||
        !vmm_get_stats(&stats) ||
        (physical_address > UINT64_MAX - stats.hhdm_offset)) {
        return false;
    }

    virtual_address = stats.hhdm_offset + physical_address;
    if ((virtual_address >> 48U) != 0xffffULL) {
        return false;
    }

    *pointer = (uint8_t *)(uintptr_t)virtual_address;
    return true;
}

static void zero_page(uint8_t *page) {
    size_t index;

    for (index = 0U; index < (size_t)VMM_PAGE_SIZE; ++index) {
        page[index] = 0U;
    }
}

static bool prepare_user_pages(uint64_t code_physical,
                               uint64_t stack_physical,
                               uintptr_t *cli_rip_out) {
    static const char message[] = "hello from boring syscall";
    uint8_t *code_page;
    uint8_t *stack_page;
    const uintptr_t payload_start =
        (uintptr_t)&x86_64_syscall_test_payload_start[0];
    const uintptr_t payload_cli =
        (uintptr_t)&x86_64_syscall_test_payload_cli[0];
    const uintptr_t payload_end =
        (uintptr_t)&x86_64_syscall_test_payload_end[0];
    size_t payload_size;
    size_t index;

    if ((cli_rip_out == NULL) || (payload_end <= payload_start) ||
        (payload_cli < payload_start) || (payload_cli >= payload_end) ||
        !physical_pointer(code_physical, &code_page) ||
        !physical_pointer(stack_physical, &stack_page)) {
        return false;
    }

    payload_size = (size_t)(payload_end - payload_start);
    if ((payload_size > (size_t)VMM_PAGE_SIZE) ||
        ((sizeof(message) - 1U) != (size_t)SYSCALL_MESSAGE_LENGTH) ||
        ((size_t)SYSCALL_USER_MESSAGE_OFFSET +
         (sizeof(message) - 1U) > (size_t)VMM_PAGE_SIZE) ||
        ((size_t)SYSCALL_USER_RESULT_OFFSET +
         sizeof(struct syscall_user_result) > (size_t)VMM_PAGE_SIZE)) {
        return false;
    }

    zero_page(code_page);
    zero_page(stack_page);
    for (index = 0U; index < payload_size; ++index) {
        code_page[index] = x86_64_syscall_test_payload_start[index];
    }
    for (index = 0U; index < sizeof(message) - 1U; ++index) {
        stack_page[(size_t)SYSCALL_USER_MESSAGE_OFFSET + index] =
            (uint8_t)message[index];
    }

    *cli_rip_out = (uintptr_t)SYSCALL_USER_CODE_VA +
        (payload_cli - payload_start);
    return true;
}

bool syscall_test_exception_armed(void) {
    return syscall_test_state.armed;
}

void syscall_test_run(void) {
    struct syscall_stats syscall_stats;
    struct descriptor_stats descriptors;
    struct process *process = NULL;
    uint64_t translated;

    syscall_test_state.process = NULL;
    syscall_test_state.code_frame = 0ULL;
    syscall_test_state.stack_frame = 0ULL;
    syscall_test_state.expected_cli_rip = 0U;
    syscall_test_state.expected_user_rsp = 0U;
    syscall_test_state.armed = false;

    serial_write_string("Syscall test:\n");

    if (!syscall_init() || !syscall_get_stats(&syscall_stats) ||
        !syscall_stats.supported || !syscall_stats.initialized) {
        syscall_test_fail("msr-config");
    }
    serial_write_string("  msr-config: PASS\n");

    if (!descriptor_get_stats(&descriptors) ||
        (descriptors.kernel_code_selector !=
         (uint16_t)X86_64_GDT_KERNEL_CODE_SELECTOR) ||
        (descriptors.kernel_data_selector !=
         (uint16_t)X86_64_GDT_KERNEL_DATA_SELECTOR) ||
        (descriptors.user_code_selector !=
         (uint16_t)X86_64_GDT_USER_CODE_SELECTOR) ||
        (descriptors.user_data_selector !=
         (uint16_t)X86_64_GDT_USER_DATA_SELECTOR)) {
        syscall_test_fail("gdt-selectors");
    }
    serial_write_string("  gdt-selectors: PASS\n");

    if (!process_init() || !process_create(&process) ||
        (process == NULL) || (process->pid != 1ULL) ||
        !pmm_alloc_frame(&syscall_test_state.code_frame) ||
        !pmm_alloc_frame(&syscall_test_state.stack_frame)) {
        syscall_test_fail("process-address-space");
    }
    syscall_test_state.process = process;

    if (!prepare_user_pages(syscall_test_state.code_frame,
                            syscall_test_state.stack_frame,
                            &syscall_test_state.expected_cli_rip) ||
        !ring3_user_map_page(&process->address_space,
                             (uintptr_t)SYSCALL_USER_CODE_VA,
                             syscall_test_state.code_frame, false) ||
        !ring3_user_mapping_valid(&process->address_space,
                                  (uintptr_t)SYSCALL_USER_CODE_VA,
                                  syscall_test_state.code_frame, false) ||
        !address_space_translate(&process->address_space,
                                 (uintptr_t)SYSCALL_USER_CODE_VA,
                                 &translated) ||
        (translated != syscall_test_state.code_frame)) {
        syscall_test_fail("user-code-mapped");
    }
    serial_write_string("  user-code-mapped: PASS\n");

    if (!ring3_user_map_page(&process->address_space,
                             (uintptr_t)SYSCALL_USER_STACK_BASE,
                             syscall_test_state.stack_frame, true) ||
        !ring3_user_mapping_valid(&process->address_space,
                                  (uintptr_t)SYSCALL_USER_STACK_BASE,
                                  syscall_test_state.stack_frame, true) ||
        !address_space_translate(&process->address_space,
                                 (uintptr_t)SYSCALL_USER_STACK_BASE,
                                 &translated) ||
        (translated != syscall_test_state.stack_frame)) {
        syscall_test_fail("user-stack-mapped");
    }
    serial_write_string("  user-stack-mapped: PASS\n");

    if (!ring3_shared_higher_half_supervisor_only(&process->address_space)) {
        syscall_test_fail("higher-half-supervisor-only");
    }
    serial_write_string("  higher-half-supervisor-only: PASS\n");

    serial_write_string("IA32_EFER: ");
    serial_write_hex_u64(syscall_stats.efer);
    serial_write_string("\nIA32_STAR: ");
    serial_write_hex_u64(syscall_stats.star);
    serial_write_string("\nIA32_LSTAR: ");
    serial_write_hex_u64(syscall_stats.lstar);
    serial_write_string("\nIA32_FMASK: ");
    serial_write_hex_u64(syscall_stats.fmask);
    serial_write_string("\nSyscall stack base: ");
    serial_write_hex_u64((uint64_t)syscall_stats.stack_base);
    serial_write_string("\nSyscall stack top: ");
    serial_write_hex_u64((uint64_t)syscall_stats.stack_top);
    serial_write_string("\nUser code VA: 0x0000000040000000\n");
    serial_write_string("User stack base: 0x0000000040010000\n");
    serial_write_string("User stack top: 0x0000000040011000\n");
    serial_write_string("User message VA: 0x0000000040010200\n");

    syscall_test_state.expected_user_rsp =
        (uintptr_t)SYSCALL_USER_STACK_TOP;
    x86_64_interrupts_disable();
    if (!process_activate(process) ||
        !ring3_shared_higher_half_supervisor_only(&process->address_space)) {
        syscall_test_fail("cr3-activate");
    }

    syscall_test_state.armed = true;
    serial_write_string("Entering syscall CPL3 payload with IRETQ.\n");
    x86_64_enter_ring3((uintptr_t)SYSCALL_USER_CODE_VA,
                       syscall_test_state.expected_user_rsp,
                       (uint16_t)X86_64_GDT_USER_CODE_SELECTOR,
                       (uint16_t)X86_64_GDT_USER_DATA_SELECTOR,
                       (uintptr_t)SYSCALL_USER_RESULT_VA);
}

void syscall_test_handle_exception(const struct x86_64_trap_frame *frame) {
    struct syscall_stats syscall_stats;
    uint8_t *stack_page;
    const struct syscall_user_result *result;
    const uintptr_t handler_rsp = x86_64_read_rsp();

    if ((!syscall_test_state.armed) || (frame == NULL) ||
        !physical_pointer(syscall_test_state.stack_frame, &stack_page)) {
        syscall_test_fail("exception-frame");
    }
    syscall_test_state.armed = false;
    result = (const struct syscall_user_result *)(const void *)(
        &stack_page[SYSCALL_USER_RESULT_OFFSET]);

    serial_write_string("Syscall return evidence:\n");
    serial_write_string("Final fault vector: ");
    serial_write_u64(frame->vector);
    serial_write_string("\nFinal saved CS: ");
    serial_write_hex_u64(frame->cs);
    serial_write_string("\nFinal saved user RIP: ");
    serial_write_hex_u64(frame->rip);
    serial_write_string("\nFinal saved user RSP: ");
    serial_write_hex_u64(frame->rsp);
    serial_write_string("\nFinal exception kernel RSP: ");
    serial_write_hex_u64((uint64_t)handler_rsp);
    serial_write_string("\n");

    if ((result->initial_cs != (uint64_t)X86_64_GDT_USER_CODE_SELECTOR) ||
        (result->initial_rsp !=
         (uint64_t)syscall_test_state.expected_user_rsp)) {
        syscall_test_fail("entered-cpl3");
    }
    serial_write_string("  entered-cpl3: PASS\n");

    if (!syscall_get_stats(&syscall_stats) ||
        (syscall_stats.dispatch_count != 7ULL)) {
        syscall_test_fail("syscall-entered-cpl0");
    }
    serial_write_string("  syscall-entered-cpl0: PASS\n");

    if (!syscall_stack_contains(syscall_stats.last_kernel_rsp) ||
        syscall_stack_contains(syscall_stats.last_user_rsp) ||
        (syscall_stats.last_user_rsp !=
         syscall_test_state.expected_user_rsp)) {
        syscall_test_fail("syscall-kernel-stack");
    }
    serial_write_string("  syscall-kernel-stack: PASS\n");

    if (result->getpid_result != syscall_test_state.process->pid) {
        syscall_test_fail("getpid");
    }
    serial_write_string("  getpid: PASS\n");

    if (result->debug_write_result != (uint64_t)SYSCALL_MESSAGE_LENGTH) {
        syscall_test_fail("valid-user-copy");
    }
    serial_write_string("  valid-user-copy: PASS\n");

    if (result->unmapped_result != expected_error(BORING_SYSCALL_EFAULT)) {
        syscall_test_fail("unmapped-user-pointer-rejected");
    }
    serial_write_string("  unmapped-user-pointer-rejected: PASS\n");

    if (result->kernel_result != expected_error(BORING_SYSCALL_EFAULT)) {
        syscall_test_fail("kernel-pointer-rejected");
    }
    serial_write_string("  kernel-pointer-rejected: PASS\n");

    if (result->overflow_result != expected_error(BORING_SYSCALL_EFAULT)) {
        syscall_test_fail("overflowing-user-range-rejected");
    }
    serial_write_string("  overflowing-user-range-rejected: PASS\n");

    if (result->oversized_result != expected_error(BORING_SYSCALL_EINVAL)) {
        syscall_test_fail("oversized-length-rejected");
    }
    serial_write_string("  oversized-length-rejected: PASS\n");

    if (result->unknown_result != expected_error(BORING_SYSCALL_ENOSYS)) {
        syscall_test_fail("unknown-syscall");
    }
    serial_write_string("  unknown-syscall: PASS\n");

    if ((result->after_getpid_marker != SYSCALL_USER_AFTER_MARKER) ||
        (result->post_sysret_cs !=
         (uint64_t)X86_64_GDT_USER_CODE_SELECTOR)) {
        syscall_test_fail("sysret-cpl3");
    }
    serial_write_string("  sysret-cpl3: PASS\n");

    if ((result->post_sysret_rsp !=
         (uint64_t)syscall_test_state.expected_user_rsp) ||
        (frame->rsp != (uint64_t)syscall_test_state.expected_user_rsp)) {
        syscall_test_fail("user-rsp-restored");
    }
    serial_write_string("  user-rsp-restored: PASS\n");

    if (result->callee_saved_ok != 1ULL) {
        syscall_test_fail("callee-saved-preserved");
    }
    serial_write_string("  callee-saved-preserved: PASS\n");

    if ((frame->vector != SYSCALL_GP_VECTOR) ||
        (frame->error_code != 0ULL) ||
        !exception_frame_originates_from_cpl3(frame) ||
        (frame->cs != (uint64_t)X86_64_GDT_USER_CODE_SELECTOR) ||
        (frame->ss != (uint64_t)X86_64_GDT_USER_DATA_SELECTOR) ||
        (frame->rip != (uint64_t)syscall_test_state.expected_cli_rip) ||
        (result->final_marker != SYSCALL_USER_FINAL_MARKER)) {
        syscall_test_fail("final-cpl3-proof");
    }
    serial_write_string("  final-cpl3-proof: PASS\n");

    if (!descriptor_rsp0_stack_contains(handler_rsp) ||
        !descriptor_rsp0_stack_contains((uintptr_t)frame) ||
        syscall_stack_contains(handler_rsp)) {
        syscall_test_fail("final-tss-rsp0");
    }
    serial_write_string("  final-tss-rsp0: PASS\n");

    serial_write_string("Live syscall kernel RSP: ");
    serial_write_hex_u64((uint64_t)syscall_stats.last_kernel_rsp);
    serial_write_string("\nSaved syscall user RSP: ");
    serial_write_hex_u64((uint64_t)syscall_stats.last_user_rsp);
    serial_write_string("\nGETPID result: ");
    serial_write_u64(result->getpid_result);
    serial_write_string("\nDEBUG_WRITE result: ");
    serial_write_u64(result->debug_write_result);
    serial_write_string("\nSyscall dispatches: ");
    serial_write_u64(syscall_stats.dispatch_count);
    serial_write_string("\nBoringKernel syscall boundary test passed.\n");
    x86_64_halt_forever();
}
