#ifndef BORING_IO_H
#define BORING_IO_H

#include <stdint.h>

#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
/*
 * Candidate-only shared POST state. The common definition deliberately keeps
 * this diagnostic byte available to every inlined x86_64_out8() call without
 * changing any normal kernel object or PMM API.
 */
volatile uint8_t boring_m61_pmm_false_reason_post __attribute__((common));
#endif

/* x86 I/O-port instructions cannot be expressed in ISO C. */
static inline void x86_64_out8(uint16_t port, uint8_t value) {
#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
    if (port == (uint16_t)0x80U) {
        if (value == (uint8_t)0x7aU) {
            boring_m61_pmm_false_reason_post = 0U;
        } else if ((value >= (uint8_t)0xa0U) &&
                   (value <= (uint8_t)0xabU)) {
            boring_m61_pmm_false_reason_post = value;
        }
    }
#endif

    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));

#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
    if ((port == (uint16_t)0x80U) && (value == (uint8_t)0x98U)) {
        const uint8_t reason = boring_m61_pmm_false_reason_post;

        if ((reason >= (uint8_t)0xa0U) && (reason <= (uint8_t)0xabU)) {
            __asm__ volatile ("outb %0, $0x80" : : "a"(reason));
        }
    }
#endif
}

static inline void x86_64_out16(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline void x86_64_out32(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t x86_64_in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint16_t x86_64_in16(uint16_t port) {
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint32_t x86_64_in32(uint16_t port) {
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void x86_64_memory_barrier(void) {
    __asm__ volatile ("mfence" : : : "memory");
}

#endif
