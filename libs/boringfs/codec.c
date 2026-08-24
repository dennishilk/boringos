#include <boring/boringfs.h>

static uint16_t load_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] |
                      ((uint16_t)bytes[1] << 8U));
}

static uint32_t load_le32(const uint8_t *bytes) {
    return (uint32_t)((uint32_t)bytes[0] |
                      ((uint32_t)bytes[1] << 8U) |
                      ((uint32_t)bytes[2] << 16U) |
                      ((uint32_t)bytes[3] << 24U));
}

static uint64_t load_le64(const uint8_t *bytes) {
    uint64_t value = 0ULL;
    unsigned int index;

    for (index = 0U; index < 8U; ++index) {
        value |= ((uint64_t)bytes[index]) << (index * 8U);
    }
    return value;
}

static void store_le16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)(value & 0xffU);
    bytes[1] = (uint8_t)((value >> 8U) & 0xffU);
}

static void store_le32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value & 0xffU);
    bytes[1] = (uint8_t)((value >> 8U) & 0xffU);
    bytes[2] = (uint8_t)((value >> 16U) & 0xffU);
    bytes[3] = (uint8_t)((value >> 24U) & 0xffU);
}

static void store_le64(uint8_t *bytes, uint64_t value) {
    unsigned int index;

    for (index = 0U; index < 8U; ++index) {
        bytes[index] = (uint8_t)((value >> (index * 8U)) & 0xffULL);
    }
}

static void zero_bytes(uint8_t *bytes, size_t length) {
    size_t index;

    for (index = 0U; index < length; ++index) {
        bytes[index] = 0U;
    }
}

static bool free_object_value_is_zero(const struct boringfs_object *value) {
    size_t index;

    if ((value->type != BORINGFS_TYPE_NONE) || (value->flags != 0U) ||
        (value->object_id != 0U) || (value->parent_object_id != 0U) ||
        (value->extent_count != 0U) || (value->size_bytes != 0ULL)) {
        return false;
    }
    for (index = 0U; index < (size_t)BORINGFS_MAX_EXTENTS; ++index) {
        if ((value->extents[index].start_block != 0U) ||
            (value->extents[index].block_count != 0U)) {
            return false;
        }
    }
    return true;
}

static bool unused_directory_value_is_zero(
    const struct boringfs_directory_record *value) {
    size_t index;

    if ((value->name_length != 0U) || (value->type_hint != 0U) ||
        (value->flags != 0U)) {
        return false;
    }
    for (index = 0U; index < (size_t)BORINGFS_MAX_FILENAME; ++index) {
        if (value->name[index] != 0U) {
            return false;
        }
    }
    return true;
}

bool boringfs_decode_superblock(const uint8_t *bytes,
                                size_t length,
                                struct boringfs_superblock *out) {
    size_t index;

    if ((bytes == NULL) || (out == NULL) ||
        (length < (size_t)BORINGFS_SUPERBLOCK_HEADER_SIZE)) {
        return false;
    }
    for (index = 0U; index < (size_t)BORINGFS_MAGIC_SIZE; ++index) {
        out->magic[index] = bytes[index];
    }
    out->format_major = load_le16(&bytes[8]);
    out->format_minor = load_le16(&bytes[10]);
    out->header_size = load_le16(&bytes[12]);
    out->block_shift = bytes[14];
    out->flags = bytes[15];
    out->total_blocks = load_le32(&bytes[16]);
    out->bitmap_start = load_le32(&bytes[20]);
    out->bitmap_blocks = load_le32(&bytes[24]);
    out->object_table_start = load_le32(&bytes[28]);
    out->object_table_blocks = load_le32(&bytes[32]);
    out->object_count = load_le32(&bytes[36]);
    out->root_object_id = load_le32(&bytes[40]);
    out->data_start = load_le32(&bytes[44]);
    out->object_record_size = load_le16(&bytes[48]);
    out->directory_record_size = load_le16(&bytes[50]);
    out->feature_compat = load_le32(&bytes[52]);
    out->feature_ro_compat = load_le32(&bytes[56]);
    out->feature_incompat = load_le32(&bytes[60]);
    return true;
}

bool boringfs_encode_superblock(uint8_t *bytes,
                                size_t length,
                                const struct boringfs_superblock *value) {
    size_t index;

    if ((bytes == NULL) || (value == NULL) ||
        (length < (size_t)BORINGFS_BLOCK_SIZE)) {
        return false;
    }
    zero_bytes(bytes, (size_t)BORINGFS_BLOCK_SIZE);
    for (index = 0U; index < (size_t)BORINGFS_MAGIC_SIZE; ++index) {
        bytes[index] = value->magic[index];
    }
    store_le16(&bytes[8], value->format_major);
    store_le16(&bytes[10], value->format_minor);
    store_le16(&bytes[12], value->header_size);
    bytes[14] = value->block_shift;
    bytes[15] = value->flags;
    store_le32(&bytes[16], value->total_blocks);
    store_le32(&bytes[20], value->bitmap_start);
    store_le32(&bytes[24], value->bitmap_blocks);
    store_le32(&bytes[28], value->object_table_start);
    store_le32(&bytes[32], value->object_table_blocks);
    store_le32(&bytes[36], value->object_count);
    store_le32(&bytes[40], value->root_object_id);
    store_le32(&bytes[44], value->data_start);
    store_le16(&bytes[48], value->object_record_size);
    store_le16(&bytes[50], value->directory_record_size);
    store_le32(&bytes[52], value->feature_compat);
    store_le32(&bytes[56], value->feature_ro_compat);
    store_le32(&bytes[60], value->feature_incompat);
    return true;
}

