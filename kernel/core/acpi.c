#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/acpi.h>
#include <boring/acpi_s5.h>
#include <boring/cpu.h>
#include <boring/io.h>
#include <boring/vmm.h>

#define ACPI_SDT_HEADER_SIZE 36U
#define ACPI_TABLE_LENGTH_MAX (1024U * 1024U)
#define ACPI_ROOT_ENTRY_MAX 256U
#define ACPI_GAS_SYSTEM_MEMORY 0U
#define ACPI_GAS_SYSTEM_IO 1U
#define ACPI_GAS_ACCESS_UNDEFINED 0U
#define ACPI_GAS_ACCESS_BYTE 1U
#define ACPI_GAS_ACCESS_WORD 2U
#define ACPI_FADT_FLAG_RESET_REG_SUP (1U << 10)
#define ACPI_FADT_FLAG_HARDWARE_REDUCED (1U << 20)
#define ACPI_PM1_SCI_EN (1U << 0)
#define ACPI_PM1_SLP_TYP_SHIFT 10U
#define ACPI_PM1_SLP_TYP_MASK (7U << ACPI_PM1_SLP_TYP_SHIFT)
#define ACPI_PM1_SLP_EN (1U << 13)
#define ACPI_ENABLE_SPIN_LIMIT 10000000U
#define ACPI_TRANSITION_SPIN_LIMIT 1000000U

struct acpi_gas {
    uint8_t space_id;
    uint8_t bit_width;
    uint8_t bit_offset;
    uint8_t access_size;
    uint64_t address;
};

struct acpi_runtime {
    struct boring_acpi_stats stats;
    const struct boring_limine_hhdm_response *hhdm;
    const struct boring_limine_memmap_response *memory_map;
    struct acpi_gas reset_register;
    struct acpi_gas pm1a_control;
    struct acpi_gas pm1b_control;
    uint8_t reset_value;
    uint8_t sleep_type_a;
    uint8_t sleep_type_b;
    uint32_t smi_command;
    uint8_t acpi_enable;
    bool pm1b_present;
};

static struct acpi_runtime runtime;

static uint16_t read_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] |
                      ((uint16_t)bytes[1] << 8U));
}

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) |
           ((uint32_t)bytes[3] << 24U);
}

static uint64_t read_le64(const uint8_t *bytes) {
    return (uint64_t)read_le32(bytes) |
           ((uint64_t)read_le32(&bytes[4]) << 32U);
}

static void runtime_zero(void) {
    uint8_t *bytes = (uint8_t *)&runtime;
    size_t index;

    for (index = 0U; index < sizeof(runtime); ++index) {
        bytes[index] = 0U;
    }
}

static bool signature_equal(const uint8_t *bytes, const char *signature) {
    size_t index;

    if ((bytes == NULL) || (signature == NULL)) {
        return false;
    }
    for (index = 0U; index < 4U; ++index) {
        if (bytes[index] != (uint8_t)signature[index]) {
            return false;
        }
    }
    return true;
}

static bool checksum_ok(const uint8_t *bytes, size_t length) {
    uint8_t sum = 0U;
    size_t index;

    if ((bytes == NULL) || (length == 0U)) {
        return false;
    }
    for (index = 0U; index < length; ++index) {
        sum = (uint8_t)(sum + bytes[index]);
    }
    return sum == 0U;
}

static bool mapped_type(uint64_t type) {
    return (type == BORING_LIMINE_MEMMAP_RESERVED) ||
           (type == BORING_LIMINE_MEMMAP_ACPI_RECLAIMABLE) ||
           (type == BORING_LIMINE_MEMMAP_ACPI_NVS) ||
           (type == BORING_LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE) ||
           (type == BORING_LIMINE_MEMMAP_RESERVED_MAPPED);
}

