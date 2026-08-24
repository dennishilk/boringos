#include <boring/boringfs.h>

static bool checked_add_u32(uint32_t left, uint32_t right, uint32_t *out) {
    if ((out == NULL) || (left > UINT32_MAX - right)) {
        return false;
    }
    *out = left + right;
    return true;
}

static bool checked_add_u64(uint64_t left, uint64_t right, uint64_t *out) {
    if ((out == NULL) || (left > UINT64_MAX - right)) {
        return false;
    }
    *out = left + right;
    return true;
}

static bool checked_mul_u64(uint64_t left, uint64_t right, uint64_t *out) {
    if ((out == NULL) || ((left != 0ULL) && (right > UINT64_MAX / left))) {
        return false;
    }
    *out = left * right;
    return true;
}

static bool checked_u64_to_size(uint64_t value, size_t *out) {
    if ((out == NULL) || (value > (uint64_t)SIZE_MAX)) {
        return false;
    }
    *out = (size_t)value;
    return true;
}

static bool bytes_all_zero(const uint8_t *bytes, size_t length) {
    size_t index;

    if (bytes == NULL) {
        return false;
    }
    for (index = 0U; index < length; ++index) {
        if (bytes[index] != 0U) {
            return false;
        }
    }
    return true;
}

static bool bytes_equal(const uint8_t *left,
                        const uint8_t *right,
                        size_t length) {
    size_t index;

    if ((left == NULL) || (right == NULL)) {
        return false;
    }
    for (index = 0U; index < length; ++index) {
        if (left[index] != right[index]) {
            return false;
        }
    }
    return true;
}

static enum boringfs_validation_result fail_result(
    struct boringfs_validation_error *error,
    enum boringfs_validation_result code,
    uint32_t object_id,
    uint32_t block,
    uint64_t directory_record_index) {
    if (error != NULL) {
        error->code = code;
        error->object_id = object_id;
        error->block = block;
        error->directory_record_index = directory_record_index;
    }
    return code;
}

static void clear_error(struct boringfs_validation_error *error) {
    if (error != NULL) {
        error->code = BORINGFS_VALIDATE_OK;
        error->object_id = BORINGFS_LOCATION_NONE_U32;
        error->block = BORINGFS_LOCATION_NONE_U32;
        error->directory_record_index = BORINGFS_LOCATION_NONE_U64;
    }
}

static bool magic_valid(const uint8_t *magic) {
    static const uint8_t expected[BORINGFS_MAGIC_SIZE] = {
        'B', 'O', 'R', 'I', 'N', 'G', 'F', 'S'
    };

    return bytes_equal(magic, expected, (size_t)BORINGFS_MAGIC_SIZE);
}

static bool bitmap_bit(const uint8_t *volume,
                       size_t volume_size,
                       const struct boringfs_superblock *superblock,
                       uint64_t block,
                       bool *allocated_out) {
    uint64_t bitmap_base;
    uint64_t byte_offset;
    uint64_t absolute;
    size_t absolute_size;
    uint8_t mask;

    if ((volume == NULL) || (superblock == NULL) ||
        (allocated_out == NULL) ||
        !checked_mul_u64((uint64_t)superblock->bitmap_start,
                         (uint64_t)BORINGFS_BLOCK_SIZE,
                         &bitmap_base)) {
        return false;
    }
    byte_offset = block / 8ULL;
    if (!checked_add_u64(bitmap_base, byte_offset, &absolute) ||
        !checked_u64_to_size(absolute, &absolute_size) ||
        (absolute_size >= volume_size)) {
        return false;
    }
    mask = (uint8_t)(1U << (unsigned int)(block % 8ULL));
    *allocated_out = (volume[absolute_size] & mask) != 0U;
    return true;
}

static const uint8_t *object_record_bytes(
    const uint8_t *volume,
    size_t volume_size,
    const struct boringfs_superblock *superblock,
    uint32_t object_id) {
    uint64_t table_base;
    uint64_t slot_offset;
    uint64_t absolute;
    uint64_t end;
    size_t absolute_size;

    if ((volume == NULL) || (superblock == NULL) ||
        (object_id == BORINGFS_NULL_OBJECT_ID) ||
        (object_id > superblock->object_count) ||
        !checked_mul_u64((uint64_t)superblock->object_table_start,
                         (uint64_t)BORINGFS_BLOCK_SIZE,
                         &table_base) ||
        !checked_mul_u64((uint64_t)(object_id - 1U),
                         (uint64_t)BORINGFS_OBJECT_RECORD_SIZE,
                         &slot_offset) ||
        !checked_add_u64(table_base, slot_offset, &absolute) ||
        !checked_add_u64(absolute,
                         (uint64_t)BORINGFS_OBJECT_RECORD_SIZE,
                         &end) ||
        (end > (uint64_t)volume_size) ||
        !checked_u64_to_size(absolute, &absolute_size)) {
        return NULL;
    }
    return &volume[absolute_size];
}

static bool decode_object_id(const uint8_t *volume,
                             size_t volume_size,
                             const struct boringfs_superblock *superblock,
                             uint32_t object_id,
                             struct boringfs_object *out) {
    const uint8_t *record = object_record_bytes(volume, volume_size,
                                                 superblock, object_id);

    return (record != NULL) &&
           boringfs_decode_object(record,
                                  (size_t)BORINGFS_OBJECT_RECORD_SIZE,
                                  out);
}

