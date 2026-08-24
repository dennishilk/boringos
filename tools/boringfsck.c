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
    (void)fprintf(stream, "Usage: %s <image>\n", program);
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
    (void)printf("BoringFS v%" PRIu16 ".%" PRIu16 "\n",
                 superblock->format_major, superblock->format_minor);
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
        (void)printf("BoringFS v%" PRIu16 ".%" PRIu16 "\n",
                     superblock->format_major, superblock->format_minor);
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

static int inspect_image(const char *path) {
    struct stat file_stat;
    struct boringfs_superblock superblock = { 0 };
    struct boringfs_validation_workspace workspace = { 0 };
    struct boringfs_validation_error error = { 0 };
    enum boringfs_validation_result result;
    uint64_t file_size_u64;
    uint64_t max_volume_bytes;
    size_t volume_size;
    size_t block_owner_count = 1U;
    size_t object_reference_count = 1U;
    size_t block_owner_bytes;
    bool decoded = false;
    int fd = -1;
    void *mapping = MAP_FAILED;
    int status = BORINGFSCK_EXIT_HOST_FAILURE;

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
                          &block_owner_bytes)) {
        (void)fprintf(stderr, "boringfsck: validator workspace overflow\n");
        goto cleanup;
    }

    workspace.block_owner = (uint32_t *)malloc(block_owner_bytes);
    workspace.object_reference_count =
        (uint8_t *)malloc(object_reference_count * sizeof(uint8_t));
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

    if (result == BORINGFS_VALIDATE_OK) {
        if (!decoded) {
            (void)fprintf(stderr,
                          "boringfsck: valid volume could not be decoded\n");
            return BORINGFSCK_EXIT_HOST_FAILURE;
        }
        print_valid(path, &superblock);
        return BORINGFSCK_EXIT_VALID;
    }

    print_corrupt(path, decoded, &superblock, result, &error);
    return BORINGFSCK_EXIT_CORRUPT;

cleanup:
    free(workspace.block_owner);
    free(workspace.object_reference_count);
    if (mapping != MAP_FAILED) {
        (void)munmap(mapping, volume_size);
    }
    if (fd >= 0) {
        (void)close(fd);
    }
    return status;
}

int main(int argc, char **argv) {
    if ((argc == 2) && (strcmp(argv[1], "--help") == 0)) {
        print_usage(stdout, argv[0]);
        return BORINGFSCK_EXIT_VALID;
    }
    if (argc != 2) {
        print_usage(stderr, argv[0]);
        return BORINGFSCK_EXIT_HOST_FAILURE;
    }
    return inspect_image(argv[1]);
}