static bool physical_range_mapped(uint64_t physical, size_t length) {
    uint64_t range_end;
    uint64_t index;

    if ((runtime.memory_map == NULL) || (length == 0U) ||
        ((uint64_t)length > UINT64_MAX - physical)) {
        return false;
    }
    range_end = physical + (uint64_t)length;
    for (index = 0ULL; index < runtime.memory_map->entry_count; ++index) {
        const struct boring_limine_memmap_entry *entry =
            runtime.memory_map->entries[index];
        uint64_t entry_end;

        if ((entry == NULL) || !mapped_type(entry->type) ||
            (entry->length > UINT64_MAX - entry->base)) {
            continue;
        }
        entry_end = entry->base + entry->length;
        if ((physical >= entry->base) && (range_end <= entry_end)) {
            return true;
        }
    }
    return false;
}

static const uint8_t *physical_pointer(uint64_t physical, size_t length) {
    uint64_t mapped;

    if ((runtime.hhdm == NULL) ||
        !physical_range_mapped(physical, length) ||
        (physical > UINT64_MAX - runtime.hhdm->offset)) {
        return NULL;
    }
    mapped = runtime.hhdm->offset + physical;
    if (mapped > (uint64_t)UINTPTR_MAX) {
        return NULL;
    }
    return (const uint8_t *)(uintptr_t)mapped;
}

static bool table_from_physical(uint64_t physical,
                                const char *signature,
                                const uint8_t **table_out,
                                uint32_t *length_out) {
    const uint8_t *header;
    const uint8_t *table;
    uint32_t length;

    if ((table_out == NULL) || (length_out == NULL)) {
        return false;
    }
    header = physical_pointer(physical, ACPI_SDT_HEADER_SIZE);
    if ((header == NULL) ||
        ((signature != NULL) && !signature_equal(header, signature))) {
        return false;
    }
    length = read_le32(&header[4]);
    if ((length < ACPI_SDT_HEADER_SIZE) ||
        (length > ACPI_TABLE_LENGTH_MAX)) {
        return false;
    }
    table = physical_pointer(physical, (size_t)length);
    if ((table == NULL) ||
        ((signature != NULL) && !signature_equal(table, signature)) ||
        !checksum_ok(table, (size_t)length)) {
        return false;
    }
    *table_out = table;
    *length_out = length;
    return true;
}

static bool rsdp_valid(const uint8_t *rsdp,
                       uint64_t *xsdt_physical,
                       uint64_t *rsdt_physical) {
    size_t index;

    if ((rsdp == NULL) || (xsdt_physical == NULL) || (rsdt_physical == NULL)) {
        return false;
    }
    for (index = 0U; index < 8U; ++index) {
        static const char signature[8] = {
            'R', 'S', 'D', ' ', 'P', 'T', 'R', ' '
        };
        if (rsdp[index] != (uint8_t)signature[index]) {
            return false;
        }
    }
    if (!checksum_ok(rsdp, 20U)) {
        return false;
    }
    *rsdt_physical = (uint64_t)read_le32(&rsdp[16]);
    *xsdt_physical = 0ULL;
    if (rsdp[15] >= 2U) {
        const uint32_t length = read_le32(&rsdp[20]);
        if ((length < 36U) || (length > 4096U) ||
            !checksum_ok(rsdp, (size_t)length)) {
            return false;
        }
        *xsdt_physical = read_le64(&rsdp[24]);
    }
    return (*xsdt_physical != 0ULL) || (*rsdt_physical != 0ULL);
}

static bool find_fadt(uint64_t xsdt_physical,
                      uint64_t rsdt_physical,
                      const uint8_t **fadt,
                      uint32_t *fadt_length) {
    const uint8_t *root = NULL;
    uint32_t root_length = 0U;
    size_t width;
    size_t count;
    size_t index;

    if ((xsdt_physical != 0ULL) &&
        table_from_physical(xsdt_physical, "XSDT", &root, &root_length)) {
        width = 8U;
    } else if ((rsdt_physical != 0ULL) &&
               table_from_physical(rsdt_physical, "RSDT", &root,
                                   &root_length)) {
        width = 4U;
    } else {
        return false;
    }
    count = ((size_t)root_length - ACPI_SDT_HEADER_SIZE) / width;
    if (count > ACPI_ROOT_ENTRY_MAX) {
        count = ACPI_ROOT_ENTRY_MAX;
    }
    for (index = 0U; index < count; ++index) {
        const uint8_t *entry =
            &root[ACPI_SDT_HEADER_SIZE + (index * width)];
        const uint64_t physical = (width == 8U) ?
            read_le64(entry) : (uint64_t)read_le32(entry);
        const uint8_t *candidate = NULL;
        uint32_t candidate_length = 0U;

        if ((physical != 0ULL) &&
            table_from_physical(physical, NULL,
                                &candidate, &candidate_length) &&
            signature_equal(candidate, "FACP")) {
            *fadt = candidate;
            *fadt_length = candidate_length;
            return true;
        }
    }
    return false;
}

