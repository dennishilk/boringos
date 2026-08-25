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
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define BORINGFSCK_EXIT_VALID 0
#define BORINGFSCK_EXIT_CORRUPT 1
#define BORINGFSCK_EXIT_HOST_FAILURE 2

static void print_usage(FILE *stream, const char *program) {
    (void)fprintf(stream,
                  "Usage: %s <image>\n"
                  "       %s --cat <absolute-path> <image>\n",
                  program, program);
}

static bool checked_mul_size(size_t left, size_t right, size_t *out) {
    if ((out == NULL) || ((left != 0U) && (right > (SIZE_MAX / left)))) {
        return false;
    }
    *out = left * right;
    return true;
}

static bool decoded_block_count_bounded(const struct boringfs_superblock *superblock) {
    return (superblock != NULL) && (superblock->total_blocks != 0U) &&
           (superblock->total_blocks <= BORINGFS_MAX_BLOCKS);
}

static bool decoded_object_count_bounded(const struct boringfs_superblock *superblock) {
    return (superblock != NULL) &&
           (superblock->object_count >= BORINGFS_MIN_OBJECTS) &&
           (superblock->object_count <= BORINGFS_MAX_OBJECTS);
}

static bool validator_result_is_host_failure(enum boringfs_validation_result result) {
    return (result == BORINGFS_VALIDATE_INVALID_ARGUMENT) ||
           (result == BORINGFS_VALIDATE_INSUFFICIENT_WORKSPACE);
}

static void print_valid(const char *path,
                        const struct boringfs_superblock *superblock) {
    (void)printf("BoringFS v%u.%u\n",
                 (unsigned int)superblock->format_major,
                 (unsigned int)superblock->format_minor);
    (void)printf("Image: %s\n", path);
    (void)printf("Blocks: %" PRIu32 "\n", superblock->total_blocks);
    (void)printf("Block size: %u\n", BORINGFS_BLOCK_SIZE);
    (void)printf("Objects: %" PRIu32 "\n", superblock->object_count);
    (void)printf("Root object: %" PRIu32 "\n", superblock->root_object_id);
    (void)printf("Data start: %" PRIu32 "\n", superblock->data_start);
    (void)printf("Status: VALID\n");
}

static void print_corrupt(const char *path,
                          bool decoded,
                          const struct boringfs_superblock *superblock,
                          enum boringfs_validation_result result,
                          const struct boringfs_validation_error *error) {
    if (decoded) {
        (void)printf("BoringFS v%u.%u\n",
                     (unsigned int)superblock->format_major,
                     (unsigned int)superblock->format_minor);
    } else {
        (void)printf("BoringFS\n");
    }
    (void)printf("Image: %s\n", path);
    (void)printf("Status: CORRUPT\n");
    (void)printf("Reason: %s\n", boringfs_validation_result_name(result));
    if (error->object_id != BORINGFS_LOCATION_NONE_U32) {
        (void)printf("Object: %" PRIu32 "\n", error->object_id);
    }
    if (error->block != BORINGFS_LOCATION_NONE_U32) {
        (void)printf("Block: %" PRIu32 "\n", error->block);
    }
    if (error->directory_record_index != BORINGFS_LOCATION_NONE_U64) {
        (void)printf("Directory record: %" PRIu64 "\n",
                     error->directory_record_index);
    }
}

static bool decode_object_id(const uint8_t *volume,
                             size_t volume_size,
                             const struct boringfs_superblock *superblock,
                             uint32_t object_id,
                             struct boringfs_object *object_out) {
    uint64_t offset64;
    size_t offset;

    if ((volume == NULL) || (superblock == NULL) || (object_out == NULL) ||
        (object_id == BORINGFS_NULL_OBJECT_ID) ||
        (object_id > superblock->object_count)) {
        return false;
    }
    offset64 = ((uint64_t)superblock->object_table_start *
                (uint64_t)BORINGFS_BLOCK_SIZE) +
               ((uint64_t)(object_id - 1U) *
                (uint64_t)BORINGFS_OBJECT_RECORD_SIZE);
    if ((offset64 > (uint64_t)SIZE_MAX) ||
        (offset64 > (uint64_t)volume_size) ||
        ((uint64_t)BORINGFS_OBJECT_RECORD_SIZE >
         (uint64_t)volume_size - offset64)) {
        return false;
    }
    offset = (size_t)offset64;
    return boringfs_decode_object(&volume[offset],
                                  (size_t)BORINGFS_OBJECT_RECORD_SIZE,
                                  object_out);
}

