#include <boring/cpu_inventory.h>
#include <boring/serial.h>

static struct boring_cpu_inventory boot_cpu;

static void native_query(void *context, uint32_t leaf, uint32_t subleaf,
                         struct boring_cpuid_regs *out) {
    (void)context;
    __asm__ volatile ("cpuid" : "=a"(out->eax), "=b"(out->ebx),
                     "=c"(out->ecx), "=d"(out->edx) : "a"(leaf), "c"(subleaf));
}

static void number(const char *name, uint32_t value) {
    serial_write_string("cpu-inventory: "); serial_write_string(name);
    serial_write_string("="); serial_write_u64(value); serial_write_string("\n");
}
static void bits(const char *name, uint32_t value) {
    serial_write_string("cpu-inventory: "); serial_write_string(name);
    serial_write_string("="); serial_write_hex_u64(value); serial_write_string("\n");
}

void boring_cpu_inventory_init(void) {
    boring_cpu_inventory_collect(&boot_cpu, native_query, (void *)0);
    serial_write_string("cpu-inventory: vendor=");
    serial_write_string(boot_cpu.vendor); serial_write_string("\n");
    serial_write_string("cpu-inventory: brand=");
    serial_write_string(boot_cpu.brand_valid ? boot_cpu.brand : "unavailable");
    serial_write_string("\n");
    bits("max_basic", boot_cpu.max_basic); bits("max_extended", boot_cpu.max_extended);
    number("leaf1_valid", boot_cpu.leaf1_valid ? 1U : 0U);
    number("leaf7_valid", boot_cpu.leaf7_valid ? 1U : 0U);
    number("ext1_valid", boot_cpu.ext1_valid ? 1U : 0U);
    number("brand_valid", boot_cpu.brand_valid ? 1U : 0U);
    bits("signature", boot_cpu.signature);
    number("family", boot_cpu.family); number("model", boot_cpu.model);
    number("stepping", boot_cpu.stepping);
    number("initial_apic_id", boot_cpu.initial_apic_id);
    number("logical_per_package_max", boot_cpu.logical_per_package_max);
    bits("leaf1_ebx", boot_cpu.leaf1_ebx); bits("leaf1_ecx", boot_cpu.leaf1_ecx);
    bits("leaf1_edx", boot_cpu.leaf1_edx); bits("leaf7_ebx", boot_cpu.leaf7_ebx);
    bits("leaf7_ecx", boot_cpu.leaf7_ecx); bits("leaf7_edx", boot_cpu.leaf7_edx);
    bits("ext1_ecx", boot_cpu.ext1_ecx); bits("ext1_edx", boot_cpu.ext1_edx);
    serial_write_string("cpu-inventory: boot CPU CPUID collection complete; advertised features only\n");
}

const struct boring_cpu_inventory *boring_cpu_inventory_get(void) { return &boot_cpu; }
