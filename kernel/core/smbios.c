#include <boring/smbios.h>

static void clear_bytes(void *object, size_t length) {
    unsigned char *bytes = object;
    for (size_t index = 0U; index < length; ++index) {
        bytes[index] = 0U;
    }
}

static bool bytes_equal(const uint8_t *bytes, const char *text, size_t length) {
    for (size_t index = 0U; index < length; ++index) {
        if (bytes[index] != (uint8_t)text[index]) {
            return false;
        }
    }
    return true;
}

static bool checksum_zero(const uint8_t *bytes, size_t length) {
    uint8_t sum = 0U;
    for (size_t index = 0U; index < length; ++index) {
        sum = (uint8_t)(sum + bytes[index]);
    }
    return sum == 0U;
}

static uint16_t read_u16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] |
                      ((uint16_t)bytes[1] << 8U));
}

static uint32_t read_u32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) |
           ((uint32_t)bytes[3] << 24U);
}

static uint64_t read_u64(const uint8_t *bytes) {
    return (uint64_t)read_u32(bytes) |
           ((uint64_t)read_u32(bytes + 4U) << 32U);
}

bool boring_smbios_decode_entry(const void *entry, size_t available,
                                struct boring_smbios_table *table) {
    const uint8_t *bytes = entry;
    if (table == NULL) {
        return false;
    }
    clear_bytes(table, sizeof(*table));
    if ((bytes == NULL) || (available < 7U)) {
        return false;
    }

    if (bytes_equal(bytes, "_SM3_", 5U)) {
        const size_t entry_length = bytes[6];
        if ((entry_length < 24U) || (entry_length > 32U) ||
            (entry_length > available) || !checksum_zero(bytes, entry_length) ||
            (bytes[7] < 3U)) {
            return false;
        }
        table->length = read_u32(bytes + 12U);
        table->physical_address = read_u64(bytes + 16U);
        table->major = bytes[7];
        table->minor = bytes[8];
        table->entry_64 = true;
    } else if (bytes_equal(bytes, "_SM_", 4U)) {
        const size_t entry_length = bytes[5];
        if ((entry_length < 31U) || (entry_length > 32U) ||
            (entry_length > available) || !checksum_zero(bytes, entry_length) ||
            !bytes_equal(bytes + 16U, "_DMI_", 5U) ||
            !checksum_zero(bytes + 16U, 15U) || (bytes[6] < 2U)) {
            return false;
        }
        table->length = read_u16(bytes + 22U);
        table->physical_address = read_u32(bytes + 24U);
        table->declared_structures = read_u16(bytes + 28U);
        table->major = bytes[6];
        table->minor = bytes[7];
        table->entry_64 = false;
    } else {
        return false;
    }

    return (table->physical_address != 0ULL) &&
           (table->length != 0U) &&
           (table->length <= BORING_SMBIOS_MAX_TABLE_BYTES) &&
           ((table->declared_structures == 0U) ||
            (table->declared_structures <= BORING_SMBIOS_MAX_STRUCTURES));
}

static bool string_copy(const uint8_t *bytes, size_t strings_start,
                        size_t strings_end, uint8_t wanted,
                        char output[BORING_SMBIOS_STRING_CAP]) {
    if (wanted == 0U) {
        output[0] = '\0';
        return true;
    }

    size_t position = strings_start;
    uint16_t current = 1U;
    while (position < strings_end) {
        size_t terminator = position;
        while ((terminator < strings_end) && (bytes[terminator] != 0U)) {
            ++terminator;
        }
        if (current == wanted) {
            size_t copied = 0U;
            for (size_t source = position; source < terminator; ++source) {
                if (copied + 1U < BORING_SMBIOS_STRING_CAP) {
                    const uint8_t value = bytes[source];
                    output[copied++] =
                        ((value >= 0x20U) && (value <= 0x7eU)) ?
                        (char)value : '?';
                }
            }
            output[copied] = '\0';
            return true;
        }
        position = terminator + 1U;
        ++current;
    }
    return false;
}

static bool field_copy_once(const uint8_t *bytes, size_t strings_start,
                            size_t strings_end, uint8_t wanted,
                            char output[BORING_SMBIOS_STRING_CAP]) {
    char value[BORING_SMBIOS_STRING_CAP];
    if (!string_copy(bytes, strings_start, strings_end, wanted, value)) {
        return false;
    }
    if ((output[0] == '\0') && (value[0] != '\0')) {
        for (size_t index = 0U; index < BORING_SMBIOS_STRING_CAP; ++index) {
            output[index] = value[index];
            if (value[index] == '\0') {
                break;
            }
        }
    }
    return true;
}

