#define _POSIX_C_SOURCE 200809L

#include <boring/boringfs.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

struct boringfs_geometry {
    uint32_t total_blocks;
    uint32_t object_count;
    uint32_t bitmap_blocks;
    uint32_t object_table_start;
    uint32_t object_table_blocks;
    uint32_t data_start;
    uint64_t volume_bytes;
};

struct cli_options {
    const char *output_path;
    uint32_t total_blocks;
    uint32_t object_count;
};

static void print_usage(FILE *stream, const char *program) {
    (void)fprintf(stream,
                  "Usage: %s --blocks <count> --objects <count> <image>\n",
                  program);
}

static bool parse_u64_decimal(const char *text, uint64_t *value_out) {
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
    *value_out = value;
    return true;
}

static bool parse_cli(int argc, char **argv, struct cli_options *options) {
    uint64_t blocks = 0ULL;
    uint64_t objects = 0ULL;
    bool have_blocks = false;
    bool have_objects = false;
    const char *output = NULL;
    int index;

    if ((argv == NULL) || (options == NULL) || (argc < 1)) {
        return false;
    }
    if ((argc == 2) && (strcmp(argv[1], "--help") == 0)) {
        print_usage(stdout, argv[0]);
        exit(EXIT_SUCCESS);
    }

    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--blocks") == 0) {
            if (have_blocks || ((index + 1) >= argc) ||
                !parse_u64_decimal(argv[index + 1], &blocks)) {
                return false;
            }
            have_blocks = true;
            ++index;
        } else if (strcmp(argv[index], "--objects") == 0) {
            if (have_objects || ((index + 1) >= argc) ||
                !parse_u64_decimal(argv[index + 1], &objects)) {
                return false;
            }
            have_objects = true;
            ++index;
        } else if ((argv[index][0] == '-') || (output != NULL)) {
            return false;
        } else {
            output = argv[index];
        }
    }

    if (!have_blocks || !have_objects || (output == NULL) ||
        (blocks == 0ULL) || (blocks > (uint64_t)BORINGFS_MAX_BLOCKS) ||
        (objects < (uint64_t)BORINGFS_MIN_OBJECTS) ||
        (objects > (uint64_t)BORINGFS_MAX_OBJECTS)) {
        return false;
    }

    options->output_path = output;
    options->total_blocks = (uint32_t)blocks;
    options->object_count = (uint32_t)objects;
    return true;
}

static bool checked_mul_u64(uint64_t first, uint64_t second,
                            uint64_t *result_out) {
    if ((result_out == NULL) ||
        ((second != 0ULL) && (first > (UINT64_MAX / second)))) {
        return false;
    }
    *result_out = first * second;
    return true;
}

static bool ceil_div_u64(uint64_t value, uint64_t divisor,
                         uint64_t *result_out) {
    uint64_t result;

    if ((divisor == 0ULL) || (result_out == NULL)) {
        return false;
    }
    result = value / divisor;
    if ((value % divisor) != 0ULL) {
        if (result == UINT64_MAX) {
            return false;
        }
        ++result;
    }
    *result_out = result;
    return true;
}

