#ifndef BORING_CPU_INVENTORY_H
#define BORING_CPU_INVENTORY_H
#include <stdbool.h>
#include <stdint.h>

struct boring_cpuid_regs { uint32_t eax, ebx, ecx, edx; };
typedef void (*boring_cpuid_query)(void *context, uint32_t leaf, uint32_t subleaf,
                                  struct boring_cpuid_regs *out);

/* Kernel-owned boot CPU inventory, not a userspace ABI. Feature bits describe
 * CPU advertisement, not OS enablement. Leaf 1 logical count is a maximum
 * addressable count per package, never an online CPU count or core count. */
struct boring_cpu_inventory {
    char vendor[13], brand[49];
    uint32_t max_basic, max_extended, signature;
    uint32_t family, model, stepping, initial_apic_id;
    uint32_t leaf1_ebx, leaf1_ecx, leaf1_edx;
    uint32_t leaf7_ebx, leaf7_ecx, leaf7_edx;
    uint32_t ext1_ecx, ext1_edx, logical_per_package_max;
    bool leaf1_valid, leaf7_valid, ext1_valid, brand_valid;
};

void boring_cpu_inventory_collect(struct boring_cpu_inventory *info,
                                  boring_cpuid_query query, void *context);
void boring_cpu_inventory_init(void);
const struct boring_cpu_inventory *boring_cpu_inventory_get(void);

#endif