static bool memory_size(const uint8_t *structure, size_t formatted_length,
                        bool *present, bool *known, uint64_t *bytes) {
    const uint16_t encoded = read_u16(structure + 12U);
    *present = encoded != 0U;
    *known = false;
    *bytes = 0ULL;
    if (encoded == 0U) {
        *known = true;
        return true;
    }
    if (encoded == 0xffffU) {
        return true;
    }

    uint64_t units;
    uint64_t multiplier;
    if (encoded == 0x7fffU) {
        if (formatted_length < 32U) {
            return false;
        }
        units = (uint64_t)(read_u32(structure + 28U) & 0x7fffffffU);
        multiplier = 1024ULL * 1024ULL;
        if ((units == 0ULL) || (units == 0x7fffffffULL)) {
            return true;
        }
    } else if ((encoded & 0x8000U) != 0U) {
        units = encoded & 0x7fffU;
        multiplier = 1024ULL;
    } else {
        units = encoded;
        multiplier = 1024ULL * 1024ULL;
    }
    if (units > (UINT64_MAX / multiplier)) {
        return false;
    }
    *bytes = units * multiplier;
    *known = true;
    return true;
}

static bool parse_known(const uint8_t *table, const uint8_t *structure,
                        size_t formatted_length,
                        size_t strings_start, size_t strings_end,
                        struct boring_platform_identity *identity) {
    const uint8_t type = structure[0];
    if (type == 0U) {
        return (formatted_length >= 18U) &&
               field_copy_once(table, strings_start, strings_end,
                               structure[4], identity->firmware_vendor) &&
               field_copy_once(table, strings_start, strings_end,
                               structure[5], identity->firmware_version);
    }
    if (type == 1U) {
        return (formatted_length >= 8U) &&
               field_copy_once(table, strings_start, strings_end,
                               structure[4], identity->system_manufacturer) &&
               field_copy_once(table, strings_start, strings_end,
                               structure[5], identity->system_product);
    }
    if (type == 2U) {
        return (formatted_length >= 8U) &&
               field_copy_once(table, strings_start, strings_end,
                               structure[4], identity->board_manufacturer) &&
               field_copy_once(table, strings_start, strings_end,
                               structure[5], identity->board_product);
    }
    if (type == 17U) {
        bool present;
        bool known;
        uint64_t bytes;
        if ((formatted_length < 21U) ||
            !memory_size(structure, formatted_length, &present, &known, &bytes)) {
            return false;
        }
        identity->memory_info_available = true;
        ++identity->memory_device_slots;
        if (present) {
            ++identity->memory_devices_present;
            if (!known) {
                identity->memory_size_complete = false;
            } else if (identity->memory_bytes > (UINT64_MAX - bytes)) {
                return false;
            } else {
                identity->memory_bytes += bytes;
            }
        }
    }
    return true;
}

bool boring_smbios_parse_table(const void *table_bytes, size_t length,
                               uint16_t declared_structures,
                               struct boring_platform_identity *identity) {
    const uint8_t *bytes = table_bytes;
    if (identity == NULL) {
        return false;
    }
    clear_bytes(identity, sizeof(*identity));
    if ((bytes == NULL) || (length == 0U) ||
        (length > BORING_SMBIOS_MAX_TABLE_BYTES) ||
        (declared_structures > BORING_SMBIOS_MAX_STRUCTURES)) {
        return false;
    }

    identity->memory_size_complete = true;
    size_t offset = 0U;
    bool end_marker = false;
    while (offset < length) {
        if ((identity->structures >= BORING_SMBIOS_MAX_STRUCTURES) ||
            ((declared_structures != 0U) &&
             (identity->structures >= declared_structures)) ||
            ((length - offset) < 4U)) {
            break;
        }
        const uint8_t *structure = bytes + offset;
        const size_t formatted_length = structure[1];
        if ((formatted_length < 4U) || (formatted_length > (length - offset))) {
            return false;
        }

        const size_t strings_start = offset + formatted_length;
        size_t strings_end = strings_start;
        bool terminated = false;
        while ((strings_end + 1U) < length) {
            if ((bytes[strings_end] == 0U) &&
                (bytes[strings_end + 1U] == 0U)) {
                terminated = true;
                break;
            }
            ++strings_end;
        }
        if (!terminated ||
            !parse_known(bytes, structure, formatted_length, strings_start,
                         strings_end, identity)) {
            return false;
        }

        ++identity->structures;
        offset = strings_end + 2U;
        if (structure[0] == 127U) {
            end_marker = true;
            break;
        }
    }

    if (((declared_structures != 0U) &&
         ((identity->structures != declared_structures) ||
          (offset != length))) ||
        ((declared_structures == 0U) && !end_marker)) {
        return false;
    }
    identity->table_bytes = (uint32_t)offset;
    identity->available = true;
    identity->complete = true;
    return true;
}
