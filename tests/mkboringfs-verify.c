#include <boring/boringfs.h>

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct expected_geometry {
    uint32_t total_blocks;
    uint32_t object_count;
    uint32_t bitmap_blocks;
    uint32_t object_table_start;
    uint32_t object_table_blocks;
    uint32_t data_start;
    size_t volume_size;
};

static uint16_t test_load_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] |
                      ((uint16_t)bytes[1] << 8U));
}

static uint32_t test_load_le32(const uint8_t *bytes) {
    return (uint32_t)((uint32_t)bytes[0] |
                      ((uint32_t)bytes[1] << 8U) |
                      ((uint32_t)bytes[2] << 16U) |
                      ((uint32_t)bytes[3] << 24U));
}

static uint64_t test_load_le64(const uint8_t *bytes) {
    uint64_t value = 0ULL;
    unsigned int index;

    for (index = 0U; index < 8U; ++index) {
        value |= ((uint64_t)bytes[index]) << (index * 8U);
    }
    return value;
}

static bool parse_u32_decimal(const char *text, uint32_t *value_out) {
    uint64_t value = 0ULL;
    size_t index;

    if ((text == NULL) || (value_out == NULL) || (text[0] == '\0')) {
        return false;
    }
    for (index = 0U; text[index] != '\0'; ++index) {
        uint64_t digit;

        if ((text[index] < '0') || (text[index] > '9')) {
            return false;
        }
        digit = (uint64_t)(unsigned int)(text[index] - '0');
        if (value > ((UINT64_MAX - digit) / 10ULL)) {
            return false;
        }
        value = (value * 10ULL) + digit;
    }
    if (value > UINT32_MAX) {
        return false;
    }
    *value_out = (uint32_t)value;
    return true;
}

static bool calculate_expected(uint32_t blocks, uint32_t objects,
                               struct expected_geometry *geometry) {
    uint64_t bitmap_blocks;
    uint64_t object_bytes;
    uint64_t object_table_blocks;
    uint64_t object_table_start;
    uint64_t data_start;
    uint64_t volume_size;

    if ((geometry == NULL) || (blocks == 0U)) {
        return false;
    }
    bitmap_blocks = ((uint64_t)blocks / (uint64_t)BORINGFS_BITMAP_BLOCK_BITS) +
                    ((((uint64_t)blocks %
                       (uint64_t)BORINGFS_BITMAP_BLOCK_BITS) != 0ULL) ? 1ULL : 0ULL);
    object_bytes = (uint64_t)objects * (uint64_t)BORINGFS_OBJECT_RECORD_SIZE;
    object_table_blocks = (object_bytes / (uint64_t)BORINGFS_BLOCK_SIZE) +
                          (((object_bytes % (uint64_t)BORINGFS_BLOCK_SIZE) != 0ULL) ?
                           1ULL : 0ULL);
    object_table_start = 1ULL + bitmap_blocks;
    data_start = object_table_start + object_table_blocks;
    volume_size = (uint64_t)blocks * (uint64_t)BORINGFS_BLOCK_SIZE;
    if ((bitmap_blocks > UINT32_MAX) ||
        (object_table_start > UINT32_MAX) ||
        (object_table_blocks > UINT32_MAX) ||
        (data_start > UINT32_MAX) ||
        (volume_size > (uint64_t)SIZE_MAX)) {
        return false;
    }

    geometry->total_blocks = blocks;
    geometry->object_count = objects;
    geometry->bitmap_blocks = (uint32_t)bitmap_blocks;
    geometry->object_table_start = (uint32_t)object_table_start;
    geometry->object_table_blocks = (uint32_t)object_table_blocks;
    geometry->data_start = (uint32_t)data_start;
    geometry->volume_size = (size_t)volume_size;
    return true;
}

static bool bytes_are_zero(const uint8_t *bytes, size_t start, size_t end) {
    size_t index;

    for (index = start; index < end; ++index) {
        if (bytes[index] != 0U) {
            return false;
        }
    }
    return true;
}

static bool bitmap_bit_is_set(const uint8_t *bitmap, uint64_t bit_index) {
    const size_t byte_index = (size_t)(bit_index / 8ULL);
    const unsigned int shift = (unsigned int)(bit_index % 8ULL);
    const uint8_t mask = (uint8_t)(1U << shift);

    return (bitmap[byte_index] & mask) != 0U;
}

