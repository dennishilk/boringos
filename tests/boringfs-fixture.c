#include <boring/boringfs.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIXTURE_BLOCKS 64U
#define FIXTURE_OBJECTS BORINGFS_MIN_OBJECTS
#define ROOT_BLOCK 4U
#define DOCS_BLOCK 5U
#define HOME_BLOCK 6U
#define DENNIS_BLOCK 7U
#define SYSTEM_BLOCK 8U
#define README_BLOCK 9U
#define HELLO_BLOCK 10U
#define ARCH_BLOCK0 11U
#define WELCOME_BLOCK 12U
#define VERSION_BLOCK 13U
#define ARCH_BLOCK1 14U
#define BIN_BLOCK 15U
#define BORINGFETCH_BLOCK0 16U
#define ARCH_SIZE 4200U

static const char readme_text[] = "Welcome to BoringOS.\n";
static const char hello_text[] = "Hello from BoringFS on VirtIO.\n";
static const char welcome_text[] =
    "This file came from a real BoringFS volume.\n";
static const char version_text[] = "BoringOS BoringFS fixture v0.1\n";

static void fill_magic(uint8_t magic[BORINGFS_MAGIC_SIZE]) {
    static const uint8_t value[BORINGFS_MAGIC_SIZE] = {
        'B', 'O', 'R', 'I', 'N', 'G', 'F', 'S'
    };
    (void)memcpy(magic, value, sizeof(value));
}

static void fill_superblock(struct boringfs_superblock *superblock) {
    const uint32_t bitmap_blocks =
        (FIXTURE_BLOCKS + (BORINGFS_BITMAP_BLOCK_BITS - 1U)) /
        BORINGFS_BITMAP_BLOCK_BITS;
    const uint32_t object_table_blocks =
        (uint32_t)(((uint64_t)FIXTURE_OBJECTS *
                    (uint64_t)BORINGFS_OBJECT_RECORD_SIZE +
                    (uint64_t)BORINGFS_BLOCK_SIZE - 1ULL) /
                   (uint64_t)BORINGFS_BLOCK_SIZE);

    (void)memset(superblock, 0, sizeof(*superblock));
    fill_magic(superblock->magic);
    superblock->format_major = BORINGFS_FORMAT_MAJOR;
    superblock->format_minor = BORINGFS_FORMAT_MINOR;
    superblock->header_size = BORINGFS_SUPERBLOCK_HEADER_SIZE;
    superblock->block_shift = BORINGFS_BLOCK_SHIFT;
    superblock->total_blocks = FIXTURE_BLOCKS;
    superblock->bitmap_start = 1U;
    superblock->bitmap_blocks = bitmap_blocks;
    superblock->object_table_start = 1U + bitmap_blocks;
    superblock->object_table_blocks = object_table_blocks;
    superblock->object_count = FIXTURE_OBJECTS;
    superblock->root_object_id = BORINGFS_ROOT_OBJECT_ID;
    superblock->data_start = superblock->object_table_start +
                             object_table_blocks;
    superblock->object_record_size = BORINGFS_OBJECT_RECORD_SIZE;
    superblock->directory_record_size = BORINGFS_DIRECTORY_RECORD_SIZE;
}

static void bitmap_set(uint8_t *volume, uint32_t block, bool allocated) {
    const size_t offset = (size_t)BORINGFS_BLOCK_SIZE +
                          ((size_t)block / 8U);
    const uint8_t mask = (uint8_t)(1U << (block % 8U));

    if (allocated) {
        volume[offset] = (uint8_t)(volume[offset] | mask);
    } else {
        volume[offset] = (uint8_t)(volume[offset] & (uint8_t)~mask);
    }
}

static size_t object_offset(const struct boringfs_superblock *superblock,
                            uint32_t object_id) {
    return ((size_t)superblock->object_table_start *
            (size_t)BORINGFS_BLOCK_SIZE) +
           ((size_t)(object_id - 1U) *
            (size_t)BORINGFS_OBJECT_RECORD_SIZE);
}

