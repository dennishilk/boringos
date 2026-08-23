#ifndef BORING_DESCRIPTOR_H
#define BORING_DESCRIPTOR_H

#include <stdbool.h>
#include <stdint.h>

#define X86_64_GDT_ENTRY_COUNT 7U
#define X86_64_GDT_KERNEL_CODE_SELECTOR 0x08U
#define X86_64_GDT_KERNEL_DATA_SELECTOR 0x10U
#define X86_64_GDT_USER_DATA_SELECTOR 0x1BU
#define X86_64_GDT_USER_CODE_SELECTOR 0x23U
#define X86_64_GDT_TSS_SELECTOR 0x28U
#define X86_64_TSS_RSP0_STACK_SIZE 16384U

struct descriptor_stats {
    uintptr_t gdtr_base;
    uintptr_t tss_rsp0;
    uintptr_t rsp0_stack_base;
    uintptr_t rsp0_stack_top;
    uint16_t gdtr_limit;
    uint16_t kernel_code_selector;
    uint16_t kernel_data_selector;
    uint16_t user_code_selector;
    uint16_t user_data_selector;
    uint16_t tss_selector;
    uint16_t task_register;
    uint16_t gdt_entries;
};

bool descriptor_init(void);
bool descriptor_get_stats(struct descriptor_stats *stats);
bool descriptor_rsp0_stack_contains(uintptr_t stack_pointer);

#endif