static bool logical_object_read(const uint8_t *volume,
                                size_t volume_size,
                                const struct boringfs_object *object,
                                uint64_t logical_offset,
                                uint8_t *destination,
                                size_t length) {
    uint64_t offset = logical_offset;
    size_t remaining = length;
    size_t output_index = 0U;
    size_t extent_index;

    if ((volume == NULL) || (object == NULL) ||
        ((destination == NULL) && (length != 0U))) {
        return false;
    }

    for (extent_index = 0U;
         (extent_index < (size_t)object->extent_count) && (remaining != 0U);
         ++extent_index) {
        uint64_t extent_bytes;

        if (!checked_mul_u64(
                (uint64_t)object->extents[extent_index].block_count,
                (uint64_t)BORINGFS_BLOCK_SIZE,
                &extent_bytes)) {
            return false;
        }
        if (offset >= extent_bytes) {
            offset -= extent_bytes;
            continue;
        }

        while ((offset < extent_bytes) && (remaining != 0U)) {
            uint64_t physical_base;
            uint64_t physical;
            uint64_t available64 = extent_bytes - offset;
            size_t available;
            size_t chunk;
            size_t physical_size;
            size_t index;

            if (!checked_mul_u64(
                    (uint64_t)object->extents[extent_index].start_block,
                    (uint64_t)BORINGFS_BLOCK_SIZE,
                    &physical_base) ||
                !checked_add_u64(physical_base, offset, &physical) ||
                !checked_u64_to_size(available64, &available) ||
                !checked_u64_to_size(physical, &physical_size)) {
                return false;
            }
            chunk = (remaining < available) ? remaining : available;
            if ((physical_size > volume_size) ||
                (chunk > volume_size - physical_size)) {
                return false;
            }
            for (index = 0U; index < chunk; ++index) {
                destination[output_index + index] =
                    volume[physical_size + index];
            }
            output_index += chunk;
            remaining -= chunk;
            offset += (uint64_t)chunk;
        }
        offset = 0ULL;
    }
    return remaining == 0U;
}

bool boringfs_utf8_valid(const uint8_t *bytes, size_t length) {
    size_t index = 0U;

    if ((bytes == NULL) && (length != 0U)) {
        return false;
    }
    while (index < length) {
        const uint8_t first = bytes[index];

        if (first <= 0x7fU) {
            ++index;
            continue;
        }
        if ((first >= 0xc2U) && (first <= 0xdfU)) {
            if ((index + 1U >= length) ||
                (bytes[index + 1U] < 0x80U) ||
                (bytes[index + 1U] > 0xbfU)) {
                return false;
            }
            index += 2U;
            continue;
        }
        if ((first >= 0xe0U) && (first <= 0xefU)) {
            uint8_t second;
            uint8_t third;

            if (index + 2U >= length) {
                return false;
            }
            second = bytes[index + 1U];
            third = bytes[index + 2U];
            if ((third < 0x80U) || (third > 0xbfU)) {
                return false;
            }
            if (first == 0xe0U) {
                if ((second < 0xa0U) || (second > 0xbfU)) {
                    return false;
                }
            } else if (first == 0xedU) {
                if ((second < 0x80U) || (second > 0x9fU)) {
                    return false;
                }
            } else if ((second < 0x80U) || (second > 0xbfU)) {
                return false;
            }
            index += 3U;
            continue;
        }
        if ((first >= 0xf0U) && (first <= 0xf4U)) {
            uint8_t second;
            uint8_t third;
            uint8_t fourth;

            if (index + 3U >= length) {
                return false;
            }
            second = bytes[index + 1U];
            third = bytes[index + 2U];
            fourth = bytes[index + 3U];
            if ((third < 0x80U) || (third > 0xbfU) ||
                (fourth < 0x80U) || (fourth > 0xbfU)) {
                return false;
            }
            if (first == 0xf0U) {
                if ((second < 0x90U) || (second > 0xbfU)) {
                    return false;
                }
            } else if (first == 0xf4U) {
                if ((second < 0x80U) || (second > 0x8fU)) {
                    return false;
                }
            } else if ((second < 0x80U) || (second > 0xbfU)) {
                return false;
            }
            index += 4U;
            continue;
        }
        return false;
    }
    return true;
}

const char *boringfs_validation_result_name(
    enum boringfs_validation_result result) {
    switch (result) {
        case BORINGFS_VALIDATE_OK: return "ok";
        case BORINGFS_VALIDATE_INVALID_ARGUMENT: return "invalid-argument";
        case BORINGFS_VALIDATE_TRUNCATED_VOLUME: return "truncated-volume";
        case BORINGFS_VALIDATE_BAD_MAGIC: return "bad-magic";
        case BORINGFS_VALIDATE_UNSUPPORTED_VERSION: return "unsupported-version";
        case BORINGFS_VALIDATE_UNSUPPORTED_FEATURE: return "unsupported-feature";
        case BORINGFS_VALIDATE_BAD_SUPERBLOCK: return "bad-superblock";
        case BORINGFS_VALIDATE_BAD_LAYOUT: return "bad-layout";
        case BORINGFS_VALIDATE_BAD_BITMAP: return "bad-bitmap";
        case BORINGFS_VALIDATE_BAD_OBJECT_RECORD: return "bad-object-record";
        case BORINGFS_VALIDATE_BAD_EXTENT: return "bad-extent";
        case BORINGFS_VALIDATE_EXTENT_OVERLAP: return "extent-overlap";
        case BORINGFS_VALIDATE_BAD_DIRECTORY_RECORD: return "bad-directory-record";
        case BORINGFS_VALIDATE_INVALID_UTF8: return "invalid-utf8";
        case BORINGFS_VALIDATE_DUPLICATE_NAME: return "duplicate-name";
        case BORINGFS_VALIDATE_ORPHAN_OBJECT: return "orphan-object";
        case BORINGFS_VALIDATE_MULTIPLE_REFERENCE: return "multiple-reference";
        case BORINGFS_VALIDATE_PARENT_MISMATCH: return "parent-mismatch";
        case BORINGFS_VALIDATE_DIRECTORY_CYCLE: return "directory-cycle";
        case BORINGFS_VALIDATE_ALLOCATION_LEAK: return "allocation-leak";
        case BORINGFS_VALIDATE_INSUFFICIENT_WORKSPACE: return "insufficient-workspace";
        default: return "unknown";
    }
}