static bool read_object_bytes(const uint8_t *volume,
                              size_t volume_size,
                              const struct boringfs_object *object,
                              uint64_t logical_offset,
                              void *buffer,
                              size_t length) {
    uint8_t *destination = (uint8_t *)buffer;
    size_t remaining = length;
    uint64_t offset = logical_offset;
    size_t extent_index;

    if ((volume == NULL) || (object == NULL) ||
        ((destination == NULL) && (length != 0U)) ||
        (object->extent_count > BORINGFS_MAX_EXTENTS)) {
        return false;
    }
    for (extent_index = 0U;
         (extent_index < (size_t)object->extent_count) && (remaining != 0U);
         ++extent_index) {
        const struct boringfs_extent *const extent =
            &object->extents[extent_index];
        const uint64_t extent_bytes =
            (uint64_t)extent->block_count * (uint64_t)BORINGFS_BLOCK_SIZE;
        uint64_t physical;
        uint64_t available;
        size_t chunk;

        if (offset >= extent_bytes) {
            offset -= extent_bytes;
            continue;
        }
        physical = ((uint64_t)extent->start_block *
                    (uint64_t)BORINGFS_BLOCK_SIZE) + offset;
        available = extent_bytes - offset;
        chunk = remaining;
        if (available < (uint64_t)chunk) {
            chunk = (size_t)available;
        }
        if ((physical > (uint64_t)SIZE_MAX) ||
            (physical > (uint64_t)volume_size) ||
            ((uint64_t)chunk > (uint64_t)volume_size - physical)) {
            return false;
        }
        (void)memcpy(destination, &volume[(size_t)physical], chunk);
        destination += chunk;
        remaining -= chunk;
        offset = 0ULL;
    }
    return remaining == 0U;
}

static bool lookup_child(const uint8_t *volume,
                         size_t volume_size,
                         const struct boringfs_superblock *superblock,
                         uint32_t directory_id,
                         const char *name,
                         size_t name_length,
                         uint32_t *child_out) {
    struct boringfs_object directory;
    uint64_t record_count;
    uint64_t index;

    if ((name == NULL) || (child_out == NULL) ||
        !decode_object_id(volume, volume_size, superblock, directory_id,
                          &directory) ||
        (directory.type != BORINGFS_TYPE_DIRECTORY)) {
        return false;
    }
    record_count = directory.size_bytes /
                   (uint64_t)BORINGFS_DIRECTORY_RECORD_SIZE;
    for (index = 0ULL; index < record_count; ++index) {
        uint8_t raw[BORINGFS_DIRECTORY_RECORD_SIZE];
        struct boringfs_directory_record record;

        if (!read_object_bytes(
                volume, volume_size, &directory,
                index * (uint64_t)BORINGFS_DIRECTORY_RECORD_SIZE,
                raw, sizeof(raw)) ||
            !boringfs_decode_directory_record(raw, sizeof(raw), &record)) {
            return false;
        }
        if ((record.object_id != BORINGFS_NULL_OBJECT_ID) &&
            ((size_t)record.name_length == name_length) &&
            (memcmp(record.name, name, name_length) == 0)) {
            *child_out = record.object_id;
            return true;
        }
    }
    return false;
}

