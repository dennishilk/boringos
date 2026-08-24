#include <boring/boringfs.h>

#include <stdio.h>
#include <string.h>

#define EMPTY_BLOCKS 4U
#define NONTRIVIAL_BLOCKS 9U
#define EMPTY_SIZE ((size_t)EMPTY_BLOCKS * (size_t)BORINGFS_BLOCK_SIZE)
#define NONTRIVIAL_SIZE ((size_t)NONTRIVIAL_BLOCKS * (size_t)BORINGFS_BLOCK_SIZE)
#define TEST_OBJECT_TABLE_START 2U
#define ROOT_DATA_BLOCK0 4U
#define CHILD_DATA_BLOCK 5U
#define ROOT_DATA_BLOCK1 6U
#define FILE_DATA_BLOCK 7U

static uint8_t empty_volume[EMPTY_SIZE];
static uint8_t nontrivial_volume[NONTRIVIAL_SIZE];
static uint8_t working_volume[NONTRIVIAL_SIZE];
static uint8_t shadow_volume[NONTRIVIAL_SIZE];
static uint32_t block_owner[BORINGFS_MAX_BLOCKS];
static uint8_t object_reference_count[BORINGFS_MAX_OBJECTS];
static int failures;

static void test_fail(const char *label) {
    (void)fprintf(stderr, "FAIL: %s\n", label);
    ++failures;
}

static void check_true(bool condition, const char *label) {
    if (!condition) {
        test_fail(label);
    }
}

static void put_le16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)(value & 0xffU);
    bytes[1] = (uint8_t)((value >> 8U) & 0xffU);
}

static void put_le32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value & 0xffU);
    bytes[1] = (uint8_t)((value >> 8U) & 0xffU);
    bytes[2] = (uint8_t)((value >> 16U) & 0xffU);
    bytes[3] = (uint8_t)((value >> 24U) & 0xffU);
}

static void put_le64(uint8_t *bytes, uint64_t value) {
    unsigned int index;

    for (index = 0U; index < 8U; ++index) {
        bytes[index] = (uint8_t)((value >> (index * 8U)) & 0xffULL);
    }
}

static size_t object_offset(uint32_t object_id) {
    return ((size_t)TEST_OBJECT_TABLE_START * (size_t)BORINGFS_BLOCK_SIZE) +
           ((size_t)(object_id - 1U) * (size_t)BORINGFS_OBJECT_RECORD_SIZE);
}

static size_t root_record_offset(uint64_t record_index) {
    if (record_index < 16ULL) {
        return ((size_t)ROOT_DATA_BLOCK0 * (size_t)BORINGFS_BLOCK_SIZE) +
               ((size_t)record_index *
                (size_t)BORINGFS_DIRECTORY_RECORD_SIZE);
    }
    return ((size_t)ROOT_DATA_BLOCK1 * (size_t)BORINGFS_BLOCK_SIZE) +
           ((size_t)(record_index - 16ULL) *
            (size_t)BORINGFS_DIRECTORY_RECORD_SIZE);
}

static size_t child_record_offset(uint64_t record_index) {
    return ((size_t)CHILD_DATA_BLOCK * (size_t)BORINGFS_BLOCK_SIZE) +
           ((size_t)record_index * (size_t)BORINGFS_DIRECTORY_RECORD_SIZE);
}

static void fill_magic(uint8_t magic[BORINGFS_MAGIC_SIZE]) {
    static const uint8_t value[BORINGFS_MAGIC_SIZE] = {
        'B', 'O', 'R', 'I', 'N', 'G', 'F', 'S'
    };

    (void)memcpy(magic, value, (size_t)BORINGFS_MAGIC_SIZE);
}

static void fill_v0_superblock(struct boringfs_superblock *superblock,
                               uint32_t total_blocks,
                               uint32_t object_count) {
    const uint32_t bitmap_blocks =
        (total_blocks + (BORINGFS_BITMAP_BLOCK_BITS - 1U)) /
        BORINGFS_BITMAP_BLOCK_BITS;
    const uint32_t object_table_blocks =
        (uint32_t)(((uint64_t)object_count *
                    (uint64_t)BORINGFS_OBJECT_RECORD_SIZE +
                    (uint64_t)BORINGFS_BLOCK_SIZE - 1ULL) /
                   (uint64_t)BORINGFS_BLOCK_SIZE);

    (void)memset(superblock, 0, sizeof(*superblock));
    fill_magic(superblock->magic);
    superblock->format_major = BORINGFS_FORMAT_MAJOR;
    superblock->format_minor = BORINGFS_FORMAT_MINOR;
    superblock->header_size = BORINGFS_SUPERBLOCK_HEADER_SIZE;
    superblock->block_shift = BORINGFS_BLOCK_SHIFT;
    superblock->flags = 0U;
    superblock->total_blocks = total_blocks;
    superblock->bitmap_start = 1U;
    superblock->bitmap_blocks = bitmap_blocks;
    superblock->object_table_start = 1U + bitmap_blocks;
    superblock->object_table_blocks = object_table_blocks;
    superblock->object_count = object_count;
    superblock->root_object_id = BORINGFS_ROOT_OBJECT_ID;
    superblock->data_start = superblock->object_table_start +
                             object_table_blocks;
    superblock->object_record_size = BORINGFS_OBJECT_RECORD_SIZE;
    superblock->directory_record_size = BORINGFS_DIRECTORY_RECORD_SIZE;
}