static enum boringfs_validation_result validate_superblock(
    const uint8_t *volume,
    size_t volume_size,
    struct boringfs_superblock *superblock,
    struct boringfs_validation_error *error) {
    uint32_t expected_bitmap_blocks;
    uint32_t expected_object_table_start;
    uint32_t expected_object_table_blocks;
    uint32_t expected_data_start;
    uint64_t object_bytes;
    uint64_t rounded_object_bytes;
    uint64_t declared_bytes;
    uint64_t bitmap_storage_bits;

    if (volume_size < (size_t)BORINGFS_BLOCK_SIZE) {
        return fail_result(error, BORINGFS_VALIDATE_TRUNCATED_VOLUME,
                           BORINGFS_LOCATION_NONE_U32, 0U,
                           BORINGFS_LOCATION_NONE_U64);
    }
    if (!boringfs_decode_superblock(volume, volume_size, superblock)) {
        return fail_result(error, BORINGFS_VALIDATE_BAD_SUPERBLOCK,
                           BORINGFS_LOCATION_NONE_U32, 0U,
                           BORINGFS_LOCATION_NONE_U64);
    }
    if (!magic_valid(superblock->magic)) {
        return fail_result(error, BORINGFS_VALIDATE_BAD_MAGIC,
                           BORINGFS_LOCATION_NONE_U32, 0U,
                           BORINGFS_LOCATION_NONE_U64);
    }
    if ((superblock->format_major != BORINGFS_FORMAT_MAJOR) ||
        (superblock->format_minor != BORINGFS_FORMAT_MINOR)) {
        return fail_result(error, BORINGFS_VALIDATE_UNSUPPORTED_VERSION,
                           BORINGFS_LOCATION_NONE_U32, 0U,
                           BORINGFS_LOCATION_NONE_U64);
    }
    if ((superblock->feature_compat != 0U) ||
        (superblock->feature_ro_compat != 0U) ||
        (superblock->feature_incompat != 0U)) {
        return fail_result(error, BORINGFS_VALIDATE_UNSUPPORTED_FEATURE,
                           BORINGFS_LOCATION_NONE_U32, 0U,
                           BORINGFS_LOCATION_NONE_U64);
    }
    if ((superblock->header_size != BORINGFS_SUPERBLOCK_HEADER_SIZE) ||
        (superblock->block_shift != BORINGFS_BLOCK_SHIFT) ||
        (superblock->flags != 0U) ||
        (superblock->total_blocks == 0U) ||
        (superblock->total_blocks > BORINGFS_MAX_BLOCKS) ||
        (superblock->object_count < BORINGFS_MIN_OBJECTS) ||
        (superblock->object_count > BORINGFS_MAX_OBJECTS) ||
        (superblock->root_object_id != BORINGFS_ROOT_OBJECT_ID) ||
        (superblock->object_record_size != BORINGFS_OBJECT_RECORD_SIZE) ||
        (superblock->directory_record_size != BORINGFS_DIRECTORY_RECORD_SIZE) ||
        !bytes_all_zero(&volume[64], 64U) ||
        !bytes_all_zero(&volume[BORINGFS_SUPERBLOCK_HEADER_SIZE],
                        (size_t)BORINGFS_BLOCK_SIZE -
                        (size_t)BORINGFS_SUPERBLOCK_HEADER_SIZE)) {
        return fail_result(error, BORINGFS_VALIDATE_BAD_SUPERBLOCK,
                           BORINGFS_LOCATION_NONE_U32, 0U,
                           BORINGFS_LOCATION_NONE_U64);
    }

    if (superblock->total_blocks > UINT32_MAX -
                                   (BORINGFS_BITMAP_BLOCK_BITS - 1U)) {
        return fail_result(error, BORINGFS_VALIDATE_BAD_LAYOUT,
                           BORINGFS_LOCATION_NONE_U32, 0U,
                           BORINGFS_LOCATION_NONE_U64);
    }
    expected_bitmap_blocks =
        (superblock->total_blocks + (BORINGFS_BITMAP_BLOCK_BITS - 1U)) /
        BORINGFS_BITMAP_BLOCK_BITS;
    if (!checked_add_u32(1U, expected_bitmap_blocks,
                         &expected_object_table_start) ||
        !checked_mul_u64((uint64_t)superblock->object_count,
                         (uint64_t)BORINGFS_OBJECT_RECORD_SIZE,
                         &object_bytes) ||
        !checked_add_u64(object_bytes,
                         (uint64_t)BORINGFS_BLOCK_SIZE - 1ULL,
                         &rounded_object_bytes) ||
        ((rounded_object_bytes / (uint64_t)BORINGFS_BLOCK_SIZE) >
         (uint64_t)UINT32_MAX)) {
        return fail_result(error, BORINGFS_VALIDATE_BAD_LAYOUT,
                           BORINGFS_LOCATION_NONE_U32, 0U,
                           BORINGFS_LOCATION_NONE_U64);
    }
    expected_object_table_blocks =
        (uint32_t)(rounded_object_bytes / (uint64_t)BORINGFS_BLOCK_SIZE);
    if (!checked_add_u32(expected_object_table_start,
                         expected_object_table_blocks,
                         &expected_data_start) ||
        (superblock->bitmap_start != 1U) ||
        (superblock->bitmap_blocks != expected_bitmap_blocks) ||
        (superblock->object_table_start != expected_object_table_start) ||
        (superblock->object_table_blocks != expected_object_table_blocks) ||
        (superblock->data_start != expected_data_start) ||
        (superblock->data_start > superblock->total_blocks) ||
        !checked_mul_u64((uint64_t)superblock->total_blocks,
                         (uint64_t)BORINGFS_BLOCK_SIZE,
                         &declared_bytes) ||
        (declared_bytes > (uint64_t)SIZE_MAX)) {
        return fail_result(error, BORINGFS_VALIDATE_BAD_LAYOUT,
                           BORINGFS_LOCATION_NONE_U32, 0U,
                           BORINGFS_LOCATION_NONE_U64);
    }
    if ((uint64_t)volume_size < declared_bytes) {
        return fail_result(error, BORINGFS_VALIDATE_TRUNCATED_VOLUME,
                           BORINGFS_LOCATION_NONE_U32,
                           superblock->total_blocks,
                           BORINGFS_LOCATION_NONE_U64);
    }
    if (!checked_mul_u64((uint64_t)superblock->bitmap_blocks,
                         (uint64_t)BORINGFS_BITMAP_BLOCK_BITS,
                         &bitmap_storage_bits) ||
        (bitmap_storage_bits < (uint64_t)superblock->total_blocks)) {
        return fail_result(error, BORINGFS_VALIDATE_BAD_LAYOUT,
                           BORINGFS_LOCATION_NONE_U32, 0U,
                           BORINGFS_LOCATION_NONE_U64);
    }
    return BORINGFS_VALIDATE_OK;
}

