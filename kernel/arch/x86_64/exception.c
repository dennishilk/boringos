#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/cpu.h>
#include <boring/descriptor.h>
#include <boring/exception.h>
#include <boring/ring3_test.h>
#include <boring/serial.h>
#if (BORING_TEST_MODE >= 4) && (BORING_TEST_MODE <= 6)
#include <boring/syscall_test.h>
#endif

#define IDT_GATE_INTERRUPT 0x8eU
#define IDT_IST_NONE 0U
#define PAGE_FAULT_PRESENT (1ULL << 0)
#define PAGE_FAULT_WRITE (1ULL << 1)
#define PAGE_FAULT_USER (1ULL << 2)
#define PAGE_FAULT_RESERVED (1ULL << 3)
#define PAGE_FAULT_INSTRUCTION (1ULL << 4)

struct x86_64_idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct x86_64_idtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

_Static_assert(sizeof(struct x86_64_idt_entry) == 16U,
               "x86_64 IDT entry must be 16 bytes");
_Static_assert(sizeof(struct x86_64_idtr) == 10U,
               "x86_64 IDTR image must be 10 bytes");
_Static_assert(sizeof(struct x86_64_trap_frame) == 192U,
               "trap-frame layout must match x86_64 entry stubs");
_Static_assert(offsetof(struct x86_64_trap_frame, rsp) == 0U,
               "trap-frame RSP copy offset mismatch");
_Static_assert(offsetof(struct x86_64_trap_frame, ss) == 8U,
               "trap-frame SS copy offset mismatch");
_Static_assert(offsetof(struct x86_64_trap_frame, vector) == 136U,
               "trap-frame vector offset mismatch");
_Static_assert(offsetof(struct x86_64_trap_frame, error_code) == 144U,
               "trap-frame error-code offset mismatch");
_Static_assert(offsetof(struct x86_64_trap_frame, rip) == 152U,
               "trap-frame RIP offset mismatch");
_Static_assert(offsetof(struct x86_64_trap_frame, cs) == 160U,
               "trap-frame CS offset mismatch");
_Static_assert(offsetof(struct x86_64_trap_frame, rflags) == 168U,
               "trap-frame RFLAGS offset mismatch");
_Static_assert(offsetof(struct x86_64_trap_frame, hardware_rsp) == 176U,
               "trap-frame hardware RSP offset mismatch");
_Static_assert(offsetof(struct x86_64_trap_frame, hardware_ss) == 184U,
               "trap-frame hardware SS offset mismatch");

extern const uintptr_t x86_64_exception_stub_table[X86_64_EXCEPTION_VECTOR_COUNT];
void x86_64_load_idt(const struct x86_64_idtr *idtr);
void x86_64_store_idt(struct x86_64_idtr *idtr);
uint16_t x86_64_read_cs(void);
uint64_t x86_64_read_cr2(void);

static struct x86_64_idt_entry idt[X86_64_IDT_ENTRY_COUNT]
    __attribute__((aligned(16)));
static struct exception_stats exception_state;
static bool exception_initialized;

static const char *const exception_names[X86_64_EXCEPTION_VECTOR_COUNT] = {
    "Divide Error",
    "Debug",
    "Non-Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Reserved (legacy Coprocessor Segment Overrun)",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 Floating-Point Exception",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Hypervisor Injection Exception",
    "VMM Communication Exception",
    "Security Exception",
    "Reserved"
};

static uintptr_t idt_entry_offset(const struct x86_64_idt_entry *entry) {
    uint64_t offset;

    offset = (uint64_t)entry->offset_low;
    offset |= (uint64_t)entry->offset_middle << 16;
    offset |= (uint64_t)entry->offset_high << 32;
    return (uintptr_t)offset;
}

static void idt_set_gate(uint8_t vector, uintptr_t handler,
                         uint16_t selector) {
    struct x86_64_idt_entry *const entry = &idt[vector];
    const uint64_t address = (uint64_t)handler;

    entry->offset_low = (uint16_t)(address & 0xffffULL);
    entry->selector = selector;
    entry->ist = (uint8_t)IDT_IST_NONE;
    entry->type_attributes = (uint8_t)IDT_GATE_INTERRUPT;
    entry->offset_middle = (uint16_t)((address >> 16) & 0xffffULL);
    entry->offset_high = (uint32_t)((address >> 32) & 0xffffffffULL);
    entry->reserved = 0U;
}

