#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <boring/smbios.h>

static uint8_t table_bytes[65536];
static size_t table_used;

static void put_u16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
}

static void put_u32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    bytes[2] = (uint8_t)(value >> 16U);
    bytes[3] = (uint8_t)(value >> 24U);
}

static void put_u64(uint8_t *bytes, uint64_t value) {
    put_u32(bytes, (uint32_t)value);
    put_u32(bytes + 4U, (uint32_t)(value >> 32U));
}

static void checksum(uint8_t *bytes, size_t length, size_t checksum_offset) {
    uint8_t sum = 0U;
    bytes[checksum_offset] = 0U;
    for (size_t index = 0U; index < length; ++index) {
        sum = (uint8_t)(sum + bytes[index]);
    }
    bytes[checksum_offset] = (uint8_t)(0U - sum);
}

static void table_reset(void) {
    memset(table_bytes, 0, sizeof(table_bytes));
    table_used = 0U;
}

static size_t structure_add(uint8_t type, uint8_t length,
                            const char *const *strings, size_t string_count) {
    const size_t start = table_used;
    assert(length >= 4U);
    assert((table_used + length + 2U) < sizeof(table_bytes));
    memset(table_bytes + table_used, 0, length);
    table_bytes[table_used] = type;
    table_bytes[table_used + 1U] = length;
    table_used += length;
    for (size_t index = 0U; index < string_count; ++index) {
        const size_t length_bytes = strlen(strings[index]);
        assert((table_used + length_bytes + 2U) < sizeof(table_bytes));
        memcpy(table_bytes + table_used, strings[index], length_bytes);
        table_used += length_bytes;
        table_bytes[table_used++] = 0U;
    }
    table_bytes[table_used++] = 0U;
    if (string_count == 0U) {
        table_bytes[table_used++] = 0U;
    }
    return start;
}

static void valid_table(void) {
    static const char *const firmware[] = {"SeaBIOS", "1.16-test"};
    static const char *const system[] = {"QEMU", "Standard PC"};
    static const char *const board[] = {"Boring Board Co", "Bounded Board"};
    table_reset();
    size_t start = structure_add(0U, 18U, firmware, 2U);
    table_bytes[start + 4U] = 1U;
    table_bytes[start + 5U] = 2U;
    start = structure_add(1U, 8U, system, 2U);
    table_bytes[start + 4U] = 1U;
    table_bytes[start + 5U] = 2U;
    start = structure_add(2U, 8U, board, 2U);
    table_bytes[start + 4U] = 1U;
    table_bytes[start + 5U] = 2U;
    start = structure_add(17U, 32U, NULL, 0U);
    put_u16(table_bytes + start + 12U, 0x7fffU);
    put_u32(table_bytes + start + 28U, 2048U);
    start = structure_add(17U, 21U, NULL, 0U);
    put_u16(table_bytes + start + 12U, 0U);
    (void)structure_add(127U, 4U, NULL, 0U);
}

static void entry_tests(void) {
    uint8_t entry[32] = {0};
    struct boring_smbios_table table;
    assert(!boring_smbios_decode_entry(NULL, 0U, &table));
    assert(!boring_smbios_decode_entry(entry, sizeof(entry), NULL));
    memcpy(entry, "_SM3_", 5U);
    entry[6] = 24U;
    entry[7] = 3U;
    entry[8] = 5U;
    put_u32(entry + 12U, 4096U);
    put_u64(entry + 16U, 0x123456789ULL);
    checksum(entry, 24U, 5U);
    assert(boring_smbios_decode_entry(entry, sizeof(entry), &table));
    assert(table.entry_64 && table.major == 3U && table.minor == 5U);
    assert(table.length == 4096U &&
           table.physical_address == 0x123456789ULL);
    entry[5] ^= 1U;
    assert(!boring_smbios_decode_entry(entry, sizeof(entry), &table));
    entry[5] ^= 1U;
    put_u32(entry + 12U, BORING_SMBIOS_MAX_TABLE_BYTES + 1U);
    checksum(entry, 24U, 5U);
    assert(!boring_smbios_decode_entry(entry, sizeof(entry), &table));
    put_u32(entry + 12U, 4096U);
    checksum(entry, 24U, 5U);
    entry[6] = 23U;
    assert(!boring_smbios_decode_entry(entry, sizeof(entry), &table));

    memset(entry, 0, sizeof(entry));
    memcpy(entry, "_SM_", 4U);
    entry[5] = 31U;
    entry[6] = 2U;
    entry[7] = 8U;
    memcpy(entry + 16U, "_DMI_", 5U);
    put_u16(entry + 22U, 8192U);
    put_u32(entry + 24U, 0x000f0000U);
    put_u16(entry + 28U, 42U);
    checksum(entry + 16U, 15U, 5U);
    checksum(entry, 31U, 4U);
    assert(boring_smbios_decode_entry(entry, sizeof(entry), &table));
    assert(!table.entry_64 && table.major == 2U && table.minor == 8U);
    assert(table.length == 8192U && table.declared_structures == 42U);
    entry[21] ^= 1U;
    checksum(entry, 31U, 4U);
    assert(!boring_smbios_decode_entry(entry, sizeof(entry), &table));
    assert(!boring_smbios_decode_entry(entry, 6U, &table));
}

