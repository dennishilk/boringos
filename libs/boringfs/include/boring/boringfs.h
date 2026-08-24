#ifndef BORING_BORINGFS_H
#define BORING_BORINGFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BORINGFS_MAGIC_SIZE 8U
#define BORINGFS_BLOCK_SIZE 4096U
#define BORINGFS_BLOCK_SHIFT 12U
#define BORINGFS_SUPERBLOCK_HEADER_SIZE 128U
#define BORINGFS_OBJECT_RECORD_SIZE 128U
#define BORINGFS_DIRECTORY_RECORD_SIZE 256U
#define BORINGFS_EXTENT_RECORD_SIZE 8U
#define BORINGFS_ROOT_OBJECT_ID 1U
#define BORINGFS_NULL_OBJECT_ID 0U
#define BORINGFS_MAX_EXTENTS 8U
#define BORINGFS_MAX_FILENAME 240U
#define BORINGFS_MAX_BLOCKS 1048576U
#define BORINGFS_MIN_OBJECTS 64U
#define BORINGFS_MAX_OBJECTS 16384U
#define BORINGFS_FORMAT_MAJOR 0U
#define BORINGFS_FORMAT_MINOR 1U
#define BORINGFS_BITMAP_BLOCK_BITS 32768U

#define BORINGFS_OBJECT_FREE 0U
#define BORINGFS_OBJECT_ALLOCATED 1U
#define BORINGFS_TYPE_NONE 0U
#define BORINGFS_TYPE_REGULAR 1U
#define BORINGFS_TYPE_DIRECTORY 2U

#define BORINGFS_LOCATION_NONE_U32 UINT32_MAX
#define BORINGFS_LOCATION_NONE_U64 UINT64_MAX

struct boringfs_extent {
    uint32_t start_block;
    uint32_t block_count;
};

struct boringfs_superblock {
    uint8_t magic[BORINGFS_MAGIC_SIZE];
    uint16_t format_major;
    uint16_t format_minor;
    uint16_t header_size;
    uint8_t block_shift;
    uint8_t flags;
    uint32_t total_blocks;
    uint32_t bitmap_start;
    uint32_t bitmap_blocks;
    uint32_t object_table_start;
    uint32_t object_table_blocks;
    uint32_t object_count;
    uint32_t root_object_id;
    uint32_t data_start;
    uint16_t object_record_size;
    uint16_t directory_record_size;
    uint32_t feature_compat;
    uint32_t feature_ro_compat;
    uint32_t feature_incompat;
};

struct boringfs_object {
    uint8_t state;
    uint8_t type;
    uint16_t flags;
    uint32_t object_id;
    uint32_t parent_object_id;
    uint16_t extent_count;
    uint64_t size_bytes;
    struct boringfs_extent extents[BORINGFS_MAX_EXTENTS];
};

struct boringfs_directory_record {
    uint32_t object_id;
    uint16_t name_length;
    uint8_t type_hint;
    uint8_t flags;
    uint8_t name[BORINGFS_MAX_FILENAME];
};

bool boringfs_decode_superblock(const uint8_t *bytes,
                                size_t length,
                                struct boringfs_superblock *out);
bool boringfs_encode_superblock(uint8_t *bytes,
                                size_t length,
                                const struct boringfs_superblock *value);
bool boringfs_decode_extent(const uint8_t *bytes,
                            size_t length,
                            struct boringfs_extent *out);
bool boringfs_encode_extent(uint8_t *bytes,
                            size_t length,
                            const struct boringfs_extent *value);
bool boringfs_decode_object(const uint8_t *bytes,
                            size_t length,
                            struct boringfs_object *out);
bool boringfs_encode_object(uint8_t *bytes,
                            size_t length,
                            const struct boringfs_object *value);
bool boringfs_decode_directory_record(
    const uint8_t *bytes,
    size_t length,
    struct boringfs_directory_record *out);
bool boringfs_encode_directory_record(
    uint8_t *bytes,
    size_t length,
    const struct boringfs_directory_record *value);

enum boringfs_validation_result {
    BORINGFS_VALIDATE_OK = 0,
    BORINGFS_VALIDATE_INVALID_ARGUMENT,
    BORINGFS_VALIDATE_TRUNCATED_VOLUME,
    BORINGFS_VALIDATE_BAD_MAGIC,
    BORINGFS_VALIDATE_UNSUPPORTED_VERSION,
    BORINGFS_VALIDATE_UNSUPPORTED_FEATURE,
    BORINGFS_VALIDATE_BAD_SUPERBLOCK,
    BORINGFS_VALIDATE_BAD_LAYOUT,
    BORINGFS_VALIDATE_BAD_BITMAP,
    BORINGFS_VALIDATE_BAD_OBJECT_RECORD,
    BORINGFS_VALIDATE_BAD_EXTENT,
    BORINGFS_VALIDATE_EXTENT_OVERLAP,
    BORINGFS_VALIDATE_BAD_DIRECTORY_RECORD,
    BORINGFS_VALIDATE_INVALID_UTF8,
    BORINGFS_VALIDATE_DUPLICATE_NAME,
    BORINGFS_VALIDATE_ORPHAN_OBJECT,
    BORINGFS_VALIDATE_MULTIPLE_REFERENCE,
    BORINGFS_VALIDATE_PARENT_MISMATCH,
    BORINGFS_VALIDATE_DIRECTORY_CYCLE,
    BORINGFS_VALIDATE_ALLOCATION_LEAK,
    BORINGFS_VALIDATE_INSUFFICIENT_WORKSPACE
};

struct boringfs_validation_error {
    enum boringfs_validation_result code;
    uint32_t object_id;
    uint32_t block;
    uint64_t directory_record_index;
};

struct boringfs_validation_workspace {
    uint32_t *block_owner;
    size_t block_owner_count;
    uint8_t *object_reference_count;
    size_t object_reference_count_count;
};

enum boringfs_validation_result boringfs_validate_volume(
    const uint8_t *volume,
    size_t volume_size,
    const struct boringfs_validation_workspace *workspace,
    struct boringfs_validation_error *error_out);

bool boringfs_utf8_valid(const uint8_t *bytes, size_t length);
const char *boringfs_validation_result_name(enum boringfs_validation_result result);

#endif