static bool idt_entry_matches(uint8_t vector, uint16_t selector,
                              uintptr_t expected) {
    const struct x86_64_idt_entry *const entry = &idt[vector];

    return (expected != (uintptr_t)0U) &&
           (entry->selector == selector) &&
           (entry->ist == (uint8_t)IDT_IST_NONE) &&
           (entry->type_attributes == (uint8_t)IDT_GATE_INTERRUPT) &&
           (entry->reserved == 0U) &&
           (idt_entry_offset(entry) == expected);
}

static bool idt_exception_entry_valid(uint8_t vector, uint16_t selector) {
    return idt_entry_matches(vector, selector,
                             x86_64_exception_stub_table[vector]);
}

bool exception_init(void) {
    struct x86_64_idtr requested;
    struct x86_64_idtr active;
    const uint16_t selector =
        (uint16_t)X86_64_GDT_KERNEL_CODE_SELECTOR;
    size_t index;

    exception_initialized = false;
    exception_state.idtr_base = (uintptr_t)0U;
    exception_state.idtr_limit = 0U;
    exception_state.code_selector = 0U;
    exception_state.configured_vectors = 0U;

    if (!descriptor_init() || (x86_64_read_cs() != selector)) {
        return false;
    }

    for (index = 0U; index < (size_t)X86_64_IDT_ENTRY_COUNT; ++index) {
        idt[index].offset_low = 0U;
        idt[index].selector = 0U;
        idt[index].ist = 0U;
        idt[index].type_attributes = 0U;
        idt[index].offset_middle = 0U;
        idt[index].offset_high = 0U;
        idt[index].reserved = 0U;
    }

    for (index = 0U; index < (size_t)X86_64_EXCEPTION_VECTOR_COUNT; ++index) {
        if (x86_64_exception_stub_table[index] == (uintptr_t)0U) {
            return false;
        }
        idt_set_gate((uint8_t)index, x86_64_exception_stub_table[index],
                     selector);
    }

    for (index = 0U; index < (size_t)X86_64_EXCEPTION_VECTOR_COUNT; ++index) {
        if (!idt_exception_entry_valid((uint8_t)index, selector)) {
            return false;
        }
    }

    requested.limit = (uint16_t)(sizeof(idt) - 1U);
    requested.base = (uint64_t)(uintptr_t)&idt[0];
    active.limit = 0U;
    active.base = 0ULL;

    x86_64_load_idt(&requested);
    x86_64_store_idt(&active);

    if ((active.limit != requested.limit) ||
        (active.base != requested.base)) {
        return false;
    }

    exception_state.idtr_base = (uintptr_t)active.base;
    exception_state.idtr_limit = active.limit;
    exception_state.code_selector = selector;
    exception_state.configured_vectors =
        (uint16_t)X86_64_EXCEPTION_VECTOR_COUNT;
    exception_initialized = true;
    return true;
}

bool exception_get_stats(struct exception_stats *stats) {
    if ((!exception_initialized) || (stats == NULL)) {
        return false;
    }

    *stats = exception_state;
    return true;
}

bool exception_install_interrupt_gate(uint8_t vector, uintptr_t handler) {
    if ((!exception_initialized) ||
        (vector < (uint8_t)X86_64_EXCEPTION_VECTOR_COUNT) ||
        (handler == (uintptr_t)0U)) {
        return false;
    }

    idt_set_gate(vector, handler, exception_state.code_selector);
    return idt_entry_matches(vector, exception_state.code_selector, handler);
}

bool exception_frame_originates_from_cpl3(
    const struct x86_64_trap_frame *frame) {
    if (frame == NULL) {
        return false;
    }

    return ((frame->cs & 0x3ULL) == 0x3ULL) &&
           ((frame->ss & 0x3ULL) == 0x3ULL) &&
           ((frame->hardware_ss & 0x3ULL) == 0x3ULL) &&
           (frame->rsp == frame->hardware_rsp) &&
           (frame->ss == frame->hardware_ss);
}