static bool write_object(uint8_t *volume,
                         size_t volume_size,
                         const struct boringfs_superblock *superblock,
                         const struct boringfs_object *object) {
    const size_t offset = object_offset(superblock, object->object_id);

    return (offset <= volume_size) &&
           ((size_t)BORINGFS_OBJECT_RECORD_SIZE <= volume_size - offset) &&
           boringfs_encode_object(&volume[offset],
                                  (size_t)BORINGFS_OBJECT_RECORD_SIZE,
                                  object);
}

static bool make_object(uint8_t *volume,
                        size_t volume_size,
                        const struct boringfs_superblock *superblock,
                        uint32_t object_id,
                        uint32_t parent_id,
                        uint8_t type,
                        uint64_t size_bytes,
                        const struct boringfs_extent *extents,
                        uint16_t extent_count) {
    struct boringfs_object object;
    size_t index;

    (void)memset(&object, 0, sizeof(object));
    object.state = BORINGFS_OBJECT_ALLOCATED;
    object.type = type;
    object.object_id = object_id;
    object.parent_object_id = parent_id;
    object.size_bytes = size_bytes;
    object.extent_count = extent_count;
    for (index = 0U; index < (size_t)extent_count; ++index) {
        object.extents[index] = extents[index];
    }
    return write_object(volume, volume_size, superblock, &object);
}

static void set_name(struct boringfs_directory_record *record,
                     const char *name) {
    const size_t length = strlen(name);

    (void)memset(record->name, 0, sizeof(record->name));
    (void)memcpy(record->name, name, length);
    record->name_length = (uint16_t)length;
}

static bool write_dirent(uint8_t *volume,
                         size_t volume_size,
                         uint32_t directory_block,
                         uint64_t index,
                         uint32_t object_id,
                         uint8_t type,
                         const char *name) {
    struct boringfs_directory_record record;
    const uint64_t absolute =
        ((uint64_t)directory_block * (uint64_t)BORINGFS_BLOCK_SIZE) +
        (index * (uint64_t)BORINGFS_DIRECTORY_RECORD_SIZE);
    size_t offset;

    if ((absolute > (uint64_t)SIZE_MAX) ||
        ((uint64_t)BORINGFS_DIRECTORY_RECORD_SIZE >
         (uint64_t)SIZE_MAX - absolute)) {
        return false;
    }
    offset = (size_t)absolute;
    if ((offset > volume_size) ||
        ((size_t)BORINGFS_DIRECTORY_RECORD_SIZE > volume_size - offset)) {
        return false;
    }
    (void)memset(&record, 0, sizeof(record));
    record.object_id = object_id;
    record.type_hint = type;
    set_name(&record, name);
    return boringfs_encode_directory_record(
        &volume[offset], (size_t)BORINGFS_DIRECTORY_RECORD_SIZE, &record);
}

