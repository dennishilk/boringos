#ifndef BORING_CPU_H
#define BORING_CPU_H

#include <stdint.h>

uint64_t x86_64_read_cr3(void);
void x86_64_invalidate_page(uintptr_t virtual_address);
void x86_64_halt_forever(void) __attribute__((noreturn));

#endif
