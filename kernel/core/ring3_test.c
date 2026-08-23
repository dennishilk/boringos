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
#include <boring/ring3_test.h>
#include <boring/serial.h>
#include <boring/vmm.h>

#define RING3_USER_CODE_VA 0x0000000040000000ULL
#define RING3_USER_STACK_BASE 0x0000000040010000ULL
#define RING3_USER_STACK_TOP (RING3_USER_STACK_BASE + VMM_PAGE_SIZE)
#define RING3_USER_RESULT_VA (RING3_USER_STACK_BASE + 0x100ULL)
#define RING3_USER_MARKER 0x52494e4733544553ULL
#define RING3_USER_STACK_VALUE 0x5aULL
#define RING3_RFLAGS_RESERVED_BIT 0x2ULL
#define RING3_GP_VECTOR 13ULL

struct ring3_user_result {
    uint64_t cs;
    uint64_t rsp;
    uint64_t marker;
    uint64_t stack_value;
};

struct ring3_test_state {
    struct process *process;
    uint64_t code_frame;
    uint64_t stack_frame;
    uintptr_t expected_cli_rip;
    uintptr_t expected_user_rsp;
    bool armed;
};

extern const uint8_t x86_64_ring3_payload_start[];
extern const uint8_t x86_64_ring3_payload_cli[];
extern const uint8_t x86_64_ring3_payload_end[];
void x86_64_enter_ring3(uintptr_t user_rip,
                        uintptr_t user_rsp,
                        uint16_t user_cs,
                        uint16_t user_ss,
                        uintptr_t result_address)
    __attribute__((noreturn));

static struct ring3_test_state ring3_state;