static bool cat_valid_path(const uint8_t *volume,
                           size_t volume_size,
                           const struct boringfs_superblock *superblock,
                           const char *path) {
    uint32_t object_id = BORINGFS_ROOT_OBJECT_ID;
    size_t path_length;
    size_t cursor = 0U;
    struct boringfs_object object;

    if ((volume == NULL) || (superblock == NULL) || (path == NULL) ||
        (path[0] != '/')) {
        (void)fprintf(stderr,
                      "boringfsck: --cat requires an absolute path\n");
        return false;
    }
    path_length = strlen(path);
    while (cursor < path_length) {
        size_t start;
        size_t length;

        while ((cursor < path_length) && (path[cursor] == '/')) {
            ++cursor;
        }
        if (cursor == path_length) {
            break;
        }
        start = cursor;
        while ((cursor < path_length) && (path[cursor] != '/')) {
            ++cursor;
        }
        length = cursor - start;
        if ((length == 0U) || (length > (size_t)BORINGFS_MAX_FILENAME) ||
            !lookup_child(volume, volume_size, superblock, object_id,
                          &path[start], length, &object_id)) {
            (void)fprintf(stderr, "boringfsck: path not found: %s\n", path);
            return false;
        }
    }
    if (!decode_object_id(volume, volume_size, superblock, object_id,
                          &object) ||
        (object.type != BORINGFS_TYPE_REGULAR)) {
        (void)fprintf(stderr, "boringfsck: not a regular file: %s\n", path);
        return false;
    }
    {
        uint8_t buffer[BORINGFS_BLOCK_SIZE];
        uint64_t offset = 0ULL;

        while (offset < object.size_bytes) {
            uint64_t remaining = object.size_bytes - offset;
            size_t chunk = sizeof(buffer);

            if (remaining < (uint64_t)chunk) {
                chunk = (size_t)remaining;
            }
            if (!read_object_bytes(volume, volume_size, &object, offset,
                                   buffer, chunk) ||
                (fwrite(buffer, 1U, chunk, stdout) != chunk)) {
                (void)fprintf(stderr,
                              "boringfsck: cannot read file data: %s\n",
                              path);
                return false;
            }
            offset += (uint64_t)chunk;
        }
    }
    return true;
}