static void valid_and_optional_tests(void) {
    struct boring_platform_identity identity;
    assert(!boring_smbios_parse_table(table_bytes, 1U, 0U, NULL));
    valid_table();
    assert(boring_smbios_parse_table(table_bytes, table_used, 6U, &identity));
    assert(identity.available && identity.complete && identity.structures == 6U);
    assert(strcmp(identity.firmware_vendor, "SeaBIOS") == 0);
    assert(strcmp(identity.firmware_version, "1.16-test") == 0);
    assert(strcmp(identity.system_manufacturer, "QEMU") == 0);
    assert(strcmp(identity.system_product, "Standard PC") == 0);
    assert(strcmp(identity.board_manufacturer, "Boring Board Co") == 0);
    assert(strcmp(identity.board_product, "Bounded Board") == 0);
    assert(identity.memory_info_available && identity.memory_size_complete);
    assert(identity.memory_device_slots == 2U &&
           identity.memory_devices_present == 1U);
    assert(identity.memory_bytes == (2048ULL * 1024ULL * 1024ULL));
    assert(identity.table_bytes == table_used);

    table_reset();
    (void)structure_add(127U, 4U, NULL, 0U);
    assert(boring_smbios_parse_table(table_bytes, table_used, 0U, &identity));
    assert(identity.structures == 1U && identity.system_product[0] == '\0');
    assert(!identity.memory_info_available);
}

static void malformed_tests(void) {
    struct boring_platform_identity identity;
    static const char *const one[] = {"only-one"};
    table_reset();
    size_t start = structure_add(1U, 8U, one, 1U);
    table_bytes[start + 4U] = 2U;
    table_bytes[start + 5U] = 0U;
    (void)structure_add(127U, 4U, NULL, 0U);
    assert(!boring_smbios_parse_table(table_bytes, table_used, 0U, &identity));

    valid_table();
    table_bytes[1] = 3U;
    assert(!boring_smbios_parse_table(table_bytes, table_used, 6U, &identity));
    valid_table();
    assert(!boring_smbios_parse_table(table_bytes, 10U, 6U, &identity));
    valid_table();
    assert(!boring_smbios_parse_table(table_bytes, table_used - 1U, 6U,
                                      &identity));
    valid_table();
    assert(!boring_smbios_parse_table(table_bytes, table_used, 5U, &identity));
    assert(!boring_smbios_parse_table(table_bytes,
                                      BORING_SMBIOS_MAX_TABLE_BYTES + 1U,
                                      0U, &identity));

    table_reset();
    start = structure_add(17U, 21U, NULL, 0U);
    put_u16(table_bytes + start + 12U, 0x7fffU);
    (void)structure_add(127U, 4U, NULL, 0U);
    assert(!boring_smbios_parse_table(table_bytes, table_used, 0U, &identity));

    table_reset();
    for (uint32_t index = 0U; index < BORING_SMBIOS_MAX_STRUCTURES; ++index) {
        (void)structure_add(126U, 4U, NULL, 0U);
    }
    assert(!boring_smbios_parse_table(table_bytes, table_used, 0U, &identity));
}

int main(void) {
    entry_tests();
    valid_and_optional_tests();
    malformed_tests();
    puts("SMBIOS entry checks, bounded strings, malformed tables and real fields passed.");
    return 0;
}