static struct acpi_gas gas_from_bytes(const uint8_t *bytes) {
    struct acpi_gas gas;

    gas.space_id = bytes[0];
    gas.bit_width = bytes[1];
    gas.bit_offset = bytes[2];
    gas.access_size = bytes[3];
    gas.address = read_le64(&bytes[4]);
    return gas;
}

static struct acpi_gas legacy_io_gas(uint32_t address, uint8_t bit_width) {
    struct acpi_gas gas;

    gas.space_id = ACPI_GAS_SYSTEM_IO;
    gas.bit_width = bit_width;
    gas.bit_offset = 0U;
    gas.access_size = (bit_width == 8U) ?
        ACPI_GAS_ACCESS_BYTE : ACPI_GAS_ACCESS_WORD;
    gas.address = (uint64_t)address;
    return gas;
}

static bool gas_address_supported(const struct acpi_gas *gas,
                                  size_t access_bytes) {
    if ((gas == NULL) || (gas->address == 0ULL) ||
        (gas->bit_offset != 0U) ||
        ((gas->address & (uint64_t)(access_bytes - 1U)) != 0ULL)) {
        return false;
    }
    if (gas->space_id == ACPI_GAS_SYSTEM_IO) {
        return gas->address <= (uint64_t)UINT16_MAX;
    }
    return gas->space_id == ACPI_GAS_SYSTEM_MEMORY;
}

static bool gas_usable8(const struct acpi_gas *gas) {
    return gas_address_supported(gas, 1U) &&
           ((gas->bit_width == 0U) || (gas->bit_width >= 8U)) &&
           ((gas->access_size == ACPI_GAS_ACCESS_UNDEFINED) ||
            (gas->access_size == ACPI_GAS_ACCESS_BYTE));
}

static bool gas_usable16(const struct acpi_gas *gas) {
    return gas_address_supported(gas, 2U) &&
           ((gas->bit_width == 0U) || (gas->bit_width >= 16U)) &&
           ((gas->access_size == ACPI_GAS_ACCESS_UNDEFINED) ||
            (gas->access_size == ACPI_GAS_ACCESS_WORD));
}

static bool gas_read16(const struct acpi_gas *gas, uint16_t *value) {
    volatile void *mapping = NULL;

    if ((value == NULL) || !gas_usable16(gas)) {
        return false;
    }
    if (gas->space_id == ACPI_GAS_SYSTEM_IO) {
        *value = x86_64_in16((uint16_t)gas->address);
        return true;
    }
    if (!vmm_map_mmio_region(gas->address, sizeof(uint16_t), &mapping)) {
        return false;
    }
    *value = *(volatile uint16_t *)mapping;
    if (!vmm_unmap_mmio_region(mapping, sizeof(uint16_t))) {
        return false;
    }
    return true;
}

static bool gas_write16(const struct acpi_gas *gas, uint16_t value) {
    volatile void *mapping = NULL;

    if (!gas_usable16(gas)) {
        return false;
    }
    if (gas->space_id == ACPI_GAS_SYSTEM_IO) {
        x86_64_out16((uint16_t)gas->address, value);
        x86_64_memory_barrier();
        return true;
    }
    if (!vmm_map_mmio_region(gas->address, sizeof(uint16_t), &mapping)) {
        return false;
    }
    *(volatile uint16_t *)mapping = value;
    x86_64_memory_barrier();
    return vmm_unmap_mmio_region(mapping, sizeof(uint16_t));
}

