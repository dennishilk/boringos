#ifndef BORING_EXCEPTION_H
#define BORING_EXCEPTION_H

#include <stdbool.h>
#include <stdint.h>

#define X86_64_EXCEPTION_VECTOR_COUNT 32U
#define X86_64_IDT_ENTRY_COUNT 256U

struct x86_64_trap_frame {
    /* C-facing copies of the interrupted stack state. */
    uint64_t rsp;
    uint64_t ss;

    /* GPRs saved by the BoringKernel assembly entry stub. */
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;

    /* Normalized BoringKernel fields. */
    uint64_t vector;
    uint64_t error_code;

    /* Long-mode hardware interrupt/exception return frame. */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t hardware_rsp;
    uint64_t hardware_ss;
};

struct exception_stats {
    uintptr_t idtr_base;
    uint16_t idtr_limit;
    uint16_t code_selector;
    uint16_t configured_vectors;
};

bool exception_init(void);
bool exception_get_stats(struct exception_stats *stats);
bool exception_install_interrupt_gate(uint8_t vector, uintptr_t handler);
void x86_64_exception_dispatch(const struct x86_64_trap_frame *frame)
    __attribute__((noreturn));
void x86_64_trigger_divide_error(void) __attribute__((noreturn));
void x86_64_trigger_page_fault(uintptr_t address) __attribute__((noreturn));

#endif
