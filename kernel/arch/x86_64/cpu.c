#include <boring/cpu.h>

uint64_t x86_64_read_cr3(void) {
    uint64_t value;

    /* CR3 is the physical base of the currently active x86_64 root table. */
    __asm__ volatile ("mov %%cr3, %0" : "=r" (value));
    return value;
}

void x86_64_write_cr3(uint64_t physical_root) {
    /* Loading CR3 switches address spaces and broadly invalidates the TLB. */
    __asm__ volatile ("mov %0, %%cr3" : : "r" (physical_root) : "memory");
}

uint64_t x86_64_read_rflags(void) {
    uint64_t value;

    __asm__ volatile ("pushfq; popq %0" : "=r" (value));
    return value;
}

uint16_t x86_64_read_ss(void) {
    uint16_t value;

    __asm__ volatile ("mov %%ss, %0" : "=r" (value));
    return value;
}

uintptr_t x86_64_read_rsp(void) {
    uintptr_t value;

    __asm__ volatile ("mov %%rsp, %0" : "=r" (value));
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
    return (x86_64_read_rflags() & (1ULL << 9)) != 0ULL;
}

void x86_64_pause(void) {
    __asm__ volatile ("pause");
}

void x86_64_enable_and_halt(void) {
    __asm__ volatile ("sti; hlt" : : : "memory");
}

void x86_64_halt_forever(void) {
    x86_64_interrupts_disable();

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
