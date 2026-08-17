#ifndef BORING_IO_H
#define BORING_IO_H

#include <stdint.h>

/* x86 I/O-port instructions cannot be expressed in ISO C. */
static inline void x86_64_out8(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t x86_64_in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

#endif
