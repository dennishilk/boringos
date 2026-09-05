#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/acpi_s5.h>

#define AML_NAME_OP 0x08U
#define AML_PACKAGE_OP 0x12U
#define AML_ROOT_PREFIX 0x5cU
#define AML_PARENT_PREFIX 0x5eU
#define AML_ZERO_OP 0x00U
#define AML_ONE_OP 0x01U
#define AML_BYTE_PREFIX 0x0aU
#define AML_WORD_PREFIX 0x0bU
#define AML_DWORD_PREFIX 0x0cU
#define AML_QWORD_PREFIX 0x0eU

static bool aml_pkg_length(const uint8_t *aml,
                           size_t available,
                           size_t *package_length,
                           size_t *encoded_length) {
    uint8_t lead;
    unsigned int following;
    size_t value;
    unsigned int index;

    if ((aml == NULL) || (available == 0U) ||
        (package_length == NULL) || (encoded_length == NULL)) {
        return false;
    }
    lead = aml[0];
    following = (unsigned int)(lead >> 6U);
    if ((size_t)following + 1U > available) {
        return false;
    }
    if (following == 0U) {
        value = (size_t)(lead & 0x3fU);
    } else {
        value = (size_t)(lead & 0x0fU);
        for (index = 0U; index < following; ++index) {
            value |= ((size_t)aml[1U + (size_t)index]) <<
                     (4U + (8U * index));
        }
    }
    *package_length = value;
    *encoded_length = (size_t)following + 1U;
    return true;
}

static bool aml_integer(const uint8_t *aml,
                        size_t available,
                        uint64_t *value,
                        size_t *consumed) {
    size_t bytes = 0U;
    size_t index;
    uint64_t result = 0ULL;

    if ((aml == NULL) || (available == 0U) ||
        (value == NULL) || (consumed == NULL)) {
        return false;
    }
    switch (aml[0]) {
        case AML_ZERO_OP:
            *value = 0ULL;
            *consumed = 1U;
            return true;
        case AML_ONE_OP:
            *value = 1ULL;
            *consumed = 1U;
            return true;
        case AML_BYTE_PREFIX:
            bytes = 1U;
            break;
        case AML_WORD_PREFIX:
            bytes = 2U;
            break;
        case AML_DWORD_PREFIX:
            bytes = 4U;
            break;
        case AML_QWORD_PREFIX:
            bytes = 8U;
            break;
        default:
            return false;
    }
    if (available < 1U + bytes) {
        return false;
    }
    for (index = 0U; index < bytes; ++index) {
        result |= ((uint64_t)aml[1U + index]) << (8U * (unsigned int)index);
    }
    *value = result;
    *consumed = 1U + bytes;
    return true;
}

static bool aml_is_s5_name(const uint8_t *aml,
                           size_t aml_length,
                           size_t *cursor) {
    size_t index;

    if ((aml == NULL) || (cursor == NULL) || (*cursor >= aml_length)) {
        return false;
    }
    index = *cursor;
    if (aml[index] == AML_ROOT_PREFIX) {
        ++index;
    }
    while ((index < aml_length) && (aml[index] == AML_PARENT_PREFIX)) {
        ++index;
    }
    if ((index > aml_length) || ((aml_length - index) < 4U) ||
        (aml[index] != (uint8_t)'_') ||
        (aml[index + 1U] != (uint8_t)'S') ||
        (aml[index + 2U] != (uint8_t)'5') ||
        (aml[index + 3U] != (uint8_t)'_')) {
        return false;
    }
    *cursor = index + 4U;
    return true;
}

bool boring_acpi_s5_parse(const uint8_t *aml,
                          size_t aml_length,
                          uint8_t *sleep_type_a,
                          uint8_t *sleep_type_b) {
    size_t offset;

    if ((aml == NULL) || (sleep_type_a == NULL) || (sleep_type_b == NULL)) {
        return false;
    }
    for (offset = 0U; offset < aml_length; ++offset) {
        size_t cursor;
        size_t package_length;
        size_t package_length_bytes;
        size_t package_end;
        size_t consumed;
        uint64_t first;
        uint64_t second;

        if (aml[offset] != AML_NAME_OP) {
            continue;
        }
        cursor = offset + 1U;
        if (!aml_is_s5_name(aml, aml_length, &cursor) ||
            (cursor >= aml_length) || (aml[cursor] != AML_PACKAGE_OP)) {
            continue;
        }
        ++cursor;
        if (!aml_pkg_length(&aml[cursor], aml_length - cursor,
                            &package_length, &package_length_bytes) ||
            (package_length < package_length_bytes + 1U) ||
            (package_length > aml_length - cursor)) {
            continue;
        }
        package_end = cursor + package_length;
        cursor += package_length_bytes;
        if ((cursor >= package_end) || (aml[cursor] < 2U)) {
            continue;
        }
        ++cursor;
        if (!aml_integer(&aml[cursor], package_end - cursor,
                         &first, &consumed)) {
            continue;
        }
        cursor += consumed;
        if ((cursor >= package_end) ||
            !aml_integer(&aml[cursor], package_end - cursor,
                         &second, &consumed) ||
            (first > 7ULL) || (second > 7ULL)) {
            continue;
        }
        *sleep_type_a = (uint8_t)first;
        *sleep_type_b = (uint8_t)second;
        return true;
    }
    return false;
}