static void set_bitmap_bit(uint8_t *volume, uint32_t block, bool allocated) {
    const size_t offset = (size_t)BORINGFS_BLOCK_SIZE +
                          ((size_t)block / 8U);
    const uint8_t mask = (uint8_t)(1U << (block % 8U));

    if (allocated) {
        volume[offset] = (uint8_t)(volume[offset] | mask);
    } else {
        volume[offset] = (uint8_t)(volume[offset] & (uint8_t)~mask);
    }
}

static bool write_object(uint8_t *volume,
                         size_t volume_size,
                         const struct boringfs_object *object) {
    const size_t offset = object_offset(object->object_id);

    if ((offset > volume_size) ||
        ((size_t)BORINGFS_OBJECT_RECORD_SIZE > volume_size - offset)) {
        return false;
    }
    return boringfs_encode_object(&volume[offset],
                                  (size_t)BORINGFS_OBJECT_RECORD_SIZE,
                                  object);
}

static void set_record_name(struct boringfs_directory_record *record,
                            const char *name,
                            size_t length) {
    (void)memset(record->name, 0, sizeof(record->name));
    if ((name != NULL) && (length != 0U)) {
        (void)memcpy(record->name, name, length);
    }
    record->name_length = (uint16_t)length;
}

static bool write_directory_record_at(uint8_t *volume,
                                      size_t volume_size,
                                      size_t offset,
                                      const struct boringfs_directory_record *record) {
    if ((offset > volume_size) ||
        ((size_t)BORINGFS_DIRECTORY_RECORD_SIZE > volume_size - offset)) {
        return false;
    }
    return boringfs_encode_directory_record(
        &volume[offset], (size_t)BORINGFS_DIRECTORY_RECORD_SIZE, record);
}

static bool build_empty_fixture(void) {
    struct boringfs_superblock superblock;
    struct boringfs_object root;

    (void)memset(empty_volume, 0, sizeof(empty_volume));
    fill_v0_superblock(&superblock, EMPTY_BLOCKS, BORINGFS_MIN_OBJECTS);
    if (!boringfs_encode_superblock(empty_volume, sizeof(empty_volume),
                                    &superblock)) {
        return false;
    }

    (void)memset(&empty_volume[BORINGFS_BLOCK_SIZE], 0xff,
                 (size_t)BORINGFS_BLOCK_SIZE);

    (void)memset(&root, 0, sizeof(root));
    root.state = BORINGFS_OBJECT_ALLOCATED;
    root.type = BORINGFS_TYPE_DIRECTORY;
    root.object_id = BORINGFS_ROOT_OBJECT_ID;
    root.parent_object_id = BORINGFS_ROOT_OBJECT_ID;
    return write_object(empty_volume, sizeof(empty_volume), &root);
}

