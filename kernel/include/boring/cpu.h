#ifndef BORING_CPU_H
#define BORING_CPU_H

#include <stdbool.h>
#include <stdint.h>

uint64_t x86_64_read_cr3(void);
uint64_t x86_64_read_rflags(void);
uint16_t x86_64_read_ss(void);
void x86_64_invalidate_page(uintptr_t virtual_address);
void x86_64_interrupts_disable(void);
void x86_64_interrupts_enable(void);
bool x86_64_interrupts_enabled(void);
void x86_64_pause(void);
void x86_64_enable_and_halt(void);
void x86_64_halt_forever(void) __attribute__((noreturn));

#endif