static enum boringfs_validation_result validate_bitmap_metadata_and_tail(
    const uint8_t *volume,
    size_t volume_size,
    const struct boringfs_superblock *superblock,
    uint32_t *block_owner,
    struct boringfs_validation_error *error) {
    uint64_t block;
    uint64_t bitmap_storage_bits;

    for (block = 0ULL; block < (uint64_t)superblock->data_start; ++block) {
        bool allocated = false;

        if (!bitmap_bit(volume, volume_size, superblock, block, &allocated) ||
            !allocated) {
            return fail_result(error, BORINGFS_VALIDATE_BAD_BITMAP,
                               BORINGFS_LOCATION_NONE_U32,
                               (uint32_t)block,
                               BORINGFS_LOCATION_NONE_U64);
        }
        block_owner[(size_t)block] = UINT32_MAX;
    }

    if (!checked_mul_u64((uint64_t)superblock->bitmap_blocks,
                         (uint64_t)BORINGFS_BITMAP_BLOCK_BITS,
                         &bitmap_storage_bits)) {
        return fail_result(error, BORINGFS_VALIDATE_BAD_LAYOUT,
                           BORINGFS_LOCATION_NONE_U32, 0U,
                           BORINGFS_LOCATION_NONE_U64);
    }
    for (block = (uint64_t)superblock->total_blocks;
         block < bitmap_storage_bits; ++block) {
        bool allocated = false;

        if (!bitmap_bit(volume, volume_size, superblock, block, &allocated) ||
            !allocated) {
            return fail_result(error, BORINGFS_VALIDATE_BAD_BITMAP,
                               BORINGFS_LOCATION_NONE_U32,
                               (block <= (uint64_t)UINT32_MAX) ?
                                   (uint32_t)block : BORINGFS_LOCATION_NONE_U32,
                               BORINGFS_LOCATION_NONE_U64);
        }
    }
    return BORINGFS_VALIDATE_OK;
}