static bool build_nontrivial_fixture(void) {
    struct boringfs_superblock superblock;
    struct boringfs_object object;
    struct boringfs_directory_record record;
    uint32_t object_id;
    uint64_t record_index;

    (void)memset(nontrivial_volume, 0, sizeof(nontrivial_volume));
    fill_v0_superblock(&superblock, NONTRIVIAL_BLOCKS,
                       BORINGFS_MIN_OBJECTS);
    if (!boringfs_encode_superblock(nontrivial_volume,
                                    sizeof(nontrivial_volume),
                                    &superblock)) {
        return false;
    }

    (void)memset(&nontrivial_volume[BORINGFS_BLOCK_SIZE], 0xff,
                 (size_t)BORINGFS_BLOCK_SIZE);
    set_bitmap_bit(nontrivial_volume, 8U, false);

    (void)memset(&object, 0, sizeof(object));
    object.state = BORINGFS_OBJECT_ALLOCATED;
    object.type = BORINGFS_TYPE_DIRECTORY;
    object.object_id = 1U;
    object.parent_object_id = 1U;
    object.extent_count = 2U;
    object.size_bytes = 17ULL * (uint64_t)BORINGFS_DIRECTORY_RECORD_SIZE;
    object.extents[0].start_block = ROOT_DATA_BLOCK0;
    object.extents[0].block_count = 1U;
    object.extents[1].start_block = ROOT_DATA_BLOCK1;
    object.extents[1].block_count = 1U;
    if (!write_object(nontrivial_volume, sizeof(nontrivial_volume), &object)) {
        return false;
    }

    (void)memset(&object, 0, sizeof(object));
    object.state = BORINGFS_OBJECT_ALLOCATED;
    object.type = BORINGFS_TYPE_DIRECTORY;
    object.object_id = 2U;
    object.parent_object_id = 1U;
    object.extent_count = 1U;
    object.size_bytes = (uint64_t)BORINGFS_DIRECTORY_RECORD_SIZE;
    object.extents[0].start_block = CHILD_DATA_BLOCK;
    object.extents[0].block_count = 1U;
    if (!write_object(nontrivial_volume, sizeof(nontrivial_volume), &object)) {
        return false;
    }

    (void)memset(&object, 0, sizeof(object));
    object.state = BORINGFS_OBJECT_ALLOCATED;
    object.type = BORINGFS_TYPE_REGULAR;
    object.object_id = 3U;
    object.parent_object_id = 1U;
    object.extent_count = 1U;
    object.size_bytes = 5ULL;
    object.extents[0].start_block = FILE_DATA_BLOCK;
    object.extents[0].block_count = 1U;
    if (!write_object(nontrivial_volume, sizeof(nontrivial_volume), &object)) {
        return false;
    }

    for (object_id = 4U; object_id <= 18U; ++object_id) {
        (void)memset(&object, 0, sizeof(object));
        object.state = BORINGFS_OBJECT_ALLOCATED;
        object.type = BORINGFS_TYPE_REGULAR;
        object.object_id = object_id;
        object.parent_object_id = 1U;
        if (!write_object(nontrivial_volume, sizeof(nontrivial_volume),
                          &object)) {
            return false;
        }
    }

    (void)memset(&object, 0, sizeof(object));
    object.state = BORINGFS_OBJECT_ALLOCATED;
    object.type = BORINGFS_TYPE_REGULAR;
    object.object_id = 19U;
    object.parent_object_id = 2U;
    if (!write_object(nontrivial_volume, sizeof(nontrivial_volume), &object)) {
        return false;
    }

    for (record_index = 0ULL; record_index < 17ULL; ++record_index) {
        const uint32_t target = (uint32_t)record_index + 2U;
        char generated_name[4];

        (void)memset(&record, 0, sizeof(record));
        record.object_id = target;
        record.flags = 0U;
        if (target == 2U) {
            record.type_hint = BORINGFS_TYPE_DIRECTORY;
            set_record_name(&record, "Child", 5U);
        } else if (target == 3U) {
            record.type_hint = BORINGFS_TYPE_REGULAR;
            set_record_name(&record, "File", 4U);
        } else {
            generated_name[0] = 'N';
            generated_name[1] = (char)('0' + (char)(target / 10U));
            generated_name[2] = (char)('0' + (char)(target % 10U));
            generated_name[3] = '\0';
            record.type_hint = BORINGFS_TYPE_REGULAR;
            set_record_name(&record, generated_name, 3U);
        }
        if (!write_directory_record_at(
                nontrivial_volume, sizeof(nontrivial_volume),
                root_record_offset(record_index), &record)) {
            return false;
        }
    }

    (void)memset(&record, 0, sizeof(record));
    record.object_id = 19U;
    record.type_hint = BORINGFS_TYPE_REGULAR;
    set_record_name(&record, "Inner", 5U);
    if (!write_directory_record_at(nontrivial_volume,
                                   sizeof(nontrivial_volume),
                                   child_record_offset(0ULL), &record)) {
        return false;
    }

    (void)memcpy(&nontrivial_volume[(size_t)FILE_DATA_BLOCK *
                                    (size_t)BORINGFS_BLOCK_SIZE],
                 "hello", 5U);
    return true;
}

static enum boringfs_validation_result validate_with_error(
    const uint8_t *volume,
    size_t length,
    struct boringfs_validation_error *error) {
    const struct boringfs_validation_workspace workspace = {
        .block_owner = block_owner,
        .block_owner_count = (size_t)BORINGFS_MAX_BLOCKS,
        .object_reference_count = object_reference_count,
        .object_reference_count_count = (size_t)BORINGFS_MAX_OBJECTS
    };

    return boringfs_validate_volume(volume, length, &workspace, error);
}

static void expect_result(const char *label,
                          const uint8_t *volume,
                          size_t length,
                          enum boringfs_validation_result expected) {
    struct boringfs_validation_error error;
    const enum boringfs_validation_result actual =
        validate_with_error(volume, length, &error);

    if (actual != expected) {
        (void)fprintf(stderr,
                      "FAIL: %s: expected %s, got %s (object=%u block=%u record=%llu)\n",
                      label,
                      boringfs_validation_result_name(expected),
                      boringfs_validation_result_name(actual),
                      error.object_id,
                      error.block,
                      (unsigned long long)error.directory_record_index);
        ++failures;
    }
}

static void reset_working(void) {
    (void)memcpy(working_volume, nontrivial_volume,
                 sizeof(nontrivial_volume));
}