static bool build_valid(uint8_t *volume, size_t volume_size,
                        const uint8_t *program_bytes,
                        size_t program_size) {
    struct boringfs_superblock superblock;
    struct boringfs_extent extent[2];
    uint32_t block;
    uint32_t program_blocks = 0U;
    size_t index;
    const bool have_program = (program_bytes != NULL);

    if (have_program) {
        const size_t maximum_program_size =
            ((size_t)FIXTURE_BLOCKS - (size_t)BORINGFETCH_BLOCK0) *
            (size_t)BORINGFS_BLOCK_SIZE;
        size_t blocks;

        if ((program_size == 0U) ||
            (program_size > maximum_program_size)) {
            return false;
        }
        blocks = (program_size + (size_t)BORINGFS_BLOCK_SIZE - 1U) /
                 (size_t)BORINGFS_BLOCK_SIZE;
        if ((blocks == 0U) || (blocks > (size_t)UINT32_MAX) ||
            ((size_t)BORINGFETCH_BLOCK0 + blocks >
             (size_t)FIXTURE_BLOCKS)) {
            return false;
        }
        program_blocks = (uint32_t)blocks;
    } else if (program_size != 0U) {
        return false;
    }

    if (volume_size != (size_t)FIXTURE_BLOCKS *
                       (size_t)BORINGFS_BLOCK_SIZE) {
        return false;
    }
    (void)memset(volume, 0, volume_size);
    fill_superblock(&superblock);
    if ((superblock.data_start != ROOT_BLOCK) ||
        !boringfs_encode_superblock(volume, volume_size, &superblock)) {
        return false;
    }

    (void)memset(&volume[BORINGFS_BLOCK_SIZE], 0xff,
                 (size_t)BORINGFS_BLOCK_SIZE);
    for (block = superblock.data_start; block < FIXTURE_BLOCKS; ++block) {
        bitmap_set(volume, block, false);
    }
    for (block = ROOT_BLOCK; block <= ARCH_BLOCK1; ++block) {
        bitmap_set(volume, block, true);
    }
    if (have_program) {
        bitmap_set(volume, BIN_BLOCK, true);
        for (block = BORINGFETCH_BLOCK0;
             block < BORINGFETCH_BLOCK0 + program_blocks; ++block) {
            bitmap_set(volume, block, true);
        }
    }

    extent[0].start_block = ROOT_BLOCK;
    extent[0].block_count = 1U;
    if (!make_object(volume, volume_size, &superblock, 1U, 1U,
                     BORINGFS_TYPE_DIRECTORY,
                     (have_program ? 5ULL : 4ULL) *
                         (uint64_t)BORINGFS_DIRECTORY_RECORD_SIZE,
                     extent, 1U)) {
        return false;
    }
    extent[0].start_block = README_BLOCK;
    if (!make_object(volume, volume_size, &superblock, 2U, 1U,
                     BORINGFS_TYPE_REGULAR, sizeof(readme_text) - 1U,
                     extent, 1U)) {
        return false;
    }
    extent[0].start_block = DOCS_BLOCK;
    if (!make_object(volume, volume_size, &superblock, 3U, 1U,
                     BORINGFS_TYPE_DIRECTORY,
                     2ULL * (uint64_t)BORINGFS_DIRECTORY_RECORD_SIZE,
                     extent, 1U)) {
        return false;
    }
    extent[0].start_block = HELLO_BLOCK;
    if (!make_object(volume, volume_size, &superblock, 4U, 3U,
                     BORINGFS_TYPE_REGULAR, sizeof(hello_text) - 1U,
                     extent, 1U)) {
        return false;
    }
    extent[0].start_block = ARCH_BLOCK0;
    extent[1].start_block = ARCH_BLOCK1;
    extent[0].block_count = 1U;
    extent[1].block_count = 1U;
    if (!make_object(volume, volume_size, &superblock, 5U, 3U,
                     BORINGFS_TYPE_REGULAR, ARCH_SIZE, extent, 2U)) {
        return false;
    }
    extent[0].start_block = HOME_BLOCK;
    if (!make_object(volume, volume_size, &superblock, 6U, 1U,
                     BORINGFS_TYPE_DIRECTORY,
                     (uint64_t)BORINGFS_DIRECTORY_RECORD_SIZE,
                     extent, 1U)) {
        return false;
    }
    extent[0].start_block = DENNIS_BLOCK;
    if (!make_object(volume, volume_size, &superblock, 7U, 6U,
                     BORINGFS_TYPE_DIRECTORY,
                     (uint64_t)BORINGFS_DIRECTORY_RECORD_SIZE,
                     extent, 1U)) {
        return false;
    }
    extent[0].start_block = WELCOME_BLOCK;
    if (!make_object(volume, volume_size, &superblock, 8U, 7U,
                     BORINGFS_TYPE_REGULAR, sizeof(welcome_text) - 1U,
                     extent, 1U)) {
        return false;
    }
    extent[0].start_block = SYSTEM_BLOCK;
    if (!make_object(volume, volume_size, &superblock, 9U, 1U,
                     BORINGFS_TYPE_DIRECTORY,
                     (uint64_t)BORINGFS_DIRECTORY_RECORD_SIZE,
                     extent, 1U)) {
        return false;
    }
    extent[0].start_block = VERSION_BLOCK;
    if (!make_object(volume, volume_size, &superblock, 10U, 9U,
                     BORINGFS_TYPE_REGULAR, sizeof(version_text) - 1U,
                     extent, 1U)) {
        return false;
    }
    if (have_program) {
        extent[0].start_block = BIN_BLOCK;
        extent[0].block_count = 1U;
        if (!make_object(volume, volume_size, &superblock, 11U, 1U,
                         BORINGFS_TYPE_DIRECTORY,
                         (uint64_t)BORINGFS_DIRECTORY_RECORD_SIZE,
                         extent, 1U)) {
            return false;
        }
        extent[0].start_block = BORINGFETCH_BLOCK0;
        extent[0].block_count = program_blocks;
        if (!make_object(volume, volume_size, &superblock, 12U, 11U,
                         BORINGFS_TYPE_REGULAR,
                         (uint64_t)program_size, extent, 1U)) {
            return false;
        }
    }

    if (!write_dirent(volume, volume_size, ROOT_BLOCK, 0ULL, 2U,
                      BORINGFS_TYPE_REGULAR, "README.txt") ||
        !write_dirent(volume, volume_size, ROOT_BLOCK, 1ULL, 3U,
                      BORINGFS_TYPE_DIRECTORY, "docs") ||
        !write_dirent(volume, volume_size, ROOT_BLOCK, 2ULL, 6U,
                      BORINGFS_TYPE_DIRECTORY, "home") ||
        !write_dirent(volume, volume_size, ROOT_BLOCK, 3ULL, 9U,
                      BORINGFS_TYPE_DIRECTORY, "system") ||
        !write_dirent(volume, volume_size, DOCS_BLOCK, 0ULL, 4U,
                      BORINGFS_TYPE_REGULAR, "hello.txt") ||
        !write_dirent(volume, volume_size, DOCS_BLOCK, 1ULL, 5U,
                      BORINGFS_TYPE_REGULAR, "architecture.txt") ||
        !write_dirent(volume, volume_size, HOME_BLOCK, 0ULL, 7U,
                      BORINGFS_TYPE_DIRECTORY, "dennis") ||
        !write_dirent(volume, volume_size, DENNIS_BLOCK, 0ULL, 8U,
                      BORINGFS_TYPE_REGULAR, "welcome.txt") ||
        !write_dirent(volume, volume_size, SYSTEM_BLOCK, 0ULL, 10U,
                      BORINGFS_TYPE_REGULAR, "version.txt")) {
        return false;
    }
    if (have_program &&
        (!write_dirent(volume, volume_size, ROOT_BLOCK, 4ULL, 11U,
                       BORINGFS_TYPE_DIRECTORY, "bin") ||
         !write_dirent(volume, volume_size, BIN_BLOCK, 0ULL, 12U,
                       BORINGFS_TYPE_REGULAR, "boringfetch"))) {
        return false;
    }

    (void)memcpy(&volume[(size_t)README_BLOCK * BORINGFS_BLOCK_SIZE],
                 readme_text, sizeof(readme_text) - 1U);
    (void)memcpy(&volume[(size_t)HELLO_BLOCK * BORINGFS_BLOCK_SIZE],
                 hello_text, sizeof(hello_text) - 1U);
    (void)memcpy(&volume[(size_t)WELCOME_BLOCK * BORINGFS_BLOCK_SIZE],
                 welcome_text, sizeof(welcome_text) - 1U);
    (void)memcpy(&volume[(size_t)VERSION_BLOCK * BORINGFS_BLOCK_SIZE],
                 version_text, sizeof(version_text) - 1U);
    if (have_program) {
        (void)memcpy(
            &volume[(size_t)BORINGFETCH_BLOCK0 * BORINGFS_BLOCK_SIZE],
            program_bytes, program_size);
    }
    for (index = 0U; index < ARCH_SIZE; ++index) {
        const uint32_t block_number = (index < BORINGFS_BLOCK_SIZE) ?
            ARCH_BLOCK0 : ARCH_BLOCK1;
        const size_t in_block = index % (size_t)BORINGFS_BLOCK_SIZE;
        volume[(size_t)block_number * BORINGFS_BLOCK_SIZE + in_block] =
            (uint8_t)('A' + (char)(index % 26U));
    }
    return true;
}