static enum boringfs_validation_result validate_objects_and_extents(
    const uint8_t *volume,
    size_t volume_size,
    const struct boringfs_superblock *superblock,
    uint32_t *block_owner,
    struct boringfs_validation_error *error) {
    uint32_t object_id;

    for (object_id = 1U; object_id <= superblock->object_count; ++object_id) {
        const uint8_t *raw = object_record_bytes(volume, volume_size,
                                                  superblock, object_id);
        struct boringfs_object object;
        uint64_t capacity = 0ULL;
        size_t extent_index;

        if (raw == NULL) {
            return fail_result(error, BORINGFS_VALIDATE_TRUNCATED_VOLUME,
                               object_id, BORINGFS_LOCATION_NONE_U32,
                               BORINGFS_LOCATION_NONE_U64);
        }
        if (bytes_all_zero(raw, (size_t)BORINGFS_OBJECT_RECORD_SIZE)) {
            if (object_id == BORINGFS_ROOT_OBJECT_ID) {
                return fail_result(error, BORINGFS_VALIDATE_BAD_OBJECT_RECORD,
                                   object_id, BORINGFS_LOCATION_NONE_U32,
                                   BORINGFS_LOCATION_NONE_U64);
            }
            continue;
        }
        if (!boringfs_decode_object(raw,
                                    (size_t)BORINGFS_OBJECT_RECORD_SIZE,
                                    &object)) {
            return fail_result(error, BORINGFS_VALIDATE_BAD_OBJECT_RECORD,
                               object_id, BORINGFS_LOCATION_NONE_U32,
                               BORINGFS_LOCATION_NONE_U64);
        }
        if ((object.state != BORINGFS_OBJECT_ALLOCATED) ||
            ((object.type != BORINGFS_TYPE_REGULAR) &&
             (object.type != BORINGFS_TYPE_DIRECTORY)) ||
            (object.flags != 0U) || (object.object_id != object_id) ||
            (object.parent_object_id == BORINGFS_NULL_OBJECT_ID) ||
            (object.parent_object_id > superblock->object_count) ||
            (object.extent_count > BORINGFS_MAX_EXTENTS) ||
            !bytes_all_zero(&raw[14], 2U) ||
            !bytes_all_zero(&raw[88], 40U)) {
            return fail_result(error, BORINGFS_VALIDATE_BAD_OBJECT_RECORD,
                               object_id, BORINGFS_LOCATION_NONE_U32,
                               BORINGFS_LOCATION_NONE_U64);
        }
        if ((object_id == BORINGFS_ROOT_OBJECT_ID) &&
            ((object.type != BORINGFS_TYPE_DIRECTORY) ||
             (object.parent_object_id != BORINGFS_ROOT_OBJECT_ID))) {
            return fail_result(error, BORINGFS_VALIDATE_BAD_OBJECT_RECORD,
                               object_id, BORINGFS_LOCATION_NONE_U32,
                               BORINGFS_LOCATION_NONE_U64);
        }
        if ((object.type == BORINGFS_TYPE_DIRECTORY) &&
            ((object.size_bytes %
              (uint64_t)BORINGFS_DIRECTORY_RECORD_SIZE) != 0ULL)) {
            return fail_result(error, BORINGFS_VALIDATE_BAD_DIRECTORY_RECORD,
                               object_id, BORINGFS_LOCATION_NONE_U32,
                               BORINGFS_LOCATION_NONE_U64);
        }
        if ((object.type == BORINGFS_TYPE_REGULAR) &&
            (object.size_bytes > (uint64_t)UINT32_MAX)) {
            return fail_result(error, BORINGFS_VALIDATE_BAD_OBJECT_RECORD,
                               object_id, BORINGFS_LOCATION_NONE_U32,
                               BORINGFS_LOCATION_NONE_U64);
        }

        for (extent_index = 0U;
             extent_index < (size_t)BORINGFS_MAX_EXTENTS; ++extent_index) {
            const uint8_t *extent_raw = &raw[24U + (extent_index * 8U)];

            if (extent_index >= (size_t)object.extent_count) {
                if (!bytes_all_zero(extent_raw,
                                    (size_t)BORINGFS_EXTENT_RECORD_SIZE)) {
                    return fail_result(
                        error, BORINGFS_VALIDATE_BAD_OBJECT_RECORD,
                        object_id, BORINGFS_LOCATION_NONE_U32,
                        BORINGFS_LOCATION_NONE_U64);
                }
                continue;
            }

            {
                const struct boringfs_extent *extent =
                    &object.extents[extent_index];
                uint32_t end_block32;
                uint64_t end_block;
                uint64_t extent_bytes;
                uint64_t new_capacity;
                uint64_t block;

                if ((extent->block_count == 0U) ||
                    (extent->start_block < superblock->data_start) ||
                    !checked_add_u32(extent->start_block,
                                     extent->block_count,
                                     &end_block32) ||
                    ((end_block = (uint64_t)end_block32) >
                     (uint64_t)superblock->total_blocks) ||
                    !checked_mul_u64((uint64_t)extent->block_count,
                                     (uint64_t)BORINGFS_BLOCK_SIZE,
                                     &extent_bytes) ||
                    !checked_add_u64(capacity, extent_bytes,
                                     &new_capacity)) {
                    return fail_result(error, BORINGFS_VALIDATE_BAD_EXTENT,
                                       object_id, extent->start_block,
                                       BORINGFS_LOCATION_NONE_U64);
                }
                capacity = new_capacity;
                for (block = (uint64_t)extent->start_block;
                     block < end_block; ++block) {
                    bool allocated = false;

                    if (block_owner[(size_t)block] != 0U) {
                        return fail_result(
                            error, BORINGFS_VALIDATE_EXTENT_OVERLAP,
                            object_id, (uint32_t)block,
                            BORINGFS_LOCATION_NONE_U64);
                    }
                    if (!bitmap_bit(volume, volume_size, superblock,
                                    block, &allocated) || !allocated) {
                        return fail_result(error, BORINGFS_VALIDATE_BAD_BITMAP,
                                           object_id, (uint32_t)block,
                                           BORINGFS_LOCATION_NONE_U64);
                    }
                    block_owner[(size_t)block] = object_id;
                }
            }
        }
        if (object.size_bytes > capacity) {
            return fail_result(error, BORINGFS_VALIDATE_BAD_OBJECT_RECORD,
                               object_id, BORINGFS_LOCATION_NONE_U32,
                               BORINGFS_LOCATION_NONE_U64);
        }
    }
    return BORINGFS_VALIDATE_OK;
}