static void golden_codec_tests(void) {
    uint8_t encoded[BORINGFS_BLOCK_SIZE];
    uint8_t expected[BORINGFS_BLOCK_SIZE];
    uint8_t raw_extent[BORINGFS_EXTENT_RECORD_SIZE] = {
        0x44U, 0x33U, 0x22U, 0x11U, 0x88U, 0x77U, 0x66U, 0x55U
    };
    struct boringfs_superblock superblock;
    struct boringfs_superblock decoded_superblock;
    struct boringfs_extent extent;
    struct boringfs_extent decoded_extent;
    struct boringfs_object object;
    struct boringfs_object decoded_object;
    struct boringfs_directory_record record;
    struct boringfs_directory_record decoded_record;
    uint8_t object_bytes[BORINGFS_OBJECT_RECORD_SIZE];
    uint8_t object_expected[BORINGFS_OBJECT_RECORD_SIZE];
    uint8_t directory_bytes[BORINGFS_DIRECTORY_RECORD_SIZE];
    uint8_t directory_expected[BORINGFS_DIRECTORY_RECORD_SIZE];
    size_t index;

    (void)memset(&superblock, 0, sizeof(superblock));
    fill_magic(superblock.magic);
    superblock.format_major = 0x1234U;
    superblock.format_minor = 0x5678U;
    superblock.header_size = 0x9abcU;
    superblock.block_shift = 0xdeU;
    superblock.flags = 0xf0U;
    superblock.total_blocks = 0x01020304U;
    superblock.bitmap_start = 0x11121314U;
    superblock.bitmap_blocks = 0x21222324U;
    superblock.object_table_start = 0x31323334U;
    superblock.object_table_blocks = 0x41424344U;
    superblock.object_count = 0x51525354U;
    superblock.root_object_id = 0x61626364U;
    superblock.data_start = 0x71727374U;
    superblock.object_record_size = 0x8182U;
    superblock.directory_record_size = 0x9192U;
    superblock.feature_compat = 0xa1a2a3a4U;
    superblock.feature_ro_compat = 0xb1b2b3b4U;
    superblock.feature_incompat = 0xc1c2c3c4U;

    (void)memset(encoded, 0xa5, sizeof(encoded));
    (void)memset(expected, 0, sizeof(expected));
    (void)memcpy(&expected[0], "BORINGFS", 8U);
    expected[8] = 0x34U; expected[9] = 0x12U;
    expected[10] = 0x78U; expected[11] = 0x56U;
    expected[12] = 0xbcU; expected[13] = 0x9aU;
    expected[14] = 0xdeU; expected[15] = 0xf0U;
    expected[16] = 0x04U; expected[17] = 0x03U;
    expected[18] = 0x02U; expected[19] = 0x01U;
    put_le32(&expected[20], 0x11121314U);
    put_le32(&expected[24], 0x21222324U);
    put_le32(&expected[28], 0x31323334U);
    put_le32(&expected[32], 0x41424344U);
    put_le32(&expected[36], 0x51525354U);
    put_le32(&expected[40], 0x61626364U);
    put_le32(&expected[44], 0x71727374U);
    put_le16(&expected[48], 0x8182U);
    put_le16(&expected[50], 0x9192U);
    put_le32(&expected[52], 0xa1a2a3a4U);
    put_le32(&expected[56], 0xb1b2b3b4U);
    put_le32(&expected[60], 0xc1c2c3c4U);

    check_true(boringfs_encode_superblock(encoded, sizeof(encoded),
                                          &superblock),
               "golden superblock encode");
    check_true(memcmp(encoded, expected, sizeof(encoded)) == 0,
               "golden superblock exact bytes and offsets");
    check_true(boringfs_decode_superblock(expected, sizeof(expected),
                                          &decoded_superblock),
               "golden superblock decode");
    check_true(decoded_superblock.format_major == 0x1234U &&
               decoded_superblock.total_blocks == 0x01020304U &&
               decoded_superblock.feature_incompat == 0xc1c2c3c4U,
               "golden superblock LE16/LE32 decode");
    for (index = 64U; index < sizeof(encoded); ++index) {
        if (encoded[index] != 0U) {
            test_fail("superblock reserved/tail deterministic zero");
            break;
        }
    }

    extent.start_block = 0x11223344U;
    extent.block_count = 0x55667788U;
    (void)memset(encoded, 0, sizeof(encoded));
    check_true(boringfs_encode_extent(encoded, BORINGFS_EXTENT_RECORD_SIZE,
                                      &extent),
               "golden extent encode");
    check_true(memcmp(encoded, raw_extent, sizeof(raw_extent)) == 0,
               "golden extent exact little endian bytes");
    check_true(boringfs_decode_extent(raw_extent, sizeof(raw_extent),
                                      &decoded_extent) &&
               decoded_extent.start_block == 0x11223344U &&
               decoded_extent.block_count == 0x55667788U,
               "golden extent independent decode");

    (void)memset(&object, 0, sizeof(object));
    object.state = BORINGFS_OBJECT_ALLOCATED;
    object.type = BORINGFS_TYPE_REGULAR;
    object.flags = 0x1234U;
    object.object_id = 0x11223344U;
    object.parent_object_id = 0x55667788U;
    object.extent_count = 1U;
    object.size_bytes = 0x0102030405060708ULL;
    object.extents[0].start_block = 0xa1a2a3a4U;
    object.extents[0].block_count = 0xb1b2b3b4U;
    (void)memset(object_bytes, 0xa5, sizeof(object_bytes));
    (void)memset(object_expected, 0, sizeof(object_expected));
    object_expected[0] = 1U;
    object_expected[1] = 1U;
    put_le16(&object_expected[2], 0x1234U);
    put_le32(&object_expected[4], 0x11223344U);
    put_le32(&object_expected[8], 0x55667788U);
    put_le16(&object_expected[12], 1U);
    put_le64(&object_expected[16], 0x0102030405060708ULL);
    put_le32(&object_expected[24], 0xa1a2a3a4U);
    put_le32(&object_expected[28], 0xb1b2b3b4U);
    check_true(boringfs_encode_object(object_bytes, sizeof(object_bytes),
                                      &object),
               "golden object encode");
    check_true(memcmp(object_bytes, object_expected,
                      sizeof(object_bytes)) == 0,
               "golden object exact offsets LE64 and zero reserve");
    check_true(boringfs_decode_object(object_expected,
                                      sizeof(object_expected),
                                      &decoded_object) &&
               decoded_object.size_bytes == 0x0102030405060708ULL &&
               decoded_object.extents[0].start_block == 0xa1a2a3a4U,
               "golden object independent decode");

    (void)memset(&record, 0, sizeof(record));
    record.object_id = 0x11223344U;
    record.type_hint = BORINGFS_TYPE_REGULAR;
    record.flags = 0U;
    set_record_name(&record, "abc", 3U);
    (void)memset(directory_bytes, 0xa5, sizeof(directory_bytes));
    (void)memset(directory_expected, 0, sizeof(directory_expected));
    put_le32(&directory_expected[0], 0x11223344U);
    put_le16(&directory_expected[4], 3U);
    directory_expected[6] = BORINGFS_TYPE_REGULAR;
    directory_expected[8] = (uint8_t)'a';
    directory_expected[9] = (uint8_t)'b';
    directory_expected[10] = (uint8_t)'c';
    check_true(boringfs_encode_directory_record(
                   directory_bytes, sizeof(directory_bytes), &record),
               "golden directory encode");
    check_true(memcmp(directory_bytes, directory_expected,
                      sizeof(directory_bytes)) == 0,
               "golden directory exact offsets and zero name tail");
    check_true(boringfs_decode_directory_record(
                   directory_expected, sizeof(directory_expected),
                   &decoded_record) &&
               decoded_record.object_id == 0x11223344U &&
               decoded_record.name_length == 3U &&
               decoded_record.name[2] == (uint8_t)'c',
               "golden directory independent decode");

    (void)puts("codec golden vectors: PASS");
}

