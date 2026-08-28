#include <stddef.h>
#include <boring/cpu_inventory.h>

static void word_chars(char *out, uint32_t value) {
    for (unsigned i = 0U; i < 4U; ++i) {
        const uint32_t byte = (value >> (i * 8U)) & 255U;
        out[i] = (byte == 0U || (byte >= 32U && byte <= 126U)) ? (char)byte : '?';
    }
}

void boring_cpu_inventory_collect(struct boring_cpu_inventory *info,
                                  boring_cpuid_query query, void *context) {
    struct boring_cpuid_regs r;
    unsigned char *bytes = (unsigned char *)info;
    for (size_t i = 0U; i < sizeof(*info); ++i) { bytes[i] = 0U; }
    query(context, 0U, 0U, &r);
    info->max_basic = r.eax;
    word_chars(info->vendor, r.ebx);
    word_chars(info->vendor + 4, r.edx);
    word_chars(info->vendor + 8, r.ecx);
    query(context, 0x80000000U, 0U, &r);
    info->max_extended = r.eax;
    if (info->max_basic >= 1U) {
        uint32_t base_family, base_model;
        query(context, 1U, 0U, &r);
        info->leaf1_valid = true;
        info->signature = r.eax;
        base_family = (r.eax >> 8U) & 15U;
        base_model = (r.eax >> 4U) & 15U;
        info->family = base_family + (base_family == 15U ? (r.eax >> 20U) & 255U : 0U);
        info->model = base_model | ((base_family == 6U || base_family == 15U) ?
                                    ((r.eax >> 16U) & 15U) << 4U : 0U);
        info->stepping = r.eax & 15U;
        info->initial_apic_id = r.ebx >> 24U;
        info->leaf1_ebx = r.ebx; info->leaf1_ecx = r.ecx; info->leaf1_edx = r.edx;
        info->logical_per_package_max = (r.edx & (1U << 28U)) != 0U ?
                                        (r.ebx >> 16U) & 255U : 1U;
    }
    if (info->max_basic >= 7U) {
        query(context, 7U, 0U, &r);
        info->leaf7_valid = true;
        info->leaf7_ebx = r.ebx; info->leaf7_ecx = r.ecx; info->leaf7_edx = r.edx;
    }
    if (info->max_extended >= 0x80000001U) {
        query(context, 0x80000001U, 0U, &r);
        info->ext1_valid = true; info->ext1_ecx = r.ecx; info->ext1_edx = r.edx;
    }
    if (info->max_extended >= 0x80000004U) {
        for (uint32_t i = 0U; i < 3U; ++i) {
            query(context, 0x80000002U + i, 0U, &r);
            word_chars(info->brand + i * 16U, r.eax);
            word_chars(info->brand + i * 16U + 4U, r.ebx);
            word_chars(info->brand + i * 16U + 8U, r.ecx);
            word_chars(info->brand + i * 16U + 12U, r.edx);
        }
        info->brand_valid = true;
    }
}