static bool calculate_geometry(uint32_t total_blocks, uint32_t object_count,
                               struct boringfs_geometry *geometry) {
    uint64_t bitmap_blocks;
    uint64_t object_bytes;
    uint64_t object_table_blocks;
    uint64_t object_table_start;
    uint64_t data_start;
    uint64_t volume_bytes;

    if ((geometry == NULL) || (total_blocks == 0U) ||
        (total_blocks > BORINGFS_MAX_BLOCKS) ||
        (object_count < BORINGFS_MIN_OBJECTS) ||
        (object_count > BORINGFS_MAX_OBJECTS) ||
        !ceil_div_u64((uint64_t)total_blocks,
                      (uint64_t)BORINGFS_BITMAP_BLOCK_BITS,
                      &bitmap_blocks) ||
        !checked_mul_u64((uint64_t)object_count,
                         (uint64_t)BORINGFS_OBJECT_RECORD_SIZE,
                         &object_bytes) ||
        !ceil_div_u64(object_bytes, (uint64_t)BORINGFS_BLOCK_SIZE,
                      &object_table_blocks)) {
        return false;
    }

    object_table_start = 1ULL + bitmap_blocks;
    if (object_table_start > UINT32_MAX) {
        return false;
    }
    data_start = object_table_start + object_table_blocks;
    if ((data_start > (uint64_t)total_blocks) ||
        (data_start > UINT32_MAX) ||
        !checked_mul_u64((uint64_t)total_blocks,
                         (uint64_t)BORINGFS_BLOCK_SIZE,
                         &volume_bytes)) {
        return false;
    }

    geometry->total_blocks = total_blocks;
    geometry->object_count = object_count;
    geometry->bitmap_blocks = (uint32_t)bitmap_blocks;
    geometry->object_table_start = (uint32_t)object_table_start;
    geometry->object_table_blocks = (uint32_t)object_table_blocks;
    geometry->data_start = (uint32_t)data_start;
    geometry->volume_bytes = volume_bytes;
    return true;
}

static void set_bitmap_bit(uint8_t *bitmap, uint64_t bit_index) {
    const size_t byte_index = (size_t)(bit_index / 8ULL);
    const unsigned int shift = (unsigned int)(bit_index % 8ULL);

    bitmap[byte_index] = (uint8_t)(bitmap[byte_index] |
                                   (uint8_t)(1U << shift));
}

static bool initialize_volume(uint8_t *volume, size_t volume_size,
                              const struct boringfs_geometry *geometry) {
    static const uint8_t magic[BORINGFS_MAGIC_SIZE] = {
        'B', 'O', 'R', 'I', 'N', 'G', 'F', 'S'
    };
    struct boringfs_superblock superblock = { 0 };
    struct boringfs_object root = { 0 };
    uint8_t *bitmap;
    uint64_t bitmap_capacity_bits;
    uint64_t bit_index;
    size_t root_offset;

    if ((volume == NULL) || (geometry == NULL) ||
        (geometry->volume_bytes != (uint64_t)volume_size)) {
        return false;
    }

    (void)memset(volume, 0, volume_size);
    (void)memcpy(superblock.magic, magic, (size_t)BORINGFS_MAGIC_SIZE);
    superblock.format_major = (uint16_t)BORINGFS_FORMAT_MAJOR;
    superblock.format_minor = (uint16_t)BORINGFS_FORMAT_MINOR;
    superblock.header_size = (uint16_t)BORINGFS_SUPERBLOCK_HEADER_SIZE;
    superblock.block_shift = (uint8_t)BORINGFS_BLOCK_SHIFT;
    superblock.flags = 0U;
    superblock.total_blocks = geometry->total_blocks;
    superblock.bitmap_start = 1U;
    superblock.bitmap_blocks = geometry->bitmap_blocks;
    superblock.object_table_start = geometry->object_table_start;
    superblock.object_table_blocks = geometry->object_table_blocks;
    superblock.object_count = geometry->object_count;
    superblock.root_object_id = BORINGFS_ROOT_OBJECT_ID;
    superblock.data_start = geometry->data_start;
    superblock.object_record_size = (uint16_t)BORINGFS_OBJECT_RECORD_SIZE;
    superblock.directory_record_size = (uint16_t)BORINGFS_DIRECTORY_RECORD_SIZE;
    superblock.feature_compat = 0U;
    superblock.feature_ro_compat = 0U;
    superblock.feature_incompat = 0U;

    if (!boringfs_encode_superblock(volume, volume_size, &superblock)) {
        return false;
    }

    bitmap = &volume[BORINGFS_BLOCK_SIZE];
    if (!checked_mul_u64((uint64_t)geometry->bitmap_blocks,
                         (uint64_t)BORINGFS_BITMAP_BLOCK_BITS,
                         &bitmap_capacity_bits)) {
        return false;
    }
    for (bit_index = 0ULL; bit_index < bitmap_capacity_bits; ++bit_index) {
        if ((bit_index < (uint64_t)geometry->data_start) ||
            (bit_index >= (uint64_t)geometry->total_blocks)) {
            set_bitmap_bit(bitmap, bit_index);
        }
    }

    root.state = BORINGFS_OBJECT_ALLOCATED;
    root.type = BORINGFS_TYPE_DIRECTORY;
    root.flags = 0U;
    root.object_id = BORINGFS_ROOT_OBJECT_ID;
    root.parent_object_id = BORINGFS_ROOT_OBJECT_ID;
    root.extent_count = 0U;
    root.size_bytes = 0ULL;

    root_offset = (size_t)geometry->object_table_start *
                  (size_t)BORINGFS_BLOCK_SIZE;
    if ((root_offset > volume_size) ||
        ((size_t)BORINGFS_OBJECT_RECORD_SIZE > (volume_size - root_offset)) ||
        !boringfs_encode_object(&volume[root_offset],
                                (size_t)BORINGFS_OBJECT_RECORD_SIZE,
                                &root)) {
        return false;
    }
    return true;
}

