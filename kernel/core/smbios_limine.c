#include <stdint.h>

#include <boring/boot_protocol.h>
#include <boring/serial.h>
#include <boring/smbios.h>

#define BORING_SMBIOS_MAX_MEMMAP_ENTRIES 4096ULL
#define BORING_SMBIOS_ENTRY_BYTES 32U

__attribute__((used, section(".limine_requests")))
static volatile struct boring_limine_smbios_request limine_smbios_request = {
    .id = BORING_LIMINE_SMBIOS_REQUEST_ID,
    .revision = 0ULL,
    .response = 0
};

static struct boring_platform_identity platform_identity;

static bool physical_range_mapped(
    const struct boring_limine_memmap_response *memory_map,
    uint64_t physical, uint64_t length) {
    if ((memory_map == 0) || (memory_map->entries == 0) ||
        (memory_map->entry_count > BORING_SMBIOS_MAX_MEMMAP_ENTRIES) ||
        (length == 0ULL) || (physical > (UINT64_MAX - length))) {
        return false;
    }
    const uint64_t end = physical + length;
    for (uint64_t index = 0ULL; index < memory_map->entry_count; ++index) {
        const struct boring_limine_memmap_entry *entry =
            memory_map->entries[index];
        if ((entry == 0) || (entry->type == BORING_LIMINE_MEMMAP_BAD_MEMORY) ||
            (entry->base > (UINT64_MAX - entry->length))) {
            continue;
        }
        const uint64_t entry_end = entry->base + entry->length;
        if ((physical >= entry->base) && (end <= entry_end)) {
            return true;
        }
    }
    return false;
}

static bool select_entry(const struct boring_limine_smbios_response *response,
                         struct boring_smbios_table *table) {
    if ((response != 0) && (response->entry_64 != 0) &&
        boring_smbios_decode_entry(response->entry_64,
                                   BORING_SMBIOS_ENTRY_BYTES, table)) {
        return true;
    }
    return (response != 0) && (response->entry_32 != 0) &&
           boring_smbios_decode_entry(response->entry_32,
                                      BORING_SMBIOS_ENTRY_BYTES, table);
}

static void print_field(const char *name,
                        const char value[BORING_SMBIOS_STRING_CAP]) {
    serial_write_string("smbios: ");
    serial_write_string(name);
    serial_write_string("=");
    serial_write_string(value[0] != '\0' ? value : "unavailable");
    serial_write_string("\n");
}

void boring_smbios_boot_init(
    const struct boring_limine_hhdm_response *hhdm,
    const struct boring_limine_memmap_response *memory_map) {
    struct boring_smbios_table table;
    const struct boring_limine_smbios_response *response =
        limine_smbios_request.response;
    if (!select_entry(response, &table)) {
        serial_write_string("smbios: unavailable-or-invalid-entry\n");
        return;
    }
    if ((hhdm == 0) ||
        !physical_range_mapped(memory_map, table.physical_address,
                               table.length) ||
        (table.physical_address >
         (UINT64_MAX - hhdm->offset))) {
        serial_write_string("smbios: invalid-or-unmapped-table-range\n");
        return;
    }

    const uint64_t virtual_address = table.physical_address + hhdm->offset;
    if (!boring_smbios_parse_table((const void *)(uintptr_t)virtual_address,
                                   table.length, table.declared_structures,
                                   &platform_identity)) {
        serial_write_string("smbios: malformed-table\n");
        return;
    }
    platform_identity.major = table.major;
    platform_identity.minor = table.minor;
    platform_identity.entry_64 = table.entry_64;

    serial_write_string("smbios: entry=");
    serial_write_u64(table.entry_64 ? 64ULL : 32ULL);
    serial_write_string(" version=");
    serial_write_u64(table.major);
    serial_write_string(".");
    serial_write_u64(table.minor);
    serial_write_string(" structures=");
    serial_write_u64(platform_identity.structures);
    serial_write_string(" table_bytes=");
    serial_write_u64(platform_identity.table_bytes);
    serial_write_string("\n");
    print_field("firmware_vendor", platform_identity.firmware_vendor);
    print_field("firmware_version", platform_identity.firmware_version);
    print_field("system_manufacturer", platform_identity.system_manufacturer);
    print_field("system_product", platform_identity.system_product);
    print_field("board_manufacturer", platform_identity.board_manufacturer);
    print_field("board_product", platform_identity.board_product);
    serial_write_string("smbios: memory_slots=");
    serial_write_u64(platform_identity.memory_device_slots);
    serial_write_string(" memory_present=");
    serial_write_u64(platform_identity.memory_devices_present);
    serial_write_string(" memory_bytes=");
    serial_write_u64(platform_identity.memory_bytes);
    serial_write_string(" memory_info_available=");
    serial_write_u64(platform_identity.memory_info_available ? 1ULL : 0ULL);
    serial_write_string(" memory_size_complete=");
    serial_write_u64(platform_identity.memory_size_complete ? 1ULL : 0ULL);
    serial_write_string("\n");
    serial_write_string("smbios: bounded platform identity complete\n");
}

const struct boring_platform_identity *boring_platform_identity_get(void) {
    return &platform_identity;
}