static enum boringfs_validation_result validate_parents_and_cycles(
    const uint8_t *volume,
    size_t volume_size,
    const struct boringfs_superblock *superblock,
    struct boringfs_validation_error *error) {
    uint32_t object_id;
    struct boringfs_object root;

    if (!decode_object_id(volume, volume_size, superblock,
                          BORINGFS_ROOT_OBJECT_ID, &root) ||
        (root.state != BORINGFS_OBJECT_ALLOCATED) ||
        (root.type != BORINGFS_TYPE_DIRECTORY) ||
        (root.parent_object_id != BORINGFS_ROOT_OBJECT_ID)) {
        return fail_result(error, BORINGFS_VALIDATE_BAD_OBJECT_RECORD,
                           BORINGFS_ROOT_OBJECT_ID,
                           BORINGFS_LOCATION_NONE_U32,
                           BORINGFS_LOCATION_NONE_U64);
    }

    for (object_id = 2U; object_id <= superblock->object_count; ++object_id) {
        struct boringfs_object object;
        struct boringfs_object parent;
        uint32_t cursor;
        uint32_t steps;
        bool reached_root = false;

        if (!decode_object_id(volume, volume_size, superblock,
                              object_id, &object)) {
            return fail_result(error, BORINGFS_VALIDATE_BAD_OBJECT_RECORD,
                               object_id, BORINGFS_LOCATION_NONE_U32,
                               BORINGFS_LOCATION_NONE_U64);
        }
        if (object.state == BORINGFS_OBJECT_FREE) {
            continue;
        }
        if (!decode_object_id(volume, volume_size, superblock,
                              object.parent_object_id, &parent) ||
            (parent.state != BORINGFS_OBJECT_ALLOCATED) ||
            (parent.type != BORINGFS_TYPE_DIRECTORY)) {
            return fail_result(error, BORINGFS_VALIDATE_BAD_OBJECT_RECORD,
                               object_id, BORINGFS_LOCATION_NONE_U32,
                               BORINGFS_LOCATION_NONE_U64);
        }

        cursor = object_id;
        for (steps = 0U; steps <= superblock->object_count; ++steps) {
            struct boringfs_object current;

            if (cursor == BORINGFS_ROOT_OBJECT_ID) {
                reached_root = true;
                break;
            }
            if (!decode_object_id(volume, volume_size, superblock,
                                  cursor, &current) ||
                (current.state != BORINGFS_OBJECT_ALLOCATED) ||
                (current.parent_object_id == BORINGFS_NULL_OBJECT_ID) ||
                (current.parent_object_id > superblock->object_count)) {
                return fail_result(error, BORINGFS_VALIDATE_BAD_OBJECT_RECORD,
                                   object_id, BORINGFS_LOCATION_NONE_U32,
                                   BORINGFS_LOCATION_NONE_U64);
            }
            if (current.parent_object_id == cursor) {
                return fail_result(error, BORINGFS_VALIDATE_DIRECTORY_CYCLE,
                                   object_id, BORINGFS_LOCATION_NONE_U32,
                                   BORINGFS_LOCATION_NONE_U64);
            }
            cursor = current.parent_object_id;
        }
        if (!reached_root) {
            return fail_result(error, BORINGFS_VALIDATE_DIRECTORY_CYCLE,
                               object_id, BORINGFS_LOCATION_NONE_U32,
                               BORINGFS_LOCATION_NONE_U64);
        }
    }
    return BORINGFS_VALIDATE_OK;
}

static bool directory_name_valid(
    const struct boringfs_directory_record *record) {
    size_t index;

    if ((record->name_length == 0U) ||
        (record->name_length > BORINGFS_MAX_FILENAME)) {
        return false;
    }
    if (((record->name_length == 1U) && (record->name[0] == '.')) ||
        ((record->name_length == 2U) && (record->name[0] == '.') &&
         (record->name[1] == '.'))) {
        return false;
    }
    for (index = 0U; index < (size_t)record->name_length; ++index) {
        if ((record->name[index] == 0U) ||
            (record->name[index] == (uint8_t)'/')) {
            return false;
        }
    }
    return true;
}

static enum boringfs_validation_result read_directory_record(
    const uint8_t *volume,
    size_t volume_size,
    const struct boringfs_object *directory,
    uint64_t record_index,
    uint8_t raw[BORINGFS_DIRECTORY_RECORD_SIZE],
    struct boringfs_validation_error *error) {
    uint64_t logical_offset;

    if (!checked_mul_u64(record_index,
                         (uint64_t)BORINGFS_DIRECTORY_RECORD_SIZE,
                         &logical_offset) ||
        !logical_object_read(volume, volume_size, directory,
                             logical_offset, raw,
                             (size_t)BORINGFS_DIRECTORY_RECORD_SIZE)) {
        return fail_result(error, BORINGFS_VALIDATE_BAD_DIRECTORY_RECORD,
                           directory->object_id,
                           BORINGFS_LOCATION_NONE_U32,
                           record_index);
    }
    return BORINGFS_VALIDATE_OK;
}