static void corrupt(uint8_t *volume, const char *kind) {
    struct boringfs_superblock superblock;
    struct boringfs_object object;
    struct boringfs_directory_record record;
    size_t offset;

    if (strcmp(kind, "bad-magic") == 0) {
        volume[0] ^= 0x40U;
        return;
    }
    if (!boringfs_decode_superblock(volume, BORINGFS_BLOCK_SIZE,
                                    &superblock)) {
        return;
    }
    if (strcmp(kind, "bad-geometry") == 0) {
        superblock.data_start += 1U;
        (void)boringfs_encode_superblock(volume, BORINGFS_BLOCK_SIZE,
                                         &superblock);
        return;
    }
    if (strcmp(kind, "bad-bitmap") == 0) {
        bitmap_set(volume, ROOT_BLOCK, false);
        return;
    }
    if (strcmp(kind, "bad-object") == 0) {
        offset = object_offset(&superblock, 4U);
        if (boringfs_decode_object(&volume[offset],
                                   BORINGFS_OBJECT_RECORD_SIZE, &object)) {
            object.flags = 1U;
            (void)boringfs_encode_object(&volume[offset],
                                         BORINGFS_OBJECT_RECORD_SIZE,
                                         &object);
        }
        return;
    }
    if (strcmp(kind, "bad-extent") == 0) {
        offset = object_offset(&superblock, 4U);
        if (boringfs_decode_object(&volume[offset],
                                   BORINGFS_OBJECT_RECORD_SIZE, &object)) {
            object.extents[0].start_block = FIXTURE_BLOCKS;
            (void)boringfs_encode_object(&volume[offset],
                                         BORINGFS_OBJECT_RECORD_SIZE,
                                         &object);
        }
        return;
    }
    if (strcmp(kind, "bad-directory") == 0) {
        offset = (size_t)DOCS_BLOCK * BORINGFS_BLOCK_SIZE;
        if (boringfs_decode_directory_record(
                &volume[offset], BORINGFS_DIRECTORY_RECORD_SIZE, &record)) {
            record.object_id = FIXTURE_OBJECTS + 1U;
            (void)boringfs_encode_directory_record(
                &volume[offset], BORINGFS_DIRECTORY_RECORD_SIZE, &record);
        }
    }
}