static int inspect_image(const char *path, const char *cat_path) {
    struct stat file_stat;
    struct boringfs_superblock superblock = { 0 };
    struct boringfs_validation_workspace workspace = { 0 };
    struct boringfs_validation_error error = { 0 };
    enum boringfs_validation_result result;
    uint64_t file_size_u64;
    uint64_t max_volume_bytes;
    size_t volume_size = 0U;
    size_t block_owner_count = 1U;
    size_t object_reference_count = 1U;
    size_t block_owner_bytes;
    size_t object_reference_bytes;
    bool decoded = false;
    int report_result = BORINGFSCK_EXIT_HOST_FAILURE;
    int fd = -1;
    void *mapping = MAP_FAILED;

    if (path == NULL) {
        (void)fprintf(stderr, "boringfsck: missing image path\n");
        return BORINGFSCK_EXIT_HOST_FAILURE;
    }

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        (void)fprintf(stderr, "boringfsck: cannot open '%s': %s\n",
                      path, strerror(errno));
        return BORINGFSCK_EXIT_HOST_FAILURE;
    }
    if (fstat(fd, &file_stat) != 0) {
        (void)fprintf(stderr, "boringfsck: cannot stat '%s': %s\n",
                      path, strerror(errno));
        goto cleanup;
    }
    if (!S_ISREG(file_stat.st_mode)) {
        (void)fprintf(stderr, "boringfsck: '%s' is not a regular file\n", path);
        goto cleanup;
    }
    if (file_stat.st_size <= (off_t)0) {
        (void)fprintf(stderr, "boringfsck: '%s' is empty\n", path);
        goto cleanup;
    }

    file_size_u64 = (uint64_t)file_stat.st_size;
    if ((off_t)file_size_u64 != file_stat.st_size) {
        (void)fprintf(stderr, "boringfsck: image size is not representable\n");
        goto cleanup;
    }
    max_volume_bytes = (uint64_t)BORINGFS_MAX_BLOCKS *
                       (uint64_t)BORINGFS_BLOCK_SIZE;
    if ((file_size_u64 > max_volume_bytes) ||
        (file_size_u64 > (uint64_t)SIZE_MAX)) {
        (void)fprintf(stderr,
                      "boringfsck: image exceeds BoringFS v0 host bounds\n");
        goto cleanup;
    }
    volume_size = (size_t)file_size_u64;

    mapping = mmap(NULL, volume_size, PROT_READ, MAP_PRIVATE, fd, (off_t)0);
    if (mapping == MAP_FAILED) {
        (void)fprintf(stderr, "boringfsck: cannot map '%s': %s\n",
                      path, strerror(errno));
        goto cleanup;
    }

    decoded = boringfs_decode_superblock((const uint8_t *)mapping,
                                         volume_size, &superblock);
    if (decoded_block_count_bounded(&superblock)) {
        block_owner_count = (size_t)superblock.total_blocks;
    }
    if (decoded_object_count_bounded(&superblock)) {
        object_reference_count = (size_t)superblock.object_count;
    }
    if (!checked_mul_size(block_owner_count, sizeof(uint32_t),
                          &block_owner_bytes) ||
        !checked_mul_size(object_reference_count, sizeof(uint8_t),
                          &object_reference_bytes)) {
        (void)fprintf(stderr, "boringfsck: validator workspace overflow\n");
        goto cleanup;
    }

    workspace.block_owner = (uint32_t *)malloc(block_owner_bytes);
    workspace.object_reference_count =
        (uint8_t *)malloc(object_reference_bytes);
    if ((workspace.block_owner == NULL) ||
        (workspace.object_reference_count == NULL)) {
        (void)fprintf(stderr, "boringfsck: cannot allocate validator workspace\n");
        goto cleanup;
    }
    workspace.block_owner_count = block_owner_count;
    workspace.object_reference_count_count = object_reference_count;

    result = boringfs_validate_volume((const uint8_t *)mapping, volume_size,
                                      &workspace, &error);
    if (validator_result_is_host_failure(result)) {
        (void)fprintf(stderr, "boringfsck: validator failed internally: %s\n",
                      boringfs_validation_result_name(result));
        goto cleanup;
    }

    if (result == BORINGFS_VALIDATE_OK) {
        if (!decoded) {
            (void)fprintf(stderr,
                          "boringfsck: valid volume could not be decoded\n");
            goto cleanup;
        }
        if (cat_path != NULL) {
            report_result = cat_valid_path((const uint8_t *)mapping,
                                           volume_size, &superblock,
                                           cat_path) ?
                BORINGFSCK_EXIT_VALID : BORINGFSCK_EXIT_CORRUPT;
        } else {
            print_valid(path, &superblock);
            report_result = BORINGFSCK_EXIT_VALID;
        }
    } else {
        print_corrupt(path, decoded, &superblock, result, &error);
        report_result = BORINGFSCK_EXIT_CORRUPT;
    }

    free(workspace.block_owner);
    workspace.block_owner = NULL;
    free(workspace.object_reference_count);
    workspace.object_reference_count = NULL;
    if (munmap(mapping, volume_size) != 0) {
        mapping = MAP_FAILED;
        (void)fprintf(stderr, "boringfsck: cannot unmap '%s': %s\n",
                      path, strerror(errno));
        goto cleanup;
    }
    mapping = MAP_FAILED;
    if (close(fd) != 0) {
        fd = -1;
        (void)fprintf(stderr, "boringfsck: cannot close '%s': %s\n",
                      path, strerror(errno));
        return BORINGFSCK_EXIT_HOST_FAILURE;
    }
    fd = -1;

    return report_result;

cleanup:
    free(workspace.block_owner);
    free(workspace.object_reference_count);
    if (mapping != MAP_FAILED) {
        (void)munmap(mapping, volume_size);
    }
    if (fd >= 0) {
        (void)close(fd);
    }
    return BORINGFSCK_EXIT_HOST_FAILURE;
}

int main(int argc, char **argv) {
    if ((argc == 2) && (strcmp(argv[1], "--help") == 0)) {
        print_usage(stdout, argv[0]);
        return BORINGFSCK_EXIT_VALID;
    }
    if (argc == 2) {
        return inspect_image(argv[1], NULL);
    }
    if ((argc == 4) && (strcmp(argv[1], "--cat") == 0)) {
        return inspect_image(argv[3], argv[2]);
    }
    {
        print_usage(stderr, argv[0]);
        return BORINGFSCK_EXIT_HOST_FAILURE;
    }
}
