#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int boringfs_fixture_historical_main(int argc, char **argv);
#define main boringfs_fixture_historical_main
#include "boringfs-fixture.c"
#undef main

#define M37_EXTRA_PROGRAMS 4U
#define M37_TERMINAL_OBJECT_ID 13U
#define M37_SHELL_OBJECT_ID 14U
#define M37_DISPLAY_OBJECT_ID 15U
#define M37_WM_OBJECT_ID 16U
#define M37_BLOCK_OWNER_CAPACITY 512U

struct m37_extra_program {
    const char *name;
    uint8_t *bytes;
    size_t size;
    uint32_t object_id;
    uint32_t blocks;
    uint32_t start_block;
};

static uint32_t blocks_for(size_t size) {
    if ((size == 0U) ||
        (size > (size_t)UINT32_MAX * (size_t)BORINGFS_BLOCK_SIZE)) {
        return 0U;
    }
    return (uint32_t)((size + (size_t)BORINGFS_BLOCK_SIZE - 1U) /
                      (size_t)BORINGFS_BLOCK_SIZE);
}

static bool build_bundle(
    uint32_t blocks,
    const uint8_t *boringfetch, size_t boringfetch_size,
    struct m37_extra_program extras[M37_EXTRA_PROGRAMS],
    uint8_t **volume_out, size_t *volume_size_out) {
    uint32_t block_owner[M37_BLOCK_OWNER_CAPACITY];
    uint8_t references[FIXTURE_OBJECTS];
    struct boringfs_validation_workspace workspace;
    struct boringfs_validation_error error;
    struct boringfs_superblock super;
    struct boringfs_extent bin = {BIN_BLOCK, 1U};
    uint8_t *volume;
    size_t volume_size;
    uint32_t cursor;
    size_t index;

    if ((volume_out == NULL) || (volume_size_out == NULL) ||
        (blocks > M37_BLOCK_OWNER_CAPACITY)) {
        return false;
    }
    *volume_out = NULL;
    *volume_size_out = 0U;
    fixture_blocks = blocks;
    volume_size = (size_t)blocks * (size_t)BORINGFS_BLOCK_SIZE;
    volume = (uint8_t *)malloc(volume_size);
    if ((volume == NULL) ||
        !build_valid(volume, volume_size, boringfetch, boringfetch_size,
                     NULL, 0U, NULL, 0U, NULL, 0U, NULL, 0U) ||
        !boringfs_decode_superblock(volume, volume_size, &super)) {
        free(volume);
        return false;
    }

    cursor = BORINGFETCH_BLOCK0 + blocks_for(boringfetch_size);
    for (index = 0U; index < M37_EXTRA_PROGRAMS; ++index) {
        uint32_t block;
        struct boringfs_extent extent;
        size_t offset;

        extras[index].blocks = blocks_for(extras[index].size);
        extras[index].start_block = cursor;
        offset = (size_t)cursor * (size_t)BORINGFS_BLOCK_SIZE;
        if ((extras[index].blocks == 0U) || (cursor > blocks) ||
            (extras[index].blocks > blocks - cursor) ||
            (offset > volume_size) ||
            (extras[index].size > volume_size - offset)) {
            free(volume);
            return false;
        }
        extent.start_block = cursor;
        extent.block_count = extras[index].blocks;
        for (block = 0U; block < extras[index].blocks; ++block) {
            bitmap_set(volume, cursor + block, true);
        }
        (void)memcpy(volume + offset, extras[index].bytes,
                     extras[index].size);
        if (!make_object(volume, volume_size, &super,
                         extras[index].object_id, 11U,
                         BORINGFS_TYPE_REGULAR,
                         (uint64_t)extras[index].size, &extent, 1U) ||
            !write_dirent(volume, volume_size, BIN_BLOCK,
                          1ULL + (uint64_t)index,
                          extras[index].object_id,
                          BORINGFS_TYPE_REGULAR, extras[index].name)) {
            free(volume);
            return false;
        }
        if (cursor > UINT32_MAX - extras[index].blocks) {
            free(volume);
            return false;
        }
        cursor += extras[index].blocks;
    }
    if (!make_object(volume, volume_size, &super, 11U, 1U,
                     BORINGFS_TYPE_DIRECTORY,
                     (1ULL + (uint64_t)M37_EXTRA_PROGRAMS) *
                         (uint64_t)BORINGFS_DIRECTORY_RECORD_SIZE,
                     &bin, 1U)) {
        free(volume);
        return false;
    }

    workspace.block_owner = block_owner;
    workspace.block_owner_count = blocks;
    workspace.object_reference_count = references;
    workspace.object_reference_count_count = FIXTURE_OBJECTS;
    if (boringfs_validate_volume(volume, volume_size, &workspace, &error) !=
        BORINGFS_VALIDATE_OK) {
        free(volume);
        return false;
    }
    *volume_out = volume;
    *volume_size_out = volume_size;
    return true;
}