static void utf8_tests(void) {
    static const uint8_t good_ascii[] = { 'a', 'b', 'c' };
    static const uint8_t good_four[] = { 0xf0U, 0x9fU, 0x98U, 0x80U };
    static const uint8_t bad_cont[] = { 0xc2U, 0x20U };
    static const uint8_t truncated[] = { 0xe2U, 0x82U };
    static const uint8_t overlong[] = { 0xc0U, 0x80U };
    static const uint8_t surrogate[] = { 0xedU, 0xa0U, 0x80U };
    static const uint8_t too_high[] = { 0xf4U, 0x90U, 0x80U, 0x80U };

    check_true(boringfs_utf8_valid(good_ascii, sizeof(good_ascii)),
               "UTF-8 ASCII valid");
    check_true(boringfs_utf8_valid(good_four, sizeof(good_four)),
               "UTF-8 U+1F600 valid");
    check_true(!boringfs_utf8_valid(bad_cont, sizeof(bad_cont)),
               "UTF-8 bad continuation rejected");
    check_true(!boringfs_utf8_valid(truncated, sizeof(truncated)),
               "UTF-8 truncated rejected");
    check_true(!boringfs_utf8_valid(overlong, sizeof(overlong)),
               "UTF-8 overlong rejected");
    check_true(!boringfs_utf8_valid(surrogate, sizeof(surrogate)),
               "UTF-8 surrogate rejected");
    check_true(!boringfs_utf8_valid(too_high, sizeof(too_high)),
               "UTF-8 > U+10FFFF rejected");
    (void)puts("UTF-8 validator: PASS");
}

static void fixture_and_workspace_tests(void) {
    struct boringfs_validation_error error;
    struct boringfs_validation_workspace too_small = {
        .block_owner = block_owner,
        .block_owner_count = 1U,
        .object_reference_count = object_reference_count,
        .object_reference_count_count = 1U
    };

    expect_result("valid empty volume", empty_volume, sizeof(empty_volume),
                  BORINGFS_VALIDATE_OK);
    expect_result("valid non-trivial volume", nontrivial_volume,
                  sizeof(nontrivial_volume), BORINGFS_VALIDATE_OK);

    (void)memcpy(working_volume, nontrivial_volume,
                 sizeof(nontrivial_volume));
    (void)memcpy(shadow_volume, nontrivial_volume,
                 sizeof(nontrivial_volume));
    expect_result("read-only input validation", working_volume,
                  sizeof(nontrivial_volume), BORINGFS_VALIDATE_OK);
    check_true(memcmp(working_volume, shadow_volume,
                      sizeof(nontrivial_volume)) == 0,
               "validator does not modify input bytes");

    check_true(boringfs_validate_volume(nontrivial_volume,
                                        sizeof(nontrivial_volume),
                                        &too_small, &error) ==
               BORINGFS_VALIDATE_INSUFFICIENT_WORKSPACE,
               "insufficient workspace rejected");
    (void)puts("valid fixtures and workspace: PASS");
}