static enum boringfs_validation_result validate_directories(
    const uint8_t *volume,
    size_t volume_size,
    const struct boringfs_superblock *superblock,
    uint8_t *reference_count,
    struct boringfs_validation_error *error) {
    uint32_t directory_id;

    for (directory_id = 1U;
         directory_id <= superblock->object_count; ++directory_id) {
        struct boringfs_object directory;
        uint64_t record_count;
        uint64_t record_index;

        if (!decode_object_id(volume, volume_size, superblock,
                              directory_id, &directory)) {
            return fail_result(error, BORINGFS_VALIDATE_BAD_OBJECT_RECORD,
                               directory_id, BORINGFS_LOCATION_NONE_U32,
                               BORINGFS_LOCATION_NONE_U64);
        }
        if ((directory.state != BORINGFS_OBJECT_ALLOCATED) ||
            (directory.type != BORINGFS_TYPE_DIRECTORY)) {
            continue;
        }
        record_count = directory.size_bytes /
                       (uint64_t)BORINGFS_DIRECTORY_RECORD_SIZE;
        for (record_index = 0ULL; record_index < record_count;
             ++record_index) {
            uint8_t raw[BORINGFS_DIRECTORY_RECORD_SIZE];
            struct boringfs_directory_record record;
            struct boringfs_object target;
            size_t index;
            uint64_t previous_index;
            enum boringfs_validation_result result;

            result = read_directory_record(volume, volume_size, &directory,
                                           record_index, raw, error);
            if (result != BORINGFS_VALIDATE_OK) {
                return result;
            }
            if (bytes_all_zero(raw,
                               (size_t)BORINGFS_DIRECTORY_RECORD_SIZE)) {
                continue;
            }
            if (!boringfs_decode_directory_record(
                    raw, (size_t)BORINGFS_DIRECTORY_RECORD_SIZE, &record) ||
                (record.object_id == BORINGFS_NULL_OBJECT_ID) ||
                (record.object_id > superblock->object_count) ||
                (record.flags != 0U) ||
                ((record.type_hint != BORINGFS_TYPE_REGULAR) &&
                 (record.type_hint != BORINGFS_TYPE_DIRECTORY)) ||
                !bytes_all_zero(&raw[248], 8U) ||
                !directory_name_valid(&record)) {
                return fail_result(error,
                                   BORINGFS_VALIDATE_BAD_DIRECTORY_RECORD,
                                   directory_id,
                                   BORINGFS_LOCATION_NONE_U32,
                                   record_index);
            }
            for (index = (size_t)record.name_length;
                 index < (size_t)BORINGFS_MAX_FILENAME; ++index) {
                if (record.name[index] != 0U) {
                    return fail_result(
                        error, BORINGFS_VALIDATE_BAD_DIRECTORY_RECORD,
                        directory_id, BORINGFS_LOCATION_NONE_U32,
                        record_index);
                }
            }
            if (!boringfs_utf8_valid(record.name,
                                     (size_t)record.name_length)) {
                return fail_result(error, BORINGFS_VALIDATE_INVALID_UTF8,
                                   directory_id,
                                   BORINGFS_LOCATION_NONE_U32,
                                   record_index);
            }
            if (!decode_object_id(volume, volume_size, superblock,
                                  record.object_id, &target) ||
                (target.state != BORINGFS_OBJECT_ALLOCATED)) {
                return fail_result(error,
                                   BORINGFS_VALIDATE_BAD_DIRECTORY_RECORD,
                                   directory_id,
                                   BORINGFS_LOCATION_NONE_U32,
                                   record_index);
            }
            if (target.type != record.type_hint) {
                return fail_result(error,
                                   BORINGFS_VALIDATE_BAD_DIRECTORY_RECORD,
                                   directory_id,
                                   BORINGFS_LOCATION_NONE_U32,
                                   record_index);
            }
            if (record.object_id == BORINGFS_ROOT_OBJECT_ID) {
                return fail_result(error,
                                   BORINGFS_VALIDATE_MULTIPLE_REFERENCE,
                                   BORINGFS_ROOT_OBJECT_ID,
                                   BORINGFS_LOCATION_NONE_U32,
                                   record_index);
            }
            if (target.parent_object_id != directory_id) {
                return fail_result(error, BORINGFS_VALIDATE_PARENT_MISMATCH,
                                   record.object_id,
                                   BORINGFS_LOCATION_NONE_U32,
                                   record_index);
            }

            for (previous_index = 0ULL;
                 previous_index < record_index; ++previous_index) {
                uint8_t previous_raw[BORINGFS_DIRECTORY_RECORD_SIZE];
                struct boringfs_directory_record previous;

                result = read_directory_record(volume, volume_size,
                                               &directory, previous_index,
                                               previous_raw, error);
                if (result != BORINGFS_VALIDATE_OK) {
                    return result;
                }
                if (bytes_all_zero(previous_raw,
                                   (size_t)BORINGFS_DIRECTORY_RECORD_SIZE)) {
                    continue;
                }
                if (!boringfs_decode_directory_record(
                        previous_raw,
                        (size_t)BORINGFS_DIRECTORY_RECORD_SIZE,
                        &previous)) {
                    return fail_result(
                        error, BORINGFS_VALIDATE_BAD_DIRECTORY_RECORD,
                        directory_id, BORINGFS_LOCATION_NONE_U32,
                        previous_index);
                }
                if ((previous.name_length == record.name_length) &&
                    bytes_equal(previous.name, record.name,
                                (size_t)record.name_length)) {
                    return fail_result(error,
                                       BORINGFS_VALIDATE_DUPLICATE_NAME,
                                       directory_id,
                                       BORINGFS_LOCATION_NONE_U32,
                                       record_index);
                }
            }

            if (reference_count[(size_t)(record.object_id - 1U)] != 0U) {
                return fail_result(error,
                                   BORINGFS_VALIDATE_MULTIPLE_REFERENCE,
                                   record.object_id,
                                   BORINGFS_LOCATION_NONE_U32,
                                   record_index);
            }
            reference_count[(size_t)(record.object_id - 1U)] = 1U;
        }
    }
    return BORINGFS_VALIDATE_OK;
}

