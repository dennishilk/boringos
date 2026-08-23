#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/cpu.h>
#include <boring/descriptor.h>

#define GDT_KERNEL_CODE_DESCRIPTOR 0x00af9a000000ffffULL
#define GDT_KERNEL_DATA_DESCRIPTOR 0x00cf92000000ffffULL
#define GDT_USER_DATA_DESCRIPTOR 0x00cff2000000ffffULL
#define GDT_USER_CODE_DESCRIPTOR 0x00affa000000ffffULL
#define GDT_TSS_AVAILABLE_ACCESS 0x89ULL

struct x86_64_tss {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

struct x86_64_gdtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

_Static_assert(sizeof(struct x86_64_tss) == 104U,
               "x86_64 TSS must match the architectural layout");
_Static_assert(offsetof(struct x86_64_tss, rsp0) == 4U,
               "x86_64 TSS RSP0 offset mismatch");
_Static_assert(offsetof(struct x86_64_tss, iomap_base) == 102U,
               "x86_64 TSS I/O map offset mismatch");
_Static_assert(sizeof(struct x86_64_gdtr) == 10U,
               "x86_64 GDTR image must be 10 bytes");

extern uint16_t x86_64_read_cs(void);
void x86_64_load_gdt(const struct x86_64_gdtr *gdtr,
                     uint16_t code_selector,
                     uint16_t data_selector);
void x86_64_store_gdt(struct x86_64_gdtr *gdtr);
void x86_64_load_tr(uint16_t selector);
uint16_t x86_64_store_tr(void);
uint16_t x86_64_read_ds(void);

static uint64_t gdt[X86_64_GDT_ENTRY_COUNT] __attribute__((aligned(16)));
static struct x86_64_tss tss __attribute__((aligned(16)));
static uint8_t rsp0_stack[X86_64_TSS_RSP0_STACK_SIZE]
    __attribute__((aligned(16)));
static struct descriptor_stats descriptor_state;
static bool descriptor_initialized;

static void memory_zero(void *memory, size_t size) {
    uint8_t *const bytes = (uint8_t *)memory;
    size_t index;

    for (index = 0U; index < size; ++index) {
        bytes[index] = 0U;
    }
}

static void gdt_set_tss_descriptor(uintptr_t base, uint32_t limit) {
    uint64_t low = 0ULL;

    low |= (uint64_t)(limit & 0xffffU);
    low |= ((uint64_t)base & 0xffffULL) << 16U;
    low |= (((uint64_t)base >> 16U) & 0xffULL) << 32U;
    low |= GDT_TSS_AVAILABLE_ACCESS << 40U;
    low |= ((uint64_t)((limit >> 16U) & 0x0fU)) << 48U;
    low |= (((uint64_t)base >> 24U) & 0xffULL) << 56U;

    gdt[5] = low;
    gdt[6] = ((uint64_t)base >> 32U) & 0xffffffffULL;
}

bool descriptor_init(void) {
    struct x86_64_gdtr requested;
    struct x86_64_gdtr active;
    const uintptr_t stack_base = (uintptr_t)&rsp0_stack[0];
    const uintptr_t stack_top =
        stack_base + (uintptr_t)X86_64_TSS_RSP0_STACK_SIZE;
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();
    uint16_t active_tr;

    if (descriptor_initialized) {
        return true;
    }

    x86_64_interrupts_disable();
    memory_zero(gdt, sizeof(gdt));
    memory_zero(&tss, sizeof(tss));
    memory_zero(&descriptor_state, sizeof(descriptor_state));

    gdt[0] = 0ULL;
    gdt[1] = GDT_KERNEL_CODE_DESCRIPTOR;
    gdt[2] = GDT_KERNEL_DATA_DESCRIPTOR;
    gdt[3] = GDT_USER_DATA_DESCRIPTOR;
    gdt[4] = GDT_USER_CODE_DESCRIPTOR;

    tss.rsp0 = (uint64_t)stack_top;
    tss.iomap_base = (uint16_t)sizeof(tss);
    gdt_set_tss_descriptor((uintptr_t)&tss,
                           (uint32_t)(sizeof(tss) - 1U));

    requested.limit = (uint16_t)(sizeof(gdt) - 1U);
    requested.base = (uint64_t)(uintptr_t)&gdt[0];
    active.limit = 0U;
    active.base = 0ULL;

    x86_64_load_gdt(&requested,
                     (uint16_t)X86_64_GDT_KERNEL_CODE_SELECTOR,
                     (uint16_t)X86_64_GDT_KERNEL_DATA_SELECTOR);
    x86_64_store_gdt(&active);

    if ((active.limit != requested.limit) ||
        (active.base != requested.base) ||
        (x86_64_read_cs() !=
         (uint16_t)X86_64_GDT_KERNEL_CODE_SELECTOR) ||
        (x86_64_read_ss() !=
         (uint16_t)X86_64_GDT_KERNEL_DATA_SELECTOR) ||
        (x86_64_read_ds() !=
         (uint16_t)X86_64_GDT_KERNEL_DATA_SELECTOR)) {
        if (interrupts_were_enabled) {
            x86_64_interrupts_enable();
        }
        return false;
    }

    x86_64_load_tr((uint16_t)X86_64_GDT_TSS_SELECTOR);
    active_tr = x86_64_store_tr();
    if (active_tr != (uint16_t)X86_64_GDT_TSS_SELECTOR) {
        if (interrupts_were_enabled) {
            x86_64_interrupts_enable();
        }
        return false;
    }

    descriptor_state.gdtr_base = (uintptr_t)active.base;
    descriptor_state.gdtr_limit = active.limit;
    descriptor_state.kernel_code_selector =
        (uint16_t)X86_64_GDT_KERNEL_CODE_SELECTOR;
    descriptor_state.kernel_data_selector =
        (uint16_t)X86_64_GDT_KERNEL_DATA_SELECTOR;
    descriptor_state.user_code_selector =
        (uint16_t)X86_64_GDT_USER_CODE_SELECTOR;
    descriptor_state.user_data_selector =
        (uint16_t)X86_64_GDT_USER_DATA_SELECTOR;
    descriptor_state.tss_selector =
        (uint16_t)X86_64_GDT_TSS_SELECTOR;
    descriptor_state.task_register = active_tr;
    descriptor_state.tss_rsp0 = stack_top;
    descriptor_state.rsp0_stack_base = stack_base;
    descriptor_state.rsp0_stack_top = stack_top;
    descriptor_state.gdt_entries = (uint16_t)X86_64_GDT_ENTRY_COUNT;
    descriptor_initialized = true;

    if (interrupts_were_enabled) {
        x86_64_interrupts_enable();
    }
    return true;
}

bool descriptor_get_stats(struct descriptor_stats *stats) {
    if ((!descriptor_initialized) || (stats == NULL)) {
        return false;
    }

    *stats = descriptor_state;
    return true;
}

bool descriptor_rsp0_stack_contains(uintptr_t stack_pointer) {
    if (!descriptor_initialized) {
        return false;
    }

    return (stack_pointer >= descriptor_state.rsp0_stack_base) &&
           (stack_pointer < descriptor_state.rsp0_stack_top);
}