static enum boringfs_validation_result validate_fixture(
    const uint8_t *volume, size_t volume_size) {
    uint32_t block_owner[FIXTURE_BLOCKS];
    uint8_t reference_count[FIXTURE_OBJECTS];
    const struct boringfs_validation_workspace workspace = {
        .block_owner = block_owner,
        .block_owner_count = FIXTURE_BLOCKS,
        .object_reference_count = reference_count,
        .object_reference_count_count = FIXTURE_OBJECTS
    };
    struct boringfs_validation_error error;

    return boringfs_validate_volume(volume, volume_size, &workspace, &error);
}

static bool read_program(const char *path,
                     uint8_t **bytes_out,
                     size_t *size_out) {
    const size_t capacity =
        ((size_t)FIXTURE_BLOCKS - (size_t)BORINGFETCH_BLOCK0) *
        (size_t)BORINGFS_BLOCK_SIZE;
    FILE *file;
    uint8_t *bytes;
    size_t size;

    if ((path == NULL) || (bytes_out == NULL) || (size_out == NULL)) {
        return false;
    }
    *bytes_out = NULL;
    *size_out = 0U;
    file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    bytes = (uint8_t *)malloc(capacity + 1U);
    if (bytes == NULL) {
        (void)fclose(file);
        return false;
    }
    size = fread(bytes, 1U, capacity + 1U, file);
    if (ferror(file) || (fclose(file) != 0) ||
        (size == 0U) || (size > capacity)) {
        free(bytes);
        return false;
    }
    *bytes_out = bytes;
    *size_out = size;
    return true;
}

