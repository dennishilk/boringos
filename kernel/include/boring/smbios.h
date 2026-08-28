#ifndef BORING_SMBIOS_H
#define BORING_SMBIOS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BORING_SMBIOS_STRING_CAP 64U
#define BORING_SMBIOS_MAX_TABLE_BYTES (1024U * 1024U)
#define BORING_SMBIOS_MAX_STRUCTURES 4096U

struct boring_limine_hhdm_response;
struct boring_limine_memmap_response;

struct boring_smbios_table {
    uint64_t physical_address;
    uint32_t length;
    uint16_t declared_structures;
    uint8_t major;
    uint8_t minor;
    bool entry_64;
};

struct boring_platform_identity {
    char firmware_vendor[BORING_SMBIOS_STRING_CAP];
    char firmware_version[BORING_SMBIOS_STRING_CAP];
    char system_manufacturer[BORING_SMBIOS_STRING_CAP];
    char system_product[BORING_SMBIOS_STRING_CAP];
    char board_manufacturer[BORING_SMBIOS_STRING_CAP];
    char board_product[BORING_SMBIOS_STRING_CAP];
    uint64_t memory_bytes;
    uint32_t structures;
    uint32_t table_bytes;
    uint32_t memory_device_slots;
    uint32_t memory_devices_present;
    uint8_t major;
    uint8_t minor;
    bool entry_64;
    bool available;
    bool complete;
    bool memory_info_available;
    bool memory_size_complete;
};

bool boring_smbios_decode_entry(const void *entry, size_t available,
                                struct boring_smbios_table *table);
bool boring_smbios_parse_table(const void *bytes, size_t length,
                               uint16_t declared_structures,
                               struct boring_platform_identity *identity);
void boring_smbios_boot_init(
    const struct boring_limine_hhdm_response *hhdm,
    const struct boring_limine_memmap_response *memory_map);
const struct boring_platform_identity *boring_platform_identity_get(void);

#endif
