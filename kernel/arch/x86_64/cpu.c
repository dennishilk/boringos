#include <boring/cpu.h>

uint64_t x86_64_read_cr3(void) {
    uint64_t value;

    /* CR3 is the physical base of the currently active x86_64 root table. */
    __asm__ volatile ("mov %%cr3, %0" : "=r" (value));
    return value;
}

void x86_64_invalidate_page(uintptr_t virtual_address) {
    /* invlpg invalidates the current address-space translation for one page. */
    __asm__ volatile ("invlpg (%0)" : : "r" (virtual_address) : "memory");
}

void x86_64_interrupts_disable(void) {
    __asm__ volatile ("cli" : : : "memory");
}

void x86_64_interrupts_enable(void) {
    __asm__ volatile ("sti" : : : "memory");
}

bool x86_64_interrupts_enabled(void) {
    uint64_t rflags;

    __asm__ volatile ("pushfq; popq %0" : "=r" (rflags));
    return (rflags & (1ULL << 9)) != 0ULL;
}

void x86_64_pause(void) {
    __asm__ volatile ("pause");
}

void x86_64_halt_forever(void) {
    x86_64_interrupts_disable();

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
