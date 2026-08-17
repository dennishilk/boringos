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

void x86_64_halt_forever(void) {
    /* cli/hlt are privileged CPU instructions and require tiny inline assembly. */
    __asm__ volatile ("cli");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