static int write_image(const char *path, const uint8_t *volume, size_t size) {
    FILE *file = fopen(path, "wb");

    if (file == NULL) {
        (void)fprintf(stderr, "cannot open %s: %s\n", path, strerror(errno));
        return 2;
    }
    if ((fwrite(volume, 1U, size, file) != size) || (fclose(file) != 0)) {
        (void)fprintf(stderr, "cannot write %s\n", path);
        return 2;
    }
    return 0;
}

int main(int argc, char **argv) {
    const size_t volume_size =
        (size_t)FIXTURE_BLOCKS * (size_t)BORINGFS_BLOCK_SIZE;
    uint8_t *volume;
    uint8_t *program_bytes = NULL;
    size_t program_size = 0U;
    enum boringfs_validation_result result;
    const char *kind;
    int status;

    if ((argc != 3) && (argc != 4)) {
        (void)fprintf(stderr,
                      "usage: %s <output> <valid|bad-magic|bad-geometry|bad-bitmap|bad-object|bad-extent|bad-directory> [program-elf]\n",
                      argv[0]);
        return 2;
    }
    kind = argv[2];
    if ((argc == 4) && (strcmp(kind, "valid") != 0)) {
        (void)fputs("program ELF is supported only for valid fixtures\n",
                    stderr);
        return 2;
    }
    if ((strcmp(kind, "valid") != 0) &&
        (strcmp(kind, "bad-magic") != 0) &&
        (strcmp(kind, "bad-geometry") != 0) &&
        (strcmp(kind, "bad-bitmap") != 0) &&
        (strcmp(kind, "bad-object") != 0) &&
        (strcmp(kind, "bad-extent") != 0) &&
        (strcmp(kind, "bad-directory") != 0)) {
        (void)fprintf(stderr, "unknown fixture kind: %s\n", kind);
        return 2;
    }

    if ((argc == 4) &&
        !read_program(argv[3], &program_bytes, &program_size)) {
        (void)fprintf(stderr, "cannot read bounded program ELF: %s\n",
                      argv[3]);
        return 2;
    }
    volume = (uint8_t *)malloc(volume_size);
    if ((volume == NULL) ||
        !build_valid(volume, volume_size, program_bytes, program_size)) {
        free(program_bytes);
        free(volume);
        (void)fputs("fixture construction failed\n", stderr);
        return 2;
    }
    free(program_bytes);
    if (strcmp(kind, "valid") != 0) {
        corrupt(volume, kind);
    }
    result = validate_fixture(volume, volume_size);
    if (((strcmp(kind, "valid") == 0) &&
         (result != BORINGFS_VALIDATE_OK)) ||
        ((strcmp(kind, "valid") != 0) &&
         (result == BORINGFS_VALIDATE_OK))) {
        (void)fprintf(stderr, "fixture validation mismatch: %s -> %s\n",
                      kind, boringfs_validation_result_name(result));
        free(volume);
        return 2;
    }
    status = write_image(argv[1], volume, volume_size);
    free(volume);
    if (status == 0) {
        (void)printf("BoringFS fixture: %s (%s)\n", argv[1], kind);
    }
    return status;
}