static bool validate_with_shared_core(const uint8_t *volume,
                                      size_t volume_size,
                                      const struct expected_geometry *geometry) {
    struct boringfs_validation_workspace workspace = { 0 };
    struct boringfs_validation_error error = { 0 };
    enum boringfs_validation_result result;
    uint8_t *before;

    workspace.block_owner =
        (uint32_t *)calloc((size_t)geometry->total_blocks, sizeof(uint32_t));
    workspace.object_reference_count =
        (uint8_t *)calloc((size_t)geometry->object_count, sizeof(uint8_t));
    before = (uint8_t *)malloc(volume_size);
    if ((workspace.block_owner == NULL) ||
        (workspace.object_reference_count == NULL) || (before == NULL)) {
        free(workspace.block_owner);
        free(workspace.object_reference_count);
        free(before);
        return false;
    }
    (void)memcpy(before, volume, volume_size);
    workspace.block_owner_count = (size_t)geometry->total_blocks;
    workspace.object_reference_count_count = (size_t)geometry->object_count;
    result = boringfs_validate_volume(volume, volume_size, &workspace, &error);
    if (result != BORINGFS_VALIDATE_OK) {
        (void)fprintf(stderr,
                      "shared validator rejected image: %s"
                      " (object=%" PRIu32 ", block=%" PRIu32
                      ", directory-record=%" PRIu64 ")\n",
                      boringfs_validation_result_name(result),
                      error.object_id, error.block,
                      error.directory_record_index);
    }
    if ((result == BORINGFS_VALIDATE_OK) &&
        (memcmp(before, volume, volume_size) != 0)) {
        (void)fprintf(stderr, "shared validator mutated the image\n");
        result = BORINGFS_VALIDATE_INVALID_ARGUMENT;
    }
    free(workspace.block_owner);
    free(workspace.object_reference_count);
    free(before);
    return result == BORINGFS_VALIDATE_OK;
}

static bool check_superblock_bytes(const uint8_t *volume,
                                   const struct expected_geometry *geometry) {
    static const uint8_t magic[BORINGFS_MAGIC_SIZE] = {
        'B', 'O', 'R', 'I', 'N', 'G', 'F', 'S'
    };

    return (memcmp(volume, magic, sizeof(magic)) == 0) &&
           (test_load_le16(&volume[8]) == BORINGFS_FORMAT_MAJOR) &&
           (test_load_le16(&volume[10]) == BORINGFS_FORMAT_MINOR) &&
           (test_load_le16(&volume[12]) == BORINGFS_SUPERBLOCK_HEADER_SIZE) &&
           (volume[14] == BORINGFS_BLOCK_SHIFT) &&
           (volume[15] == 0U) &&
           (test_load_le32(&volume[16]) == geometry->total_blocks) &&
           (test_load_le32(&volume[20]) == 1U) &&
           (test_load_le32(&volume[24]) == geometry->bitmap_blocks) &&
           (test_load_le32(&volume[28]) == geometry->object_table_start) &&
           (test_load_le32(&volume[32]) == geometry->object_table_blocks) &&
           (test_load_le32(&volume[36]) == geometry->object_count) &&
           (test_load_le32(&volume[40]) == BORINGFS_ROOT_OBJECT_ID) &&
           (test_load_le32(&volume[44]) == geometry->data_start) &&
           (test_load_le16(&volume[48]) == BORINGFS_OBJECT_RECORD_SIZE) &&
           (test_load_le16(&volume[50]) == BORINGFS_DIRECTORY_RECORD_SIZE) &&
           (test_load_le32(&volume[52]) == 0U) &&
           (test_load_le32(&volume[56]) == 0U) &&
           (test_load_le32(&volume[60]) == 0U) &&
           bytes_are_zero(volume, 64U, (size_t)BORINGFS_BLOCK_SIZE);
}

static bool check_root_and_unused_objects(
    const uint8_t *volume,
    const struct expected_geometry *geometry) {
    const size_t root_offset =
        (size_t)geometry->object_table_start * (size_t)BORINGFS_BLOCK_SIZE;
    const size_t table_end =
        (size_t)geometry->data_start * (size_t)BORINGFS_BLOCK_SIZE;
    const uint8_t *root_bytes = &volume[root_offset];
    struct boringfs_object root = { 0 };
    size_t index;

    if ((root_bytes[0] != BORINGFS_OBJECT_ALLOCATED) ||
        (root_bytes[1] != BORINGFS_TYPE_DIRECTORY) ||
        (test_load_le16(&root_bytes[2]) != 0U) ||
        (test_load_le32(&root_bytes[4]) != BORINGFS_ROOT_OBJECT_ID) ||
        (test_load_le32(&root_bytes[8]) != BORINGFS_ROOT_OBJECT_ID) ||
        (test_load_le16(&root_bytes[12]) != 0U) ||
        (root_bytes[14] != 0U) || (root_bytes[15] != 0U) ||
        (test_load_le64(&root_bytes[16]) != 0ULL) ||
        !bytes_are_zero(root_bytes, 24U, (size_t)BORINGFS_OBJECT_RECORD_SIZE) ||
        !boringfs_decode_object(root_bytes,
                                (size_t)BORINGFS_OBJECT_RECORD_SIZE,
                                &root)) {
        return false;
    }
    if ((root.state != BORINGFS_OBJECT_ALLOCATED) ||
        (root.type != BORINGFS_TYPE_DIRECTORY) || (root.flags != 0U) ||
        (root.object_id != BORINGFS_ROOT_OBJECT_ID) ||
        (root.parent_object_id != BORINGFS_ROOT_OBJECT_ID) ||
        (root.extent_count != 0U) || (root.size_bytes != 0ULL)) {
        return false;
    }
    for (index = 0U; index < (size_t)BORINGFS_MAX_EXTENTS; ++index) {
        if ((root.extents[index].start_block != 0U) ||
            (root.extents[index].block_count != 0U)) {
            return false;
        }
    }
    return bytes_are_zero(volume,
                          root_offset + (size_t)BORINGFS_OBJECT_RECORD_SIZE,
                          table_end);
}