static bool gas_write8(const struct acpi_gas *gas, uint8_t value) {
    volatile void *mapping = NULL;

    if (!gas_usable8(gas)) {
        return false;
    }
    if (gas->space_id == ACPI_GAS_SYSTEM_IO) {
        x86_64_out8((uint16_t)gas->address, value);
        x86_64_memory_barrier();
        return true;
    }
    if (!vmm_map_mmio_region(gas->address, sizeof(uint8_t), &mapping)) {
        return false;
    }
    *(volatile uint8_t *)mapping = value;
    x86_64_memory_barrier();
    return vmm_unmap_mmio_region(mapping, sizeof(uint8_t));
}

static bool choose_pm1(const uint8_t *fadt,
                       uint32_t fadt_length,
                       size_t extended_offset,
                       uint32_t legacy_address,
                       bool required,
                       struct acpi_gas *gas,
                       bool *present) {
    struct acpi_gas candidate;

    if ((gas == NULL) || (present == NULL)) {
        return false;
    }
    *present = false;
    if (fadt_length >= extended_offset + 12U) {
        candidate = gas_from_bytes(&fadt[extended_offset]);
        if (candidate.address != 0ULL) {
            if (!gas_usable16(&candidate)) {
                return false;
            }
            *gas = candidate;
            *present = true;
            return true;
        }
    }
    if (legacy_address != 0U) {
        candidate = legacy_io_gas(legacy_address, 16U);
        if (!gas_usable16(&candidate)) {
            return false;
        }
        *gas = candidate;
        *present = true;
        return true;
    }
    return !required;
}

static bool discover_fadt(const uint8_t *fadt, uint32_t length) {
    uint32_t flags = 0U;
    uint32_t legacy_dsdt;
    uint64_t dsdt_physical;
    uint32_t legacy_pm1a;
    uint32_t legacy_pm1b;
    uint8_t pm1_length;
    bool pm1a_present = false;
    const uint8_t *dsdt = NULL;
    uint32_t dsdt_length = 0U;

    if ((fadt == NULL) || (length < 90U)) {
        return false;
    }
    legacy_dsdt = read_le32(&fadt[40]);
    runtime.smi_command = read_le32(&fadt[48]);
    runtime.acpi_enable = fadt[52];
    legacy_pm1a = read_le32(&fadt[64]);
    legacy_pm1b = read_le32(&fadt[68]);
    pm1_length = fadt[89];
    if ((pm1_length != 0U) && (pm1_length < 2U)) {
        return false;
    }
    if (length >= 116U) {
        flags = read_le32(&fadt[112]);
    }
    runtime.stats.hardware_reduced =
        (flags & ACPI_FADT_FLAG_HARDWARE_REDUCED) != 0U;

    if ((length >= 129U) &&
        ((flags & ACPI_FADT_FLAG_RESET_REG_SUP) != 0U)) {
        runtime.reset_register = gas_from_bytes(&fadt[116]);
        runtime.reset_value = fadt[128];
        runtime.stats.reset_supported =
            gas_usable8(&runtime.reset_register);
    }

    if (runtime.stats.hardware_reduced) {
        return true;
    }

    if (!choose_pm1(fadt, length, 172U, legacy_pm1a, true,
                    &runtime.pm1a_control, &pm1a_present) ||
        !pm1a_present ||
        !choose_pm1(fadt, length, 184U, legacy_pm1b, false,
                    &runtime.pm1b_control, &runtime.pm1b_present)) {
        return true;
    }

    dsdt_physical = (uint64_t)legacy_dsdt;
    if (length >= 148U) {
        const uint64_t extended = read_le64(&fadt[140]);
        if (extended != 0ULL) {
            dsdt_physical = extended;
        }
    }
    if ((dsdt_physical == 0ULL) ||
        !table_from_physical(dsdt_physical, "DSDT", &dsdt, &dsdt_length) ||
        (dsdt_length <= ACPI_SDT_HEADER_SIZE) ||
        !boring_acpi_s5_parse(&dsdt[ACPI_SDT_HEADER_SIZE],
                              (size_t)dsdt_length - ACPI_SDT_HEADER_SIZE,
                              &runtime.sleep_type_a,
                              &runtime.sleep_type_b)) {
        return true;
    }
    runtime.stats.s5_supported = true;
    return true;
}

