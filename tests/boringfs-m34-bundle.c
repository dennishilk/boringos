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

#define M34_FIXTURE_BLOCKS 112U
#define M34_PROGRAM_COUNT 3U
#define M34_FIRST_OBJECT_ID 17U
#define M34_BIN_FIRST_INDEX 5ULL

struct m34_program {
    const char *name;
    const char *path;
    uint8_t *bytes;
    size_t size;
    uint32_t object_id;
    uint32_t block_count;
    uint16_t extent_count;
    struct boringfs_extent extents[BORINGFS_MAX_EXTENTS];
};

static bool bitmap_allocated(const uint8_t *volume, uint32_t block) {
    const size_t offset = (size_t)BORINGFS_BLOCK_SIZE +
                          ((size_t)block / 8U);
    const uint8_t mask = (uint8_t)(1U << (block % 8U));

    return (volume[offset] & mask) != 0U;
}

static bool program_block_count(size_t size, uint32_t *blocks_out) {
    size_t blocks;

    if ((blocks_out == NULL) || (size == 0U) ||
        (size > (size_t)PROGRAM_SLOT_BLOCKS * (size_t)BORINGFS_BLOCK_SIZE)) {
        return false;
    }
    blocks = (size + (size_t)BORINGFS_BLOCK_SIZE - 1U) /
             (size_t)BORINGFS_BLOCK_SIZE;
    if ((blocks == 0U) || (blocks > (size_t)PROGRAM_SLOT_BLOCKS) ||
        (blocks > (size_t)UINT32_MAX)) {
        return false;
    }
    *blocks_out = (uint32_t)blocks;
    return true;
}

static uint32_t count_free_blocks(const uint8_t *volume, uint32_t limit) {
    uint32_t block;
    uint32_t count = 0U;

    if (limit > FIXTURE_BLOCKS) {
        limit = FIXTURE_BLOCKS;
    }
    for (block = BORINGFETCH_BLOCK0; block < limit; ++block) {
        if (!bitmap_allocated(volume, block)) {
            ++count;
        }
    }
    return count;
}

static bool allocate_program(uint8_t *volume, struct m34_program *program) {
    uint32_t remaining;
    uint32_t block = BORINGFETCH_BLOCK0;

    if ((volume == NULL) || (program == NULL) ||
        !program_block_count(program->size, &program->block_count)) {
        return false;
    }
    program->extent_count = 0U;
    remaining = program->block_count;

    while ((block < FIXTURE_BLOCKS) && (remaining != 0U)) {
        uint32_t start;
        uint32_t run = 0U;
        uint32_t take;
        uint32_t mark;

        while ((block < FIXTURE_BLOCKS) && bitmap_allocated(volume, block)) {
            ++block;
        }
        if (block >= FIXTURE_BLOCKS) {
            break;
        }
        start = block;
        while ((block < FIXTURE_BLOCKS) && !bitmap_allocated(volume, block)) {
            ++run;
            ++block;
        }
        take = (run < remaining) ? run : remaining;
        if ((take == 0U) ||
            (program->extent_count >= (uint16_t)BORINGFS_MAX_EXTENTS)) {
            return false;
        }
        program->extents[program->extent_count].start_block = start;
        program->extents[program->extent_count].block_count = take;
        ++program->extent_count;
        for (mark = 0U; mark < take; ++mark) {
            bitmap_set(volume, start + mark, true);
        }
        remaining -= take;
        if (take < run) {
            block = start + take;
        }
    }
    return remaining == 0U;
}

static bool copy_program_to_extents(uint8_t *volume,
                                    size_t volume_size,
                                    const struct m34_program *program) {
    size_t copied = 0U;
    size_t extent_index;

    if ((volume == NULL) || (program == NULL) || (program->bytes == NULL)) {
        return false;
    }
    for (extent_index = 0U;
         (extent_index < (size_t)program->extent_count) &&
         (copied < program->size);
         ++extent_index) {
        const struct boringfs_extent *extent = &program->extents[extent_index];
        const size_t capacity = (size_t)extent->block_count *
                                (size_t)BORINGFS_BLOCK_SIZE;
        const size_t remaining = program->size - copied;
        const size_t chunk = (capacity < remaining) ? capacity : remaining;
        const size_t offset = (size_t)extent->start_block *
                              (size_t)BORINGFS_BLOCK_SIZE;

        if ((offset > volume_size) || (chunk > volume_size - offset)) {
            return false;
        }
        (void)memcpy(&volume[offset], &program->bytes[copied], chunk);
        copied += chunk;
    }
    return copied == program->size;
}

