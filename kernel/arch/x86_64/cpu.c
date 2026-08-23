#include <boring/cpu.h>

static void x86_64_cpuid(uint32_t leaf,
                         uint32_t subleaf,
                         uint32_t *eax,
                         uint32_t *ebx,
                         uint32_t *ecx,
                         uint32_t *edx) {
    uint32_t out_a;
    uint32_t out_b;
    uint32_t out_c;
    uint32_t out_d;

    __asm__ volatile (
        "cpuid"
        : "=a" (out_a), "=b" (out_b), "=c" (out_c), "=d" (out_d)
        : "a" (leaf), "c" (subleaf));

    if (eax != 0) {
        *eax = out_a;
    }
    if (ebx != 0) {
        *ebx = out_b;
    }
    if (ecx != 0) {
        *ecx = out_c;
    }
    if (edx != 0) {
        *edx = out_d;
    }
}

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

bool x86_64_syscall_supported(void) {
    uint32_t max_extended;
    uint32_t edx;

    x86_64_cpuid(0x80000000U, 0U, &max_extended, 0, 0, 0);
    if (max_extended < 0x80000001U) {
        return false;
    }

    x86_64_cpuid(0x80000001U, 0U, 0, 0, 0, &edx);
    return (edx & (1U << 11)) != 0U;
}

bool x86_64_nx_supported(void) {
    uint32_t max_extended;
    uint32_t edx;

    x86_64_cpuid(0x80000000U, 0U, &max_extended, 0, 0, 0);
    if (max_extended < 0x80000001U) {
        return false;
    }

    x86_64_cpuid(0x80000001U, 0U, 0, 0, 0, &edx);
    return (edx & (1U << 20)) != 0U;
}

uint64_t x86_64_read_msr(uint32_t msr) {
    uint32_t low;
    uint32_t high;

    __asm__ volatile ("rdmsr" : "=a" (low), "=d" (high) : "c" (msr));
    return ((uint64_t)high << 32U) | (uint64_t)low;
}

void x86_64_write_msr(uint32_t msr, uint64_t value) {
    const uint32_t low = (uint32_t)(value & 0xffffffffULL);
    const uint32_t high = (uint32_t)(value >> 32U);

    __asm__ volatile ("wrmsr" : : "c" (msr), "a" (low), "d" (high)
                      : "memory");
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