static bool check_bitmap(const uint8_t *volume,
                         const struct expected_geometry *geometry) {
    const uint8_t *bitmap = &volume[BORINGFS_BLOCK_SIZE];
    const uint64_t capacity =
        (uint64_t)geometry->bitmap_blocks *
        (uint64_t)BORINGFS_BITMAP_BLOCK_BITS;
    uint64_t bit_index;

    for (bit_index = 0ULL; bit_index < capacity; ++bit_index) {
        const bool expected =
            (bit_index < (uint64_t)geometry->data_start) ||
            (bit_index >= (uint64_t)geometry->total_blocks);
        if (bitmap_bit_is_set(bitmap, bit_index) != expected) {
            (void)fprintf(stderr, "bitmap mismatch at bit %" PRIu64 "\n",
                          bit_index);
            return false;
        }
    }
    return true;
}

int main(int argc, char **argv) {
    struct expected_geometry geometry = { 0 };
    uint32_t blocks;
    uint32_t objects;
    FILE *stream = NULL;
    uint8_t *volume = NULL;
    size_t transferred;
    int extra;
    bool ok = false;

    if ((argc != 4) ||
        !parse_u32_decimal(argv[2], &blocks) ||
        !parse_u32_decimal(argv[3], &objects) ||
        !calculate_expected(blocks, objects, &geometry)) {
        (void)fprintf(stderr, "usage: %s <image> <blocks> <objects>\n",
                      (argc > 0) ? argv[0] : "mkboringfs-verify");
        return EXIT_FAILURE;
    }
    stream = fopen(argv[1], "rb");
    if (stream == NULL) {
        (void)fprintf(stderr, "cannot open image\n");
        return EXIT_FAILURE;
    }
    volume = (uint8_t *)malloc(geometry.volume_size);
    if (volume == NULL) {
        (void)fclose(stream);
        return EXIT_FAILURE;
    }
    transferred = fread(volume, 1U, geometry.volume_size, stream);
    extra = fgetc(stream);
    if ((transferred != geometry.volume_size) || (extra != EOF) ||
        (ferror(stream) != 0)) {
        (void)fprintf(stderr, "image size mismatch\n");
        goto cleanup;
    }
    if (!validate_with_shared_core(volume, geometry.volume_size, &geometry)) {
        goto cleanup;
    }
    if (!check_superblock_bytes(volume, &geometry)) {
        (void)fprintf(stderr, "superblock raw-byte proof failed\n");
        goto cleanup;
    }
    if (!check_root_and_unused_objects(volume, &geometry)) {
        (void)fprintf(stderr, "root/unused object proof failed\n");
        goto cleanup;
    }
    if (!check_bitmap(volume, &geometry)) {
        goto cleanup;
    }
    if (!bytes_are_zero(volume,
                        (size_t)geometry.data_start *
                            (size_t)BORINGFS_BLOCK_SIZE,
                        geometry.volume_size)) {
        (void)fprintf(stderr, "data region is not zero\n");
        goto cleanup;
    }

    (void)printf("mkboringfs image verified: blocks=%" PRIu32
                 " objects=%" PRIu32 " bitmap=%" PRIu32
                 " object-table-start=%" PRIu32
                 " object-table-blocks=%" PRIu32
                 " data-start=%" PRIu32 " validator=%s\n",
                 geometry.total_blocks, geometry.object_count,
                 geometry.bitmap_blocks, geometry.object_table_start,
                 geometry.object_table_blocks, geometry.data_start,
                 boringfs_validation_result_name(BORINGFS_VALIDATE_OK));
    ok = true;

cleanup:
    free(volume);
    (void)fclose(stream);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
