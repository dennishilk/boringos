#ifndef BORING_ACPI_H
#define BORING_ACPI_H

#include <stdbool.h>

#include <boring/boot_protocol.h>

struct boring_acpi_stats {
    bool initialized;
    bool rsdp_valid;
    bool fadt_found;
    bool reset_supported;
    bool s5_supported;
    bool hardware_reduced;
};

bool boring_acpi_boot_init(
    const struct boring_limine_rsdp_response *rsdp,
    const struct boring_limine_hhdm_response *hhdm,
    const struct boring_limine_memmap_response *memory_map);
bool boring_acpi_get_stats(struct boring_acpi_stats *stats);
bool boring_acpi_reset(void);
bool boring_acpi_poweroff(void);

#endif
