#include <stdbool.h>
#include <stdint.h>
int boringfs_fixture_historical_main(int argc, char **argv);
#define main boringfs_fixture_historical_main
#include "boringfs-fixture.c"
#undef main

#define M35_BLOCKS 96U
#define M35_PROGRAMS 5U

static bool used(const uint8_t *volume, uint32_t block) {
    return (volume[BORINGFS_BLOCK_SIZE + block / 8U] & (uint8_t)(1U << (block % 8U))) != 0U;
}
static uint32_t free_blocks(const uint8_t *volume) {
    uint32_t result = 0U, block;
    for (block = 16U; block < M35_BLOCKS; ++block) { if (!used(volume, block)) { ++result; } }
    return result;
}

int main(int argc, char **argv) {
    static const char *const names[M35_PROGRAMS] = {"boring-display-wm", "boringwm", "wm-client-a", "wm-client-b", "wm-client-c"};
    uint8_t volume[M35_BLOCKS * BORINGFS_BLOCK_SIZE];
    uint32_t block_owner[M35_BLOCKS]; uint8_t references[FIXTURE_OBJECTS];
    struct boringfs_validation_workspace work = {block_owner, M35_BLOCKS, references, FIXTURE_OBJECTS};
    struct boringfs_validation_error error;
    struct boringfs_superblock super;
    struct boringfs_extent bin = {BIN_BLOCK, 1U};
    uint32_t before, required = 0U, index;
    FILE *input;
    if (argc != 8) { (void)fprintf(stderr, "usage: %s output m34-root display-wm wm client-a client-b client-c\n", argv[0]); return 2; }
    input = fopen(argv[2], "rb");
    if (input == NULL) { return 2; }
    if (fread(volume, 1U, sizeof(volume), input) != sizeof(volume) || fgetc(input) != EOF) { (void)fclose(input); return 2; }
    (void)fclose(input); fixture_blocks = M35_BLOCKS;
    if (boringfs_validate_volume(volume, sizeof(volume), &work, &error) != BORINGFS_VALIDATE_OK ||
        !boringfs_decode_superblock(volume, sizeof(volume), &super) || super.total_blocks != M35_BLOCKS) { return 2; }
    before = free_blocks(volume);
    for (index = 0U; index < M35_PROGRAMS; ++index) {
        uint8_t *bytes = NULL;
        size_t size = 0U, copied = 0U;
        uint32_t remaining, block, count;
        uint16_t extent_count = 0U;
        struct boringfs_extent extents[BORINGFS_MAX_EXTENTS] = {{0U, 0U}};
        if (!read_program(argv[index + 3U], &bytes, &size)) { return 2; }
        count = (uint32_t)((size + BORINGFS_BLOCK_SIZE - 1U) / BORINGFS_BLOCK_SIZE);
        required += count; remaining = count;
        for (block = 16U; block < M35_BLOCKS && remaining != 0U; ++block) {
            size_t chunk;
            if (used(volume, block)) { continue; }
            if (extent_count == 0U || extents[extent_count - 1U].start_block + extents[extent_count - 1U].block_count != block) {
                if (extent_count == BORINGFS_MAX_EXTENTS) { free(bytes); return 2; }
                extents[extent_count++].start_block = block;
            }
            ++extents[extent_count - 1U].block_count;
            bitmap_set(volume, block, true);
            chunk = size - copied < BORINGFS_BLOCK_SIZE ? size - copied : BORINGFS_BLOCK_SIZE;
            (void)memcpy(volume + (size_t)block * BORINGFS_BLOCK_SIZE, bytes + copied, chunk);
            copied += chunk; --remaining;
        }
        if (remaining != 0U || copied != size ||
            !make_object(volume, sizeof(volume), &super, 20U + index, 11U, BORINGFS_TYPE_REGULAR,
                         (uint64_t)size, extents, extent_count) ||
            !write_dirent(volume, sizeof(volume), BIN_BLOCK, 8ULL + index, 20U + index,
                          BORINGFS_TYPE_REGULAR, names[index])) { free(bytes); return 2; }
        (void)printf("M35 program %s: %zu bytes, %u blocks\n", names[index], size, count); free(bytes);
    }
    if (!make_object(volume, sizeof(volume), &super, 11U, 1U, BORINGFS_TYPE_DIRECTORY,
                     13ULL * BORINGFS_DIRECTORY_RECORD_SIZE, &bin, 1U) ||
        boringfs_validate_volume(volume, sizeof(volume), &work, &error) != BORINGFS_VALIDATE_OK ||
        required > before || before - required != free_blocks(volume)) { return 2; }
    (void)printf("M35 retained blocks: %u\nM34 free blocks: %u\nM35 required additional blocks: %u\nM35 remaining free blocks: %u\n",
                 M35_BLOCKS, before, required, free_blocks(volume));
    return write_image(argv[1], volume, sizeof(volume));
}