static void superblock_corruption_tests(void) {
    reset_working(); working_volume[0] = (uint8_t)'X';
    expect_result("wrong magic", working_volume, sizeof(working_volume),
                  BORINGFS_VALIDATE_BAD_MAGIC);

    reset_working(); put_le16(&working_volume[8], 1U);
    expect_result("wrong major", working_volume, sizeof(working_volume),
                  BORINGFS_VALIDATE_UNSUPPORTED_VERSION);
    reset_working(); put_le16(&working_volume[10], 2U);
    expect_result("wrong minor", working_volume, sizeof(working_volume),
                  BORINGFS_VALIDATE_UNSUPPORTED_VERSION);
    reset_working(); put_le16(&working_volume[12], 127U);
    expect_result("wrong header size", working_volume, sizeof(working_volume),
                  BORINGFS_VALIDATE_BAD_SUPERBLOCK);
    reset_working(); working_volume[14] = 11U;
    expect_result("wrong block shift", working_volume, sizeof(working_volume),
                  BORINGFS_VALIDATE_BAD_SUPERBLOCK);
    reset_working(); working_volume[15] = 1U;
    expect_result("nonzero flags", working_volume, sizeof(working_volume),
                  BORINGFS_VALIDATE_BAD_SUPERBLOCK);
    reset_working(); put_le16(&working_volume[48], 64U);
    expect_result("bad object record size", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_SUPERBLOCK);
    reset_working(); put_le16(&working_volume[50], 128U);
    expect_result("bad directory record size", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_SUPERBLOCK);
    reset_working(); put_le32(&working_volume[52], 1U);
    expect_result("feature compat nonzero", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_UNSUPPORTED_FEATURE);
    reset_working(); put_le32(&working_volume[56], 1U);
    expect_result("feature ro compat nonzero", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_UNSUPPORTED_FEATURE);
    reset_working(); put_le32(&working_volume[60], 1U);
    expect_result("feature incompat nonzero", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_UNSUPPORTED_FEATURE);
    reset_working(); working_volume[64] = 1U;
    expect_result("reserved header byte", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_SUPERBLOCK);
    reset_working(); working_volume[128] = 1U;
    expect_result("superblock tail byte", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_SUPERBLOCK);
    reset_working(); put_le32(&working_volume[36], 63U);
    expect_result("object count too small", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_SUPERBLOCK);
    reset_working(); put_le32(&working_volume[36], 16385U);
    expect_result("object count too large", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_SUPERBLOCK);
    reset_working(); put_le32(&working_volume[40], 2U);
    expect_result("wrong root id", working_volume, sizeof(working_volume),
                  BORINGFS_VALIDATE_BAD_SUPERBLOCK);
    reset_working(); put_le32(&working_volume[44], 5U);
    expect_result("wrong layout field", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_LAYOUT);
    reset_working(); put_le32(&working_volume[16], 2U);
    expect_result("impossible metadata range", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_LAYOUT);
    expect_result("truncated declared volume", nontrivial_volume,
                  sizeof(nontrivial_volume) - 1U,
                  BORINGFS_VALIDATE_TRUNCATED_VOLUME);
    (void)puts("superblock corruption matrix: PASS");
}

static void bitmap_corruption_tests(void) {
    reset_working(); set_bitmap_bit(working_volume, 0U, false);
    expect_result("metadata block free", working_volume, sizeof(working_volume),
                  BORINGFS_VALIDATE_BAD_BITMAP);
    reset_working(); set_bitmap_bit(working_volume, FILE_DATA_BLOCK, false);
    expect_result("live extent block free", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_BITMAP);

    (void)memcpy(working_volume, empty_volume, sizeof(empty_volume));
    set_bitmap_bit(working_volume, 4U, false);
    expect_result("out of range tail bit free", working_volume,
                  sizeof(empty_volume), BORINGFS_VALIDATE_BAD_BITMAP);

    reset_working(); set_bitmap_bit(working_volume, 8U, true);
    expect_result("allocated unowned data block", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_ALLOCATION_LEAK);

    reset_working();
    put_le32(&working_volume[object_offset(3U) + 24U], ROOT_DATA_BLOCK0);
    expect_result("overlapping extent ownership", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_EXTENT_OVERLAP);
    (void)puts("bitmap corruption matrix: PASS");
}