static uint32_t highest_allocated_block(const uint8_t *volume) {
    uint32_t block = FIXTURE_BLOCKS;

    while (block != 0U) {
        --block;
        if (bitmap_allocated(volume, block)) {
            return block;
        }
    }
    return 0U;
}

static enum boringfs_validation_result validate_m34(
    const uint8_t *volume,
    size_t volume_size) {
    uint32_t block_owner[M34_FIXTURE_BLOCKS];
    uint8_t reference_count[FIXTURE_OBJECTS];
    const struct boringfs_validation_workspace workspace = {
        .block_owner = block_owner,
        .block_owner_count = M34_FIXTURE_BLOCKS,
        .object_reference_count = reference_count,
        .object_reference_count_count = FIXTURE_OBJECTS
    };
    struct boringfs_validation_error error;

    return boringfs_validate_volume(volume, volume_size, &workspace, &error);
}

static void free_programs(struct m34_program programs[M34_PROGRAM_COUNT]) {
    size_t index;

    for (index = 0U; index < (size_t)M34_PROGRAM_COUNT; ++index) {
        free(programs[index].bytes);
        programs[index].bytes = NULL;
        programs[index].size = 0U;
    }
}

int main(int argc, char **argv) {
    const size_t volume_size = (size_t)M34_FIXTURE_BLOCKS *
                               (size_t)BORINGFS_BLOCK_SIZE;
    uint8_t *base_programs[5] = { NULL, NULL, NULL, NULL, NULL };
    size_t base_sizes[5] = { 0U, 0U, 0U, 0U, 0U };
    struct m34_program programs[M34_PROGRAM_COUNT] = {
        { "boring-display", NULL, NULL, 0U, M34_FIRST_OBJECT_ID,
          0U, 0U, { { 0U, 0U } } },
        { "display-client-a", NULL, NULL, 0U, M34_FIRST_OBJECT_ID + 1U,
          0U, 0U, { { 0U, 0U } } },
        { "display-client-b", NULL, NULL, 0U, M34_FIRST_OBJECT_ID + 2U,
          0U, 0U, { { 0U, 0U } } }
    };
    struct boringfs_extent bin_extent = { BIN_BLOCK, 1U };
    uint8_t *volume = NULL;
    uint32_t free_below_m33;
    uint32_t required_display_blocks = 0U;
    uint32_t highest;
    size_t index;
    int status = 2;

    if (argc != 11) {
        (void)fprintf(stderr,
                      "usage: %s <output> valid <boringfetch> <cat> <input-test> <memory-test> <ipc-test> <boring-display> <display-client-a> <display-client-b>\n",
                      argv[0]);
        return 2;
    }
    if (strcmp(argv[2], "valid") != 0) {
        (void)fputs("M34 bundle builder supports only valid fixtures\n", stderr);
        return 2;
    }

    for (index = 0U; index < 5U; ++index) {
        if (!read_program(argv[3 + index], &base_programs[index],
                          &base_sizes[index])) {
            (void)fprintf(stderr, "cannot read base bundle ELF: %s\n",
                          argv[3 + index]);
            goto cleanup;
        }
    }
    for (index = 0U; index < (size_t)M34_PROGRAM_COUNT; ++index) {
        uint32_t blocks;

        programs[index].path = argv[8 + index];
        if (!read_program(programs[index].path, &programs[index].bytes,
                          &programs[index].size) ||
            !program_block_count(programs[index].size, &blocks)) {
            (void)fprintf(stderr, "cannot read bounded M34 ELF: %s\n",
                          programs[index].path);
            goto cleanup;
        }
        if (required_display_blocks > UINT32_MAX - blocks) {
            goto cleanup;
        }
        required_display_blocks += blocks;
    }

    fixture_blocks = M34_FIXTURE_BLOCKS;
    volume = (uint8_t *)malloc(volume_size);
    if ((volume == NULL) ||
        !build_valid(volume, volume_size,
                     base_programs[0], base_sizes[0],
                     base_programs[1], base_sizes[1],
                     base_programs[2], base_sizes[2],
                     base_programs[3], base_sizes[3],
                     base_programs[4], base_sizes[4])) {
        (void)fputs("M34 base fixture construction failed\n", stderr);
        goto cleanup;
    }

    free_below_m33 = count_free_blocks(volume, M33_FIXTURE_BLOCKS);
    if (required_display_blocks <= free_below_m33) {
        (void)fprintf(stderr,
                      "M34 geometry proof failed: display trio needs %u blocks but %u free blocks already exist below M33 limit %u\n",
                      required_display_blocks, free_below_m33,
                      M33_FIXTURE_BLOCKS);
        goto cleanup;
    }

    {
        struct boringfs_superblock superblock;

        if (!boringfs_decode_superblock(volume, volume_size, &superblock)) {
            goto cleanup;
        }
        for (index = 0U; index < (size_t)M34_PROGRAM_COUNT; ++index) {
            if (!allocate_program(volume, &programs[index]) ||
                !make_object(volume, volume_size, &superblock,
                             programs[index].object_id, 11U,
                             BORINGFS_TYPE_REGULAR,
                             (uint64_t)programs[index].size,
                             programs[index].extents,
                             programs[index].extent_count) ||
                !write_dirent(volume, volume_size, BIN_BLOCK,
                              M34_BIN_FIRST_INDEX + (uint64_t)index,
                              programs[index].object_id,
                              BORINGFS_TYPE_REGULAR,
                              programs[index].name) ||
                !copy_program_to_extents(volume, volume_size,
                                         &programs[index])) {
                (void)fprintf(stderr, "M34 program packing failed: %s\n",
                              programs[index].name);
                goto cleanup;
            }
        }
        if (!make_object(volume, volume_size, &superblock, 11U, 1U,
                         BORINGFS_TYPE_DIRECTORY,
                         8ULL * (uint64_t)BORINGFS_DIRECTORY_RECORD_SIZE,
                         &bin_extent, 1U)) {
            goto cleanup;
        }
    }

    highest = highest_allocated_block(volume);
    if ((highest < M33_FIXTURE_BLOCKS) ||
        (highest >= M34_FIXTURE_BLOCKS) ||
        (validate_m34(volume, volume_size) != BORINGFS_VALIDATE_OK)) {
        (void)fprintf(stderr,
                      "M34 geometry/validation failed: highest=%u limit=%u\n",
                      highest, M34_FIXTURE_BLOCKS);
        goto cleanup;
    }

    status = write_image(argv[1], volume, volume_size);
    if (status == 0) {
        (void)printf("M34 BoringFS bundle: %s\n", argv[1]);
        (void)printf("M34 fixture blocks: %u\n", M34_FIXTURE_BLOCKS);
        (void)printf("M33 fixture blocks: %u\n", M33_FIXTURE_BLOCKS);
        (void)printf("M33 free blocks below limit: %u\n", free_below_m33);
        (void)printf("M34 display blocks required: %u\n",
                     required_display_blocks);
        (void)printf("M34 highest allocated block: %u\n", highest);
        for (index = 0U; index < (size_t)M34_PROGRAM_COUNT; ++index) {
            (void)printf("M34 program %s: %zu bytes, %u blocks, %u extents\n",
                         programs[index].name, programs[index].size,
                         programs[index].block_count,
                         (unsigned int)programs[index].extent_count);
        }
    }

cleanup:
    for (index = 0U; index < 5U; ++index) {
        free(base_programs[index]);
    }
    free_programs(programs);
    free(volume);
    return status;
}