static enum boringfs_validation_result validate_references(
    const uint8_t *volume,
    size_t volume_size,
    const struct boringfs_superblock *superblock,
    const uint8_t *reference_count,
    struct boringfs_validation_error *error) {
    uint32_t object_id;

    if (reference_count[0] != 0U) {
        return fail_result(error, BORINGFS_VALIDATE_MULTIPLE_REFERENCE,
                           BORINGFS_ROOT_OBJECT_ID,
                           BORINGFS_LOCATION_NONE_U32,
                           BORINGFS_LOCATION_NONE_U64);
    }
    for (object_id = 2U; object_id <= superblock->object_count; ++object_id) {
        struct boringfs_object object;

        if (!decode_object_id(volume, volume_size, superblock,
                              object_id, &object)) {
            return fail_result(error, BORINGFS_VALIDATE_BAD_OBJECT_RECORD,
                               object_id, BORINGFS_LOCATION_NONE_U32,
                               BORINGFS_LOCATION_NONE_U64);
        }
        if ((object.state == BORINGFS_OBJECT_ALLOCATED) &&
            (reference_count[(size_t)(object_id - 1U)] != 1U)) {
            return fail_result(error, BORINGFS_VALIDATE_ORPHAN_OBJECT,
                               object_id, BORINGFS_LOCATION_NONE_U32,
                               BORINGFS_LOCATION_NONE_U64);
        }
        if ((object.state == BORINGFS_OBJECT_FREE) &&
            (reference_count[(size_t)(object_id - 1U)] != 0U)) {
            return fail_result(error,
                               BORINGFS_VALIDATE_BAD_DIRECTORY_RECORD,
                               object_id, BORINGFS_LOCATION_NONE_U32,
                               BORINGFS_LOCATION_NONE_U64);
        }
    }
    return BORINGFS_VALIDATE_OK;
}

static enum boringfs_validation_result validate_allocation_ownership(
    const uint8_t *volume,
    size_t volume_size,
    const struct boringfs_superblock *superblock,
    const uint32_t *block_owner,
    struct boringfs_validation_error *error) {
    uint32_t block;

    for (block = superblock->data_start;
         block < superblock->total_blocks; ++block) {
        bool allocated = false;

        if (!bitmap_bit(volume, volume_size, superblock,
                        (uint64_t)block, &allocated)) {
            return fail_result(error, BORINGFS_VALIDATE_BAD_BITMAP,
                               BORINGFS_LOCATION_NONE_U32, block,
                               BORINGFS_LOCATION_NONE_U64);
        }
        if (allocated && (block_owner[(size_t)block] == 0U)) {
            return fail_result(error, BORINGFS_VALIDATE_ALLOCATION_LEAK,
                               BORINGFS_LOCATION_NONE_U32, block,
                               BORINGFS_LOCATION_NONE_U64);
        }
    }
    return BORINGFS_VALIDATE_OK;
}

enum boringfs_validation_result boringfs_validate_volume(
    const uint8_t *volume,
    size_t volume_size,
    const struct boringfs_validation_workspace *workspace,
    struct boringfs_validation_error *error_out) {
    struct boringfs_superblock superblock;
    enum boringfs_validation_result result;
    size_t index;

    clear_error(error_out);
    if ((volume == NULL) || (workspace == NULL) || (error_out == NULL) ||
        (workspace->block_owner == NULL) ||
        (workspace->object_reference_count == NULL)) {
        return fail_result(error_out, BORINGFS_VALIDATE_INVALID_ARGUMENT,
                           BORINGFS_LOCATION_NONE_U32,
                           BORINGFS_LOCATION_NONE_U32,
                           BORINGFS_LOCATION_NONE_U64);
    }

    result = validate_superblock(volume, volume_size, &superblock, error_out);
    if (result != BORINGFS_VALIDATE_OK) {
        return result;
    }
    if ((workspace->block_owner_count < (size_t)superblock.total_blocks) ||
        (workspace->object_reference_count_count <
         (size_t)superblock.object_count)) {
        return fail_result(error_out,
                           BORINGFS_VALIDATE_INSUFFICIENT_WORKSPACE,
                           BORINGFS_LOCATION_NONE_U32,
                           BORINGFS_LOCATION_NONE_U32,
                           BORINGFS_LOCATION_NONE_U64);
    }
    for (index = 0U; index < (size_t)superblock.total_blocks; ++index) {
        workspace->block_owner[index] = 0U;
    }
    for (index = 0U; index < (size_t)superblock.object_count; ++index) {
        workspace->object_reference_count[index] = 0U;
    }

    result = validate_bitmap_metadata_and_tail(
        volume, volume_size, &superblock, workspace->block_owner, error_out);
    if (result != BORINGFS_VALIDATE_OK) {
        return result;
    }
    result = validate_objects_and_extents(
        volume, volume_size, &superblock, workspace->block_owner, error_out);
    if (result != BORINGFS_VALIDATE_OK) {
        return result;
    }
    result = validate_parents_and_cycles(volume, volume_size,
                                         &superblock, error_out);
    if (result != BORINGFS_VALIDATE_OK) {
        return result;
    }
    result = validate_directories(volume, volume_size, &superblock,
                                  workspace->object_reference_count,
                                  error_out);
    if (result != BORINGFS_VALIDATE_OK) {
        return result;
    }
    result = validate_references(volume, volume_size, &superblock,
                                 workspace->object_reference_count,
                                 error_out);
    if (result != BORINGFS_VALIDATE_OK) {
        return result;
    }
    return validate_allocation_ownership(
        volume, volume_size, &superblock, workspace->block_owner, error_out);
}