static void object_corruption_tests(void) {
    reset_working(); working_volume[object_offset(20U) + 127U] = 1U;
    expect_result("free record nonzero", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_OBJECT_RECORD);
    reset_working(); working_volume[object_offset(4U)] = 2U;
    expect_result("invalid object state", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_OBJECT_RECORD);
    reset_working(); working_volume[object_offset(4U) + 1U] = 3U;
    expect_result("invalid object type", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_OBJECT_RECORD);
    reset_working(); put_le32(&working_volume[object_offset(4U) + 4U], 5U);
    expect_result("wrong object id", working_volume, sizeof(working_volume),
                  BORINGFS_VALIDATE_BAD_OBJECT_RECORD);
    reset_working(); put_le32(&working_volume[object_offset(4U) + 8U], 0U);
    expect_result("invalid parent zero", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_OBJECT_RECORD);
    reset_working(); put_le32(&working_volume[object_offset(4U) + 8U], 65U);
    expect_result("invalid parent out of range", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_OBJECT_RECORD);
    reset_working(); put_le32(&working_volume[object_offset(4U) + 8U], 3U);
    expect_result("non-directory parent", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_OBJECT_RECORD);
    reset_working(); working_volume[object_offset(1U) + 1U] = BORINGFS_TYPE_REGULAR;
    expect_result("root wrong type", working_volume, sizeof(working_volume),
                  BORINGFS_VALIDATE_BAD_OBJECT_RECORD);
    reset_working(); put_le32(&working_volume[object_offset(1U) + 8U], 2U);
    expect_result("root wrong parent", working_volume, sizeof(working_volume),
                  BORINGFS_VALIDATE_BAD_OBJECT_RECORD);
    reset_working(); put_le16(&working_volume[object_offset(4U) + 2U], 1U);
    expect_result("object flags nonzero", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_OBJECT_RECORD);
    reset_working(); put_le16(&working_volume[object_offset(3U) + 12U], 9U);
    expect_result("extent count greater than eight", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_OBJECT_RECORD);
    reset_working(); put_le32(&working_volume[object_offset(3U) + 32U], 8U);
    put_le32(&working_volume[object_offset(3U) + 36U], 1U);
    expect_result("unused extent nonzero", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_OBJECT_RECORD);
    reset_working(); working_volume[object_offset(3U) + 14U] = 1U;
    expect_result("object small reserved nonzero", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_OBJECT_RECORD);
    reset_working(); working_volume[object_offset(3U) + 88U] = 1U;
    expect_result("object reserved tail nonzero", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_OBJECT_RECORD);
    reset_working(); put_le64(&working_volume[object_offset(3U) + 16U], 4097ULL);
    expect_result("size exceeds extent capacity", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_OBJECT_RECORD);
    (void)puts("object corruption matrix: PASS");
}

static void extent_corruption_tests(void) {
    reset_working(); put_le32(&working_volume[object_offset(3U) + 28U], 0U);
    expect_result("zero extent block count", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_EXTENT);
    reset_working(); put_le32(&working_volume[object_offset(3U) + 24U], 3U);
    expect_result("extent before data start", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_EXTENT);
    reset_working(); put_le32(&working_volume[object_offset(3U) + 24U], 8U);
    put_le32(&working_volume[object_offset(3U) + 28U], 2U);
    expect_result("extent past total blocks", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_EXTENT);
    reset_working(); put_le32(&working_volume[object_offset(3U) + 24U], UINT32_MAX);
    put_le32(&working_volume[object_offset(3U) + 28U], 2U);
    expect_result("extent start plus count overflow", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_EXTENT);
    reset_working(); put_le32(&working_volume[object_offset(1U) + 32U], ROOT_DATA_BLOCK0);
    put_le32(&working_volume[object_offset(1U) + 36U], 1U);
    expect_result("two live extents overlap", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_EXTENT_OVERLAP);
    (void)puts("extent corruption matrix: PASS");
}