static bool validate_volume(const uint8_t *volume, size_t volume_size,
                            const struct boringfs_geometry *geometry) {
    struct boringfs_validation_workspace workspace = { 0 };
    struct boringfs_validation_error error = { 0 };
    enum boringfs_validation_result result;
    size_t owner_bytes;

    if ((volume == NULL) || (geometry == NULL)) {
        return false;
    }
    owner_bytes = (size_t)geometry->total_blocks * sizeof(uint32_t);
    workspace.block_owner = (uint32_t *)malloc(owner_bytes);
    workspace.object_reference_count =
        (uint8_t *)malloc((size_t)geometry->object_count);
    if ((workspace.block_owner == NULL) ||
        (workspace.object_reference_count == NULL)) {
        free(workspace.block_owner);
        free(workspace.object_reference_count);
        return false;
    }
    workspace.block_owner_count = (size_t)geometry->total_blocks;
    workspace.object_reference_count_count = (size_t)geometry->object_count;

    result = boringfs_validate_volume(volume, volume_size, &workspace, &error);
    free(workspace.block_owner);
    free(workspace.object_reference_count);
    if (result != BORINGFS_VALIDATE_OK) {
        (void)fprintf(stderr,
                      "mkboringfs: shared validator rejected image: %s"
                      " (object=%" PRIu32 ", block=%" PRIu32
                      ", directory-record=%" PRIu64 ")\n",
                      boringfs_validation_result_name(result),
                      error.object_id, error.block,
                      error.directory_record_index);
        return false;
    }
    return true;
}

static char *make_temp_template(const char *output_path) {
    static const char suffix[] = ".tmp.XXXXXX";
    size_t output_length;
    size_t allocation_size;
    char *template_path;

    if (output_path == NULL) {
        return NULL;
    }
    output_length = strlen(output_path);
    if (output_length > (SIZE_MAX - sizeof(suffix))) {
        return NULL;
    }
    allocation_size = output_length + sizeof(suffix);
    template_path = (char *)malloc(allocation_size);
    if (template_path == NULL) {
        return NULL;
    }
    (void)memcpy(template_path, output_path, output_length);
    (void)memcpy(&template_path[output_length], suffix, sizeof(suffix));
    return template_path;
}

