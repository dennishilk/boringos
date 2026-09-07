#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <boring/cpu_inventory.h>

struct fixture { uint32_t basic, extended, signature, edx; unsigned calls; };
static void query(void *context, uint32_t leaf, uint32_t subleaf,
                  struct boring_cpuid_regs *r) {
    struct fixture *f = context;
    static const char brand[49] = "BoringOS fixture CPU, not a hardware result";
    assert(subleaf == 0U); ++f->calls;
    *r = (struct boring_cpuid_regs){0};
    if (leaf == 0U) {
        r->eax = f->basic;
        memcpy(&r->ebx, "Vend", 4); memcpy(&r->edx, "orTe", 4); memcpy(&r->ecx, "st12", 4);
    } else if (leaf == 0x80000000U) { r->eax = f->extended; }
    else if (leaf == 1U) {
        assert(f->basic >= leaf);
        r->eax = f->signature; r->ebx = 0xAB040000U; r->ecx = 0x76543210U; r->edx = f->edx;
    } else if (leaf == 7U) {
        assert(f->basic >= leaf);
        r->ebx = 0x12345678U; r->ecx = 0x87654321U; r->edx = 0xA5A5A5A5U;
    } else if (leaf == 0x80000001U) {
        assert(f->extended >= leaf); r->ecx = 0x55AA55AAU; r->edx = 0xAA55AA55U;
    } else {
        assert(leaf >= 0x80000002U && leaf <= 0x80000004U && f->extended >= 0x80000004U);
        const char *b = brand + (leaf - 0x80000002U) * 16U;
        memcpy(&r->eax, b, 4); memcpy(&r->ebx, b + 4, 4);
        memcpy(&r->ecx, b + 8, 4); memcpy(&r->edx, b + 12, 4);
    }
}

int main(void) {
    struct { uint64_t before; struct boring_cpu_inventory info; uint64_t after; } guarded;
    struct fixture f = {7U, 0x80000004U, 0x002A06B3U, 1U << 28U, 0U};
    memset(&guarded, 0xA5, sizeof(guarded));
    boring_cpu_inventory_collect(&guarded.info, query, &f);
    struct boring_cpu_inventory *i = &guarded.info;
    assert(guarded.before == 0xA5A5A5A5A5A5A5A5ULL && guarded.after == guarded.before);
    assert(f.calls == 8U && strcmp(i->vendor, "VendorTest12") == 0);
    assert(strcmp(i->brand, "BoringOS fixture CPU, not a hardware result") == 0);
    assert(i->vendor[12] == '\0' && i->brand[48] == '\0');
    assert(i->leaf1_valid && i->leaf7_valid && i->ext1_valid && i->brand_valid);
    assert(i->family == 6U && i->model == 0xABU && i->stepping == 3U);
    assert(i->initial_apic_id == 0xABU && i->logical_per_package_max == 4U);
    assert(i->leaf1_ecx == 0x76543210U && i->leaf7_ebx == 0x12345678U);
    assert(i->leaf7_ecx == 0x87654321U && i->leaf7_edx == 0xA5A5A5A5U);
    assert(i->ext1_ecx == 0x55AA55AAU && i->ext1_edx == 0xAA55AA55U);
    f.signature = 0x00820F51U; f.calls = 0U;
    boring_cpu_inventory_collect(i, query, &f);
    assert(i->family == 23U && i->model == 0x25U && i->stepping == 1U);
    f.signature = 0x0FFA0557U; f.edx = 0U;
    boring_cpu_inventory_collect(i, query, &f);
    assert(i->family == 5U && i->model == 5U && i->logical_per_package_max == 1U);
    f.basic = 1U; f.extended = 0x80000003U; f.calls = 0U;
    boring_cpu_inventory_collect(i, query, &f);
    assert(f.calls == 4U && i->leaf1_valid && i->ext1_valid);
    assert(!i->leaf7_valid && !i->brand_valid && i->brand[0] == '\0');
    f.basic = 0U; f.extended = 0U; f.calls = 0U;
    boring_cpu_inventory_collect(i, query, &f);
    assert(f.calls == 2U && !i->leaf1_valid && !i->ext1_valid);
    assert(i->signature == 0U && i->leaf7_ebx == 0U && i->logical_per_package_max == 0U);
    puts("CPUID inventory decoding, advertised-leaf bounds, absent data and canaries passed.");
    return 0;
}