static void directory_corruption_tests(void) {
    size_t offset;

    reset_working();
    put_le64(&working_volume[object_offset(2U) + 16U], 257ULL);
    expect_result("directory size not multiple 256", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_DIRECTORY_RECORD);

    reset_working();
    put_le64(&working_volume[object_offset(2U) + 16U], 512ULL);
    working_volume[child_record_offset(1ULL) + 255U] = 1U;
    expect_result("unused directory record nonzero", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_DIRECTORY_RECORD);

    offset = root_record_offset(0ULL);
    reset_working(); put_le32(&working_volume[offset], 0U);
    expect_result("live shaped record target zero", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_DIRECTORY_RECORD);
    reset_working(); put_le32(&working_volume[offset], 65U);
    expect_result("directory target out of range", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_DIRECTORY_RECORD);
    reset_working(); put_le32(&working_volume[offset], 20U);
    working_volume[offset + 6U] = BORINGFS_TYPE_REGULAR;
    expect_result("directory target free", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_DIRECTORY_RECORD);

    offset = root_record_offset(1ULL);
    reset_working(); working_volume[offset + 6U] = BORINGFS_TYPE_DIRECTORY;
    expect_result("directory type mismatch", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_DIRECTORY_RECORD);
    reset_working(); working_volume[offset + 6U] = 3U;
    expect_result("invalid directory type hint", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_DIRECTORY_RECORD);

    offset = root_record_offset(0ULL);
    reset_working(); put_le16(&working_volume[offset + 4U], 0U);
    expect_result("zero directory name length", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_DIRECTORY_RECORD);
    reset_working(); put_le16(&working_volume[offset + 4U], 241U);
    expect_result("directory name too long", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_DIRECTORY_RECORD);
    reset_working(); working_volume[offset + 9U] = 0U;
    expect_result("directory embedded NUL", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_DIRECTORY_RECORD);
    reset_working(); working_volume[offset + 9U] = (uint8_t)'/';
    expect_result("directory slash", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_DIRECTORY_RECORD);

    reset_working();
    put_le16(&working_volume[offset + 4U], 1U);
    (void)memset(&working_volume[offset + 8U], 0,
                 (size_t)BORINGFS_MAX_FILENAME);
    working_volume[offset + 8U] = (uint8_t)'.';
    expect_result("directory dot name", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_DIRECTORY_RECORD);

    reset_working();
    put_le16(&working_volume[offset + 4U], 2U);
    (void)memset(&working_volume[offset + 8U], 0,
                 (size_t)BORINGFS_MAX_FILENAME);
    working_volume[offset + 8U] = (uint8_t)'.';
    working_volume[offset + 9U] = (uint8_t)'.';
    expect_result("directory dotdot name", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_DIRECTORY_RECORD);

    reset_working();
    put_le16(&working_volume[offset + 4U], 2U);
    (void)memset(&working_volume[offset + 8U], 0,
                 (size_t)BORINGFS_MAX_FILENAME);
    working_volume[offset + 8U] = 0xc0U;
    working_volume[offset + 9U] = 0x80U;
    expect_result("directory invalid UTF-8", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_INVALID_UTF8);

    reset_working(); working_volume[offset + 8U + 5U] = (uint8_t)'x';
    expect_result("directory name tail nonzero", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_DIRECTORY_RECORD);
    reset_working(); working_volume[offset + 7U] = 1U;
    expect_result("directory flags nonzero", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_DIRECTORY_RECORD);
    reset_working(); working_volume[offset + 248U] = 1U;
    expect_result("directory reserved nonzero", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_BAD_DIRECTORY_RECORD);

    reset_working();
    put_le16(&working_volume[offset + 4U], 4U);
    (void)memset(&working_volume[offset + 8U], 0,
                 (size_t)BORINGFS_MAX_FILENAME);
    (void)memcpy(&working_volume[offset + 8U], "File", 4U);
    expect_result("duplicate directory name", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_DUPLICATE_NAME);
    (void)puts("directory corruption matrix: PASS");
}

static void ownership_corruption_tests(void) {
    reset_working();
    (void)memset(&working_volume[root_record_offset(16ULL)], 0,
                 (size_t)BORINGFS_DIRECTORY_RECORD_SIZE);
    expect_result("orphan allocated object", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_ORPHAN_OBJECT);

    reset_working();
    put_le32(&working_volume[root_record_offset(2ULL)], 3U);
    expect_result("object referenced twice", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_MULTIPLE_REFERENCE);

    reset_working();
    put_le32(&working_volume[object_offset(4U) + 8U], 2U);
    expect_result("parent object disagreement", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_PARENT_MISMATCH);

    reset_working();
    put_le32(&working_volume[object_offset(2U) + 8U], 2U);
    expect_result("directory parent cycle", working_volume,
                  sizeof(working_volume), BORINGFS_VALIDATE_DIRECTORY_CYCLE);
    (void)puts("ownership and cycle matrix: PASS");
}

static void truncation_tests(void) {
    size_t length;

    for (length = 0U; length < sizeof(empty_volume); ++length) {
        struct boringfs_validation_error first_error;
        struct boringfs_validation_error second_error;
        const enum boringfs_validation_result first =
            validate_with_error(empty_volume, length, &first_error);
        const enum boringfs_validation_result second =
            validate_with_error(empty_volume, length, &second_error);

        if ((first == BORINGFS_VALIDATE_OK) || (second != first) ||
            (first_error.code != second_error.code) ||
            (first_error.object_id != second_error.object_id) ||
            (first_error.block != second_error.block) ||
            (first_error.directory_record_index !=
             second_error.directory_record_index)) {
            (void)fprintf(stderr,
                          "FAIL: truncation prefix %zu was not deterministic\n",
                          length);
            ++failures;
            break;
        }
    }
    expect_result("full empty fixture after prefix sweep", empty_volume,
                  sizeof(empty_volume), BORINGFS_VALIDATE_OK);
    (void)puts("truncation prefix sweep: PASS");
}

int main(void) {
    if (!build_empty_fixture() || !build_nontrivial_fixture()) {
        (void)fprintf(stderr, "fixture construction failed\n");
        return 1;
    }

    golden_codec_tests();
    utf8_tests();
    fixture_and_workspace_tests();
    superblock_corruption_tests();
    bitmap_corruption_tests();
    object_corruption_tests();
    extent_corruption_tests();
    directory_corruption_tests();
    ownership_corruption_tests();
    truncation_tests();

    if (failures != 0) {
        (void)fprintf(stderr, "BoringFS host tests FAILED: %d failure(s)\n",
                      failures);
        return 1;
    }
    (void)puts("BoringFS v0 codec and structural validator tests passed.");
    return 0;
}