static int format_image(const struct cli_options *options,
                        const struct boringfs_geometry *geometry) {
    char *temp_path = NULL;
    int fd = -1;
    void *mapping = MAP_FAILED;
    size_t volume_size;
    off_t file_size;
    bool temp_created = false;
    int status = EXIT_FAILURE;

    if ((options == NULL) || (geometry == NULL) ||
        (geometry->volume_bytes > (uint64_t)SIZE_MAX)) {
        (void)fprintf(stderr, "mkboringfs: image size is not addressable\n");
        return EXIT_FAILURE;
    }
    volume_size = (size_t)geometry->volume_bytes;
    file_size = (off_t)geometry->volume_bytes;
    if ((file_size < (off_t)0) || ((uint64_t)file_size != geometry->volume_bytes)) {
        (void)fprintf(stderr, "mkboringfs: image size is not representable\n");
        return EXIT_FAILURE;
    }

    temp_path = make_temp_template(options->output_path);
    if (temp_path == NULL) {
        (void)fprintf(stderr, "mkboringfs: cannot construct temporary path\n");
        goto cleanup;
    }
    fd = mkstemp(temp_path);
    if (fd < 0) {
        (void)fprintf(stderr, "mkboringfs: cannot create output: %s\n",
                      strerror(errno));
        goto cleanup;
    }
    temp_created = true;
    if (ftruncate(fd, file_size) != 0) {
        (void)fprintf(stderr, "mkboringfs: cannot size output: %s\n",
                      strerror(errno));
        goto cleanup;
    }
    mapping = mmap(NULL, volume_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapping == MAP_FAILED) {
        (void)fprintf(stderr, "mkboringfs: cannot map output: %s\n",
                      strerror(errno));
        goto cleanup;
    }

    if (!initialize_volume((uint8_t *)mapping, volume_size, geometry)) {
        (void)fprintf(stderr, "mkboringfs: failed to encode BoringFS v0 image\n");
        goto cleanup;
    }
    if (!validate_volume((const uint8_t *)mapping, volume_size, geometry)) {
        goto cleanup;
    }
    if (msync(mapping, volume_size, MS_SYNC) != 0) {
        (void)fprintf(stderr, "mkboringfs: cannot flush output: %s\n",
                      strerror(errno));
        goto cleanup;
    }
    if (munmap(mapping, volume_size) != 0) {
        mapping = MAP_FAILED;
        (void)fprintf(stderr, "mkboringfs: cannot unmap output: %s\n",
                      strerror(errno));
        goto cleanup;
    }
    mapping = MAP_FAILED;
    if (fsync(fd) != 0) {
        (void)fprintf(stderr, "mkboringfs: cannot sync output: %s\n",
                      strerror(errno));
        goto cleanup;
    }
    if (close(fd) != 0) {
        fd = -1;
        (void)fprintf(stderr, "mkboringfs: cannot close output: %s\n",
                      strerror(errno));
        goto cleanup;
    }
    fd = -1;
    if (rename(temp_path, options->output_path) != 0) {
        (void)fprintf(stderr, "mkboringfs: cannot publish output: %s\n",
                      strerror(errno));
        goto cleanup;
    }
    temp_created = false;
    status = EXIT_SUCCESS;

cleanup:
    if (mapping != MAP_FAILED) {
        (void)munmap(mapping, volume_size);
    }
    if (fd >= 0) {
        (void)close(fd);
    }
    if (temp_created && (temp_path != NULL)) {
        (void)unlink(temp_path);
    }
    free(temp_path);
    return status;
}

int main(int argc, char **argv) {
    struct cli_options options = { 0 };
    struct boringfs_geometry geometry = { 0 };

    if (!parse_cli(argc, argv, &options)) {
        print_usage(stderr, (argc > 0) ? argv[0] : "mkboringfs");
        return EXIT_FAILURE;
    }
    if (!calculate_geometry(options.total_blocks, options.object_count,
                            &geometry)) {
        (void)fprintf(stderr,
                      "mkboringfs: requested geometry is invalid or too small\n");
        return EXIT_FAILURE;
    }
    if (format_image(&options, &geometry) != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    (void)printf("mkboringfs: created %s\n", options.output_path);
    (void)printf("  blocks: %" PRIu32 "\n", geometry.total_blocks);
    (void)printf("  objects: %" PRIu32 "\n", geometry.object_count);
    (void)printf("  bitmap blocks: %" PRIu32 "\n", geometry.bitmap_blocks);
    (void)printf("  object table start: %" PRIu32 "\n",
                 geometry.object_table_start);
    (void)printf("  object table blocks: %" PRIu32 "\n",
                 geometry.object_table_blocks);
    (void)printf("  data start: %" PRIu32 "\n", geometry.data_start);
    (void)printf("  bytes: %" PRIu64 "\n", geometry.volume_bytes);
    (void)printf("  validator: %s\n",
                 boringfs_validation_result_name(BORINGFS_VALIDATE_OK));
    return EXIT_SUCCESS;
}