static const char *exception_name(uint64_t vector) {
    if (vector >= (uint64_t)X86_64_EXCEPTION_VECTOR_COUNT) {
        return "Unknown";
    }
    return exception_names[vector];
}

static void print_field(const char *name, uint64_t value) {
    serial_write_string(name);
    serial_write_hex_u64(value);
    serial_write_string("\n");
}

static void print_page_fault(const struct x86_64_trap_frame *frame) {
    const uint64_t error = frame->error_code;

    print_field("CR2: ", x86_64_read_cr2());
    serial_write_string("Page fault mapping: ");
    serial_write_string((error & PAGE_FAULT_PRESENT) != 0ULL
                            ? "protection violation\n"
                            : "non-present\n");
    serial_write_string("Page fault access: ");
    serial_write_string((error & PAGE_FAULT_WRITE) != 0ULL
                            ? "write\n"
                            : "read\n");
    serial_write_string("Page fault privilege: ");
    serial_write_string((error & PAGE_FAULT_USER) != 0ULL
                            ? "user\n"
                            : "supervisor\n");
    serial_write_string("Page fault reserved-bit violation: ");
    serial_write_string((error & PAGE_FAULT_RESERVED) != 0ULL
                            ? "yes\n"
                            : "no\n");
    serial_write_string("Page fault instruction fetch: ");
    serial_write_string((error & PAGE_FAULT_INSTRUCTION) != 0ULL
                            ? "yes\n"
                            : "no\n");
}

static void fatal_halt(void) __attribute__((noreturn));
static void fatal_halt(void) {
    serial_write_string("Fatal exception: controlled halt.\n");
    x86_64_halt_forever();
}

static void print_double_fault(const struct x86_64_trap_frame *frame)
    __attribute__((noreturn));
static void print_double_fault(const struct x86_64_trap_frame *frame) {
    serial_write_string("BoringKernel double fault\n\n");
    serial_write_string("Vector: 8\n");
    serial_write_string("Name: Double Fault\n");
    print_field("Error code: ", frame->error_code);
    print_field("RIP: ", frame->rip);
    print_field("RSP: ", frame->rsp);
    print_field("RFLAGS: ", frame->rflags);
    serial_write_string("Double Fault stack: current kernel stack (no IST yet)\n");
    fatal_halt();
}

void x86_64_exception_dispatch(const struct x86_64_trap_frame *frame) {
    if (frame == NULL) {
        serial_write_string("BoringKernel exception\n\nInvalid trap frame\n");
        fatal_halt();
    }

#if (BORING_TEST_MODE >= 4) && (BORING_TEST_MODE <= 6)
    if (syscall_test_exception_armed()) {
        syscall_test_handle_exception(frame);
    }
#endif

    if (ring3_test_exception_armed()) {
        ring3_test_handle_exception(frame);
    }

    if (frame->vector == 8ULL) {
        print_double_fault(frame);
    }

    serial_write_string("BoringKernel exception\n\n");
    serial_write_string("Vector: ");
    serial_write_u64(frame->vector);
    serial_write_string("\nName: ");
    serial_write_string(exception_name(frame->vector));
    serial_write_string("\n");
    print_field("Error code: ", frame->error_code);
    print_field("RIP: ", frame->rip);
    print_field("CS: ", frame->cs);
    print_field("RFLAGS: ", frame->rflags);
    print_field("RSP: ", frame->rsp);
    print_field("SS: ", frame->ss);

    if (frame->vector == 14ULL) {
        print_page_fault(frame);
    }

    print_field("RAX: ", frame->rax);
    print_field("RBX: ", frame->rbx);
    print_field("RCX: ", frame->rcx);
    print_field("RDX: ", frame->rdx);
    print_field("RBP: ", frame->rbp);
    print_field("RSI: ", frame->rsi);
    print_field("RDI: ", frame->rdi);
    print_field("R8: ", frame->r8);
    print_field("R9: ", frame->r9);
    print_field("R10: ", frame->r10);
    print_field("R11: ", frame->r11);
    print_field("R12: ", frame->r12);
    print_field("R13: ", frame->r13);
    print_field("R14: ", frame->r14);
    print_field("R15: ", frame->r15);
    fatal_halt();
}