bool boring_acpi_boot_init(
    const struct boring_limine_rsdp_response *rsdp,
    const struct boring_limine_hhdm_response *hhdm,
    const struct boring_limine_memmap_response *memory_map) {
    const uint8_t *fadt = NULL;
    uint32_t fadt_length = 0U;
    uint64_t xsdt_physical = 0ULL;
    uint64_t rsdt_physical = 0ULL;

    runtime_zero();
    if ((rsdp == NULL) || (rsdp->address == NULL) ||
        (hhdm == NULL) || (memory_map == NULL) ||
        (memory_map->entries == NULL)) {
        return false;
    }
    runtime.hhdm = hhdm;
    runtime.memory_map = memory_map;
    if (!rsdp_valid((const uint8_t *)rsdp->address,
                    &xsdt_physical, &rsdt_physical)) {
        return false;
    }
    runtime.stats.rsdp_valid = true;
    if (!find_fadt(xsdt_physical, rsdt_physical,
                   &fadt, &fadt_length)) {
        return false;
    }
    runtime.stats.fadt_found = true;
    if (!discover_fadt(fadt, fadt_length)) {
        return false;
    }
    runtime.stats.initialized = true;
    return true;
}

bool boring_acpi_get_stats(struct boring_acpi_stats *stats) {
    if (stats == NULL) {
        return false;
    }
    *stats = runtime.stats;
    return true;
}

static bool acpi_enable_if_needed(void) {
    uint16_t control;
    uint32_t spin;

    if (!gas_read16(&runtime.pm1a_control, &control)) {
        return false;
    }
    if ((control & ACPI_PM1_SCI_EN) != 0U) {
        return true;
    }
    if ((runtime.smi_command == 0U) ||
        (runtime.smi_command > (uint32_t)UINT16_MAX) ||
        (runtime.acpi_enable == 0U)) {
        return false;
    }
    x86_64_out8((uint16_t)runtime.smi_command, runtime.acpi_enable);
    for (spin = 0U; spin < ACPI_ENABLE_SPIN_LIMIT; ++spin) {
        if (gas_read16(&runtime.pm1a_control, &control) &&
            ((control & ACPI_PM1_SCI_EN) != 0U)) {
            return true;
        }
        x86_64_pause();
    }
    return false;
}

bool boring_acpi_reset(void) {
    uint32_t spin;

    if (!runtime.stats.initialized ||
        !runtime.stats.reset_supported ||
        !gas_write8(&runtime.reset_register, runtime.reset_value)) {
        return false;
    }
    for (spin = 0U; spin < ACPI_TRANSITION_SPIN_LIMIT; ++spin) {
        x86_64_pause();
    }
    return false;
}

bool boring_acpi_poweroff(void) {
    uint16_t control_a;
    uint16_t control_b = 0U;
    uint16_t sleep_a;
    uint16_t sleep_b;
    uint32_t spin;

    if (!runtime.stats.initialized || !runtime.stats.s5_supported ||
        runtime.stats.hardware_reduced || !acpi_enable_if_needed() ||
        !gas_read16(&runtime.pm1a_control, &control_a) ||
        (runtime.pm1b_present &&
         !gas_read16(&runtime.pm1b_control, &control_b))) {
        return false;
    }
    sleep_a = (uint16_t)(
        (control_a & (uint16_t)~(ACPI_PM1_SLP_TYP_MASK | ACPI_PM1_SLP_EN)) |
        ((uint16_t)runtime.sleep_type_a << ACPI_PM1_SLP_TYP_SHIFT) |
        ACPI_PM1_SLP_EN);
    sleep_b = (uint16_t)(
        (control_b & (uint16_t)~(ACPI_PM1_SLP_TYP_MASK | ACPI_PM1_SLP_EN)) |
        ((uint16_t)runtime.sleep_type_b << ACPI_PM1_SLP_TYP_SHIFT) |
        ACPI_PM1_SLP_EN);

    if (!gas_write16(&runtime.pm1a_control, sleep_a)) {
        return false;
    }
    if (runtime.pm1b_present &&
        !gas_write16(&runtime.pm1b_control, sleep_b)) {
        return false;
    }
    for (spin = 0U; spin < ACPI_TRANSITION_SPIN_LIMIT; ++spin) {
        x86_64_pause();
    }
    return false;
}