static void ring3_fail(const char *check) __attribute__((noreturn));
static void ring3_fail(const char *check) {
    serial_write_string("  ");
    serial_write_string(check);
    serial_write_string(": FAIL\n");
    serial_write_string("Ring 3 self-test FAILED: ");
    serial_write_string(check);
    serial_write_string("\n");
    x86_64_halt_forever();
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

static bool prepare_user_code(uint64_t physical_address,
                              uintptr_t *cli_rip_out) {
    uint8_t *page;
    const uintptr_t payload_start =
        (uintptr_t)&x86_64_ring3_payload_start[0];
    const uintptr_t payload_cli =
        (uintptr_t)&x86_64_ring3_payload_cli[0];
    const uintptr_t payload_end =
        (uintptr_t)&x86_64_ring3_payload_end[0];
    size_t payload_size;
    size_t index;

    if ((cli_rip_out == NULL) || (payload_end <= payload_start) ||
        (payload_cli < payload_start) || (payload_cli >= payload_end) ||
        !physical_pointer(physical_address, &page)) {
        return false;
    }

    payload_size = (size_t)(payload_end - payload_start);
    if (payload_size > (size_t)VMM_PAGE_SIZE) {
        return false;
    }

    zero_page(page);
    for (index = 0U; index < payload_size; ++index) {
        page[index] = x86_64_ring3_payload_start[index];
    }

    *cli_rip_out = (uintptr_t)RING3_USER_CODE_VA +
        (payload_cli - payload_start);
    return true;
}

static bool saved_user_stack_valid(uint64_t saved_rsp) {
    return (saved_rsp > RING3_USER_STACK_BASE) &&
           (saved_rsp <= RING3_USER_STACK_TOP);
}

bool ring3_test_exception_armed(void) {
    return ring3_state.armed;
}

void ring3_test_run(void) {
    struct descriptor_stats descriptors;
    struct process *process = NULL;
    uint8_t *stack_page;
    uint64_t translated;

    ring3_state.process = NULL;
    ring3_state.code_frame = 0ULL;
    ring3_state.stack_frame = 0ULL;
    ring3_state.expected_cli_rip = 0U;
    ring3_state.expected_user_rsp = 0U;
    ring3_state.armed = false;

    serial_write_string("Ring 3 test:\n");

    if (!descriptor_get_stats(&descriptors) ||
        (descriptors.gdt_entries != (uint16_t)X86_64_GDT_ENTRY_COUNT) ||
        (descriptors.kernel_code_selector !=
         (uint16_t)X86_64_GDT_KERNEL_CODE_SELECTOR) ||
        (descriptors.kernel_data_selector !=
         (uint16_t)X86_64_GDT_KERNEL_DATA_SELECTOR) ||
        (descriptors.user_code_selector !=
         (uint16_t)X86_64_GDT_USER_CODE_SELECTOR) ||
        (descriptors.user_data_selector !=
         (uint16_t)X86_64_GDT_USER_DATA_SELECTOR)) {
        ring3_fail("gdt");
    }
    serial_write_string("  gdt: PASS\n");

    if ((descriptors.task_register !=
         (uint16_t)X86_64_GDT_TSS_SELECTOR) ||
        (descriptors.tss_rsp0 != descriptors.rsp0_stack_top) ||
        (descriptors.rsp0_stack_top <= descriptors.rsp0_stack_base) ||
        ((descriptors.rsp0_stack_top - descriptors.rsp0_stack_base) !=
         (uintptr_t)X86_64_TSS_RSP0_STACK_SIZE)) {
        ring3_fail("tss-loaded");
    }
    serial_write_string("  tss-loaded: PASS\n");

    if (!process_init() || !process_create(&process) ||
        (process == NULL) || (process->pid != 1ULL) ||
        !pmm_alloc_frame(&ring3_state.code_frame) ||
        !pmm_alloc_frame(&ring3_state.stack_frame)) {
        ring3_fail("process-address-space");
    }
    ring3_state.process = process;

    if (!prepare_user_code(ring3_state.code_frame,
                           &ring3_state.expected_cli_rip) ||
        !ring3_user_map_page(&process->address_space,
                             (uintptr_t)RING3_USER_CODE_VA,
                             ring3_state.code_frame, false) ||
        !ring3_user_mapping_valid(&process->address_space,
                                  (uintptr_t)RING3_USER_CODE_VA,
                                  ring3_state.code_frame, false) ||
        !address_space_translate(&process->address_space,
                                 (uintptr_t)RING3_USER_CODE_VA,
                                 &translated) ||
        (translated != ring3_state.code_frame)) {
        ring3_fail("user-code-mapped");
    }
    serial_write_string("  user-code-mapped: PASS\n");

    if (!physical_pointer(ring3_state.stack_frame, &stack_page)) {
        ring3_fail("user-stack-mapped");
    }
    zero_page(stack_page);

    if (!ring3_user_map_page(&process->address_space,
                             (uintptr_t)RING3_USER_STACK_BASE,
                             ring3_state.stack_frame, true) ||
        !ring3_user_mapping_valid(&process->address_space,
                                  (uintptr_t)RING3_USER_STACK_BASE,
                                  ring3_state.stack_frame, true) ||
        !address_space_translate(&process->address_space,
                                 (uintptr_t)RING3_USER_STACK_BASE,
                                 &translated) ||
        (translated != ring3_state.stack_frame) ||
        !address_space_kernel_mappings_valid(&process->address_space)) {
        ring3_fail("user-stack-mapped");
    }
    serial_write_string("  user-stack-mapped: PASS\n");

    serial_write_string("GDT base: ");
    serial_write_hex_u64((uint64_t)descriptors.gdtr_base);
    serial_write_string("\nKernel CS: ");
    serial_write_hex_u64((uint64_t)descriptors.kernel_code_selector);
    serial_write_string("\nKernel SS: ");
    serial_write_hex_u64((uint64_t)descriptors.kernel_data_selector);
    serial_write_string("\nUser CS: ");
    serial_write_hex_u64((uint64_t)descriptors.user_code_selector);
    serial_write_string("\nUser SS: ");
    serial_write_hex_u64((uint64_t)descriptors.user_data_selector);
    serial_write_string("\nTR: ");
    serial_write_hex_u64((uint64_t)descriptors.task_register);
    serial_write_string("\nTSS RSP0: ");
    serial_write_hex_u64((uint64_t)descriptors.tss_rsp0);
    serial_write_string("\nRSP0 stack base: ");
    serial_write_hex_u64((uint64_t)descriptors.rsp0_stack_base);
    serial_write_string("\nRSP0 stack top: ");
    serial_write_hex_u64((uint64_t)descriptors.rsp0_stack_top);
    serial_write_string("\nUser code VA: 0x0000000040000000\n");
    serial_write_string("User stack base: 0x0000000040010000\n");
    serial_write_string("User stack top: 0x0000000040011000\n");

    ring3_state.expected_user_rsp = (uintptr_t)RING3_USER_STACK_TOP;
    x86_64_interrupts_disable();
    if (!process_activate(process) ||
        !address_space_kernel_mappings_valid(&process->address_space)) {
        ring3_fail("cr3-activate");
    }

    ring3_state.armed = true;
    serial_write_string("Entering real CPL3 with IRETQ.\n");
    x86_64_enter_ring3((uintptr_t)RING3_USER_CODE_VA,
                       ring3_state.expected_user_rsp,
                       (uint16_t)X86_64_GDT_USER_CODE_SELECTOR,
                       (uint16_t)X86_64_GDT_USER_DATA_SELECTOR,
                       (uintptr_t)RING3_USER_RESULT_VA);
}

void ring3_test_handle_exception(const struct x86_64_trap_frame *frame) {
    const struct ring3_user_result *const result =
        (const struct ring3_user_result *)(uintptr_t)RING3_USER_RESULT_VA;
    const uintptr_t handler_rsp = x86_64_read_rsp();

    if ((!ring3_state.armed) || (frame == NULL)) {
        ring3_fail("exception-frame");
    }

    ring3_state.armed = false;

    serial_write_string("Ring 3 fault evidence:\n");
    serial_write_string("Fault vector: ");
    serial_write_u64(frame->vector);
    serial_write_string("\nSaved CS: ");
    serial_write_hex_u64(frame->cs);
    serial_write_string("\nSaved SS: ");
    serial_write_hex_u64(frame->ss);
    serial_write_string("\nSaved user RIP: ");
    serial_write_hex_u64(frame->rip);
    serial_write_string("\nSaved user RSP: ");
    serial_write_hex_u64(frame->rsp);
    serial_write_string("\nKernel handler RSP: ");
    serial_write_hex_u64((uint64_t)handler_rsp);
    serial_write_string("\n");

    if ((result->cs != (uint64_t)X86_64_GDT_USER_CODE_SELECTOR) ||
        ((result->cs & 0x3ULL) != 0x3ULL) ||
        (result->marker != RING3_USER_MARKER)) {
        ring3_fail("entered-cpl3");
    }
    serial_write_string("  entered-cpl3: PASS\n");

    if ((frame->vector != RING3_GP_VECTOR) ||
        (frame->error_code != 0ULL)) {
        ring3_fail("privileged-operation-blocked");
    }
    serial_write_string("  privileged-operation-blocked: PASS\n");

    if (((frame->cs & 0x3ULL) != 0x3ULL) ||
        (frame->cs != (uint64_t)X86_64_GDT_USER_CODE_SELECTOR) ||
        ((frame->ss & 0x3ULL) != 0x3ULL) ||
        (frame->ss != (uint64_t)X86_64_GDT_USER_DATA_SELECTOR)) {
        ring3_fail("exception-origin-cpl3");
    }
    serial_write_string("  exception-origin-cpl3: PASS\n");

    if (!descriptor_rsp0_stack_contains(handler_rsp) ||
        !descriptor_rsp0_stack_contains((uintptr_t)frame) ||
        !saved_user_stack_valid(frame->rsp) ||
        descriptor_rsp0_stack_contains((uintptr_t)frame->rsp)) {
        ring3_fail("kernel-stack-transition");
    }
    serial_write_string("  kernel-stack-transition: PASS\n");

    if (frame->rip != (uint64_t)ring3_state.expected_cli_rip) {
        ring3_fail("user-rip-preserved");
    }
    serial_write_string("  user-rip-preserved: PASS\n");

    if ((frame->rsp != (uint64_t)ring3_state.expected_user_rsp) ||
        (result->rsp != (uint64_t)ring3_state.expected_user_rsp)) {
        ring3_fail("user-rsp-preserved");
    }
    serial_write_string("  user-rsp-preserved: PASS\n");

    if (frame->ss != (uint64_t)X86_64_GDT_USER_DATA_SELECTOR) {
        ring3_fail("user-ss-preserved");
    }
    serial_write_string("  user-ss-preserved: PASS\n");

    if (result->stack_value != RING3_USER_STACK_VALUE) {
        ring3_fail("user-stack-write");
    }
    serial_write_string("  user-stack-write: PASS\n");

    serial_write_string("BoringKernel Ring 3 test passed.\n");
    x86_64_halt_forever();
}
