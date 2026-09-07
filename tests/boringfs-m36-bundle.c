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

#define M36_EXTRA_PROGRAMS 2U
#define M36_TERMINAL_OBJECT_ID 13U
#define M36_SHELL_OBJECT_ID 14U

struct m36_extra_program {
    const char *name;
    uint8_t *bytes;
    size_t size;
    uint32_t object_id;
    uint32_t blocks;
    uint32_t start_block;
};

static uint32_t blocks_for(size_t size) {
    if ((size == 0U) || (size > (size_t)UINT32_MAX * BORINGFS_BLOCK_SIZE)) {
        return 0U;
    }
    return (uint32_t)((size + BORINGFS_BLOCK_SIZE - 1U) / BORINGFS_BLOCK_SIZE);
}

static bool build_bundle(uint32_t blocks,
                         const uint8_t *boringfetch, size_t boringfetch_size,
                         struct m36_extra_program extras[M36_EXTRA_PROGRAMS],
                         uint8_t **volume_out, size_t *volume_size_out) {
    uint32_t block_owner[BORINGFS_MAX_EXTENTS * PROGRAM_SLOT_BLOCKS + 128U];
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
        (blocks > (uint32_t)(sizeof(block_owner) / sizeof(block_owner[0])))) {
        return false;
    }
    *volume_out = NULL;
    *volume_size_out = 0U;
    fixture_blocks = blocks;
    volume_size = (size_t)blocks * BORINGFS_BLOCK_SIZE;
    volume = (uint8_t *)malloc(volume_size);
    if ((volume == NULL) ||
        !build_valid(volume, volume_size, boringfetch, boringfetch_size,
                     NULL, 0U, NULL, 0U, NULL, 0U, NULL, 0U) ||
        !boringfs_decode_superblock(volume, volume_size, &super)) {
        free(volume);
        return false;
    }

    cursor = BORINGFETCH_BLOCK0 + blocks_for(boringfetch_size);
    for (index = 0U; index < M36_EXTRA_PROGRAMS; ++index) {
        uint32_t block;
        struct boringfs_extent extent;
        const size_t offset = (size_t)cursor * BORINGFS_BLOCK_SIZE;

        extras[index].blocks = blocks_for(extras[index].size);
        extras[index].start_block = cursor;
        if ((extras[index].blocks == 0U) || (cursor > blocks) ||
            (extras[index].blocks > blocks - cursor) ||
            (offset > volume_size) || (extras[index].size > volume_size - offset)) {
            free(volume);
            return false;
        }
        extent.start_block = cursor;
        extent.block_count = extras[index].blocks;
        for (block = 0U; block < extras[index].blocks; ++block) {
            bitmap_set(volume, cursor + block, true);
        }
        (void)memcpy(volume + offset, extras[index].bytes, extras[index].size);
        if (!make_object(volume, volume_size, &super, extras[index].object_id,
                         11U, BORINGFS_TYPE_REGULAR, (uint64_t)extras[index].size,
                         &extent, 1U) ||
            !write_dirent(volume, volume_size, BIN_BLOCK, 1ULL + (uint64_t)index,
                          extras[index].object_id, BORINGFS_TYPE_REGULAR,
                          extras[index].name)) {
            free(volume);
            return false;
        }
        cursor += extras[index].blocks;
    }
    if (!make_object(volume, volume_size, &super, 11U, 1U,
                     BORINGFS_TYPE_DIRECTORY,
                     3ULL * (uint64_t)BORINGFS_DIRECTORY_RECORD_SIZE,
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
    struct m36_extra_program extras[M36_EXTRA_PROGRAMS] = {
        {"boring-terminal", NULL, 0U, M36_TERMINAL_OBJECT_ID, 0U, 0U},
        {"boring-shell", NULL, 0U, M36_SHELL_OBJECT_ID, 0U, 0U}
    };
    uint8_t *volume = NULL;
    size_t volume_size = 0U;
    uint8_t *lower = NULL;
    size_t lower_size = 0U;
    uint32_t minimum;
    uint32_t boringfetch_blocks;
    size_t index;
    int status = 2;

    if (argc != 5) {
        (void)fprintf(stderr,
                      "usage: %s output boringfetch boring-terminal boring-shell\n",
                      argv[0]);
        return 2;
    }
    if (!read_program(argv[2], &boringfetch, &boringfetch_size)) {
        return 2;
    }
    for (index = 0U; index < M36_EXTRA_PROGRAMS; ++index) {
        if (!read_program(argv[index + 3U], &extras[index].bytes,
                          &extras[index].size)) {
            goto cleanup;
        }
    }
    boringfetch_blocks = blocks_for(boringfetch_size);
    extras[0].blocks = blocks_for(extras[0].size);
    extras[1].blocks = blocks_for(extras[1].size);
    if ((boringfetch_blocks == 0U) || (extras[0].blocks == 0U) ||
        (extras[1].blocks == 0U) ||
        (BORINGFETCH_BLOCK0 > UINT32_MAX - boringfetch_blocks) ||
        (BORINGFETCH_BLOCK0 + boringfetch_blocks > UINT32_MAX - extras[0].blocks) ||
        (BORINGFETCH_BLOCK0 + boringfetch_blocks + extras[0].blocks > UINT32_MAX - extras[1].blocks)) {
        goto cleanup;
    }
    minimum = BORINGFETCH_BLOCK0 + boringfetch_blocks +
              extras[0].blocks + extras[1].blocks;
    if (!build_bundle(minimum, boringfetch, boringfetch_size, extras,
                      &volume, &volume_size)) {
        goto cleanup;
    }
    if ((minimum <= BORINGFETCH_BLOCK0) ||
        build_bundle(minimum - 1U, boringfetch, boringfetch_size, extras,
                     &lower, &lower_size)) {
        free(lower);
        goto cleanup;
    }

    (void)printf("M36 BoringFS measured geometry: %u blocks\n", minimum);
    (void)printf("M36 lower bound: %u blocks rejected\n", minimum - 1U);
    (void)printf("M36 boringfetch: %zu bytes, %u blocks\n",
                 boringfetch_size, boringfetch_blocks);
    (void)printf("M36 boring-terminal: %zu bytes, %u blocks\n",
                 extras[0].size, extras[0].blocks);
    (void)printf("M36 boring-shell: %zu bytes, %u blocks\n",
                 extras[1].size, extras[1].blocks);
    status = write_image(argv[1], volume, volume_size);

cleanup:
    free(volume);
    free(extras[1].bytes);
    free(extras[0].bytes);
    free(boringfetch);
    return status;
}