int main(int argc, char **argv) {
    uint8_t *boringfetch = NULL;
    size_t boringfetch_size = 0U;
    struct m37_extra_program extras[M37_EXTRA_PROGRAMS] = {
        {"boring-terminal", NULL, 0U, M37_TERMINAL_OBJECT_ID, 0U, 0U},
        {"boring-shell", NULL, 0U, M37_SHELL_OBJECT_ID, 0U, 0U},
        {"boring-display", NULL, 0U, M37_DISPLAY_OBJECT_ID, 0U, 0U},
        {"boringwm", NULL, 0U, M37_WM_OBJECT_ID, 0U, 0U}
    };
    uint8_t *volume = NULL;
    size_t volume_size = 0U;
    uint8_t *lower = NULL;
    size_t lower_size = 0U;
    uint32_t minimum;
    uint32_t boringfetch_blocks;
    uint32_t cursor;
    size_t index;
    int status = 2;

    if (argc != 7) {
        (void)fprintf(stderr,
            "usage: %s output boringfetch boring-terminal boring-shell boring-display boringwm\n",
            argv[0]);
        return 2;
    }
    if (!read_program(argv[2], &boringfetch, &boringfetch_size)) {
        return 2;
    }
    for (index = 0U; index < M37_EXTRA_PROGRAMS; ++index) {
        if (!read_program(argv[index + 3U], &extras[index].bytes,
                          &extras[index].size)) {
            goto cleanup;
        }
    }
    boringfetch_blocks = blocks_for(boringfetch_size);
    if ((boringfetch_blocks == 0U) ||
        (BORINGFETCH_BLOCK0 > UINT32_MAX - boringfetch_blocks)) {
        goto cleanup;
    }
    cursor = BORINGFETCH_BLOCK0 + boringfetch_blocks;
    for (index = 0U; index < M37_EXTRA_PROGRAMS; ++index) {
        extras[index].blocks = blocks_for(extras[index].size);
        if ((extras[index].blocks == 0U) ||
            (cursor > UINT32_MAX - extras[index].blocks)) {
            goto cleanup;
        }
        cursor += extras[index].blocks;
    }
    minimum = cursor;
    if ((minimum > M37_BLOCK_OWNER_CAPACITY) ||
        !build_bundle(minimum, boringfetch, boringfetch_size, extras,
                      &volume, &volume_size)) {
        goto cleanup;
    }
    if ((minimum <= BORINGFETCH_BLOCK0) ||
        build_bundle(minimum - 1U, boringfetch, boringfetch_size, extras,
                     &lower, &lower_size)) {
        free(lower);
        goto cleanup;
    }

    (void)printf("M37 BoringFS measured geometry: %u blocks\n", minimum);
    (void)printf("M37 lower bound: %u blocks rejected\n", minimum - 1U);
    (void)printf("M37 boringfetch: %zu bytes, %u blocks\n",
                 boringfetch_size, boringfetch_blocks);
    for (index = 0U; index < M37_EXTRA_PROGRAMS; ++index) {
        (void)printf("M37 %s: %zu bytes, %u blocks\n",
                     extras[index].name, extras[index].size,
                     extras[index].blocks);
    }
    status = write_image(argv[1], volume, volume_size);

cleanup:
    free(volume);
    for (index = M37_EXTRA_PROGRAMS; index > 0U; --index) {
        free(extras[index - 1U].bytes);
    }
    free(boringfetch);
    return status;
}