bool boringfs_decode_extent(const uint8_t *bytes,
                            size_t length,
                            struct boringfs_extent *out) {
    if ((bytes == NULL) || (out == NULL) ||
        (length < (size_t)BORINGFS_EXTENT_RECORD_SIZE)) {
        return false;
    }
    out->start_block = load_le32(&bytes[0]);
    out->block_count = load_le32(&bytes[4]);
    return true;
}

bool boringfs_encode_extent(uint8_t *bytes,
                            size_t length,
                            const struct boringfs_extent *value) {
    if ((bytes == NULL) || (value == NULL) ||
        (length < (size_t)BORINGFS_EXTENT_RECORD_SIZE)) {
        return false;
    }
    store_le32(&bytes[0], value->start_block);
    store_le32(&bytes[4], value->block_count);
    return true;
}

bool boringfs_decode_object(const uint8_t *bytes,
                            size_t length,
                            struct boringfs_object *out) {
    size_t index;

    if ((bytes == NULL) || (out == NULL) ||
        (length < (size_t)BORINGFS_OBJECT_RECORD_SIZE)) {
        return false;
    }
    out->state = bytes[0];
    out->type = bytes[1];
    out->flags = load_le16(&bytes[2]);
    out->object_id = load_le32(&bytes[4]);
    out->parent_object_id = load_le32(&bytes[8]);
    out->extent_count = load_le16(&bytes[12]);
    out->size_bytes = load_le64(&bytes[16]);
    for (index = 0U; index < (size_t)BORINGFS_MAX_EXTENTS; ++index) {
        if (!boringfs_decode_extent(&bytes[24U + (index * 8U)],
                                    (size_t)BORINGFS_EXTENT_RECORD_SIZE,
                                    &out->extents[index])) {
            return false;
        }
    }
    return true;
}

bool boringfs_encode_object(uint8_t *bytes,
                            size_t length,
                            const struct boringfs_object *value) {
    size_t index;

    if ((bytes == NULL) || (value == NULL) ||
        (length < (size_t)BORINGFS_OBJECT_RECORD_SIZE)) {
        return false;
    }
    zero_bytes(bytes, (size_t)BORINGFS_OBJECT_RECORD_SIZE);
    if (value->state == BORINGFS_OBJECT_FREE) {
        return free_object_value_is_zero(value);
    }
    if ((value->state != BORINGFS_OBJECT_ALLOCATED) ||
        (value->extent_count > BORINGFS_MAX_EXTENTS)) {
        return false;
    }
    bytes[0] = value->state;
    bytes[1] = value->type;
    store_le16(&bytes[2], value->flags);
    store_le32(&bytes[4], value->object_id);
    store_le32(&bytes[8], value->parent_object_id);
    store_le16(&bytes[12], value->extent_count);
    store_le64(&bytes[16], value->size_bytes);
    for (index = 0U; index < (size_t)value->extent_count; ++index) {
        if (!boringfs_encode_extent(&bytes[24U + (index * 8U)],
                                    (size_t)BORINGFS_EXTENT_RECORD_SIZE,
                                    &value->extents[index])) {
            return false;
        }
    }
    return true;
}

bool boringfs_decode_directory_record(
    const uint8_t *bytes,
    size_t length,
    struct boringfs_directory_record *out) {
    size_t index;

    if ((bytes == NULL) || (out == NULL) ||
        (length < (size_t)BORINGFS_DIRECTORY_RECORD_SIZE)) {
        return false;
    }
    out->object_id = load_le32(&bytes[0]);
    out->name_length = load_le16(&bytes[4]);
    out->type_hint = bytes[6];
    out->flags = bytes[7];
    for (index = 0U; index < (size_t)BORINGFS_MAX_FILENAME; ++index) {
        out->name[index] = bytes[8U + index];
    }
    return true;
}

bool boringfs_encode_directory_record(
    uint8_t *bytes,
    size_t length,
    const struct boringfs_directory_record *value) {
    size_t index;

    if ((bytes == NULL) || (value == NULL) ||
        (length < (size_t)BORINGFS_DIRECTORY_RECORD_SIZE)) {
        return false;
    }
    zero_bytes(bytes, (size_t)BORINGFS_DIRECTORY_RECORD_SIZE);
    if (value->object_id == BORINGFS_NULL_OBJECT_ID) {
        return unused_directory_value_is_zero(value);
    }
    if ((value->name_length == 0U) ||
        (value->name_length > BORINGFS_MAX_FILENAME)) {
        return false;
    }
    store_le32(&bytes[0], value->object_id);
    store_le16(&bytes[4], value->name_length);
    bytes[6] = value->type_hint;
    bytes[7] = value->flags;
    for (index = 0U; index < (size_t)value->name_length; ++index) {
        bytes[8U + index] = value->name[index];
    }
    return true;
}
