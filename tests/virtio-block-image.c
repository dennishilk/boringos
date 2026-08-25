#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMAGE_SECTORS 4096ULL
#define SECTOR_SIZE 512U
#define IMAGE_SIZE (IMAGE_SECTORS * (uint64_t)SECTOR_SIZE)
#define SINGLE_LBA 32ULL
#define MULTI_FIRST_LBA 40ULL
#define MULTI_COUNT 4U
#define CHUNK_FIRST_LBA 64ULL
#define CHUNK_COUNT 12U
#define LEFT_LBA (MULTI_FIRST_LBA - 1ULL)
#define RIGHT_LBA (MULTI_FIRST_LBA + (uint64_t)MULTI_COUNT)

static uint8_t initial_byte(uint64_t lba, size_t offset) {
    const uint64_t value = ((lba & 0xffULL) * 17ULL) +
                           (((uint64_t)offset & 0xffULL) * 31ULL) +
                           0x5aULL;
    return (uint8_t)(value & 0xffULL);
}

static uint8_t single_byte(size_t offset) {
    const size_t value = 0xa5U + ((offset & 0xffU) * 13U);
    return (uint8_t)(value & 0xffU);
}

static uint8_t multi_byte(uint32_t relative_sector, size_t offset) {
    const size_t value = 0xc3U + ((size_t)relative_sector * 29U) +
                         ((offset & 0xffU) * 7U);
    return (uint8_t)(value & 0xffU);
}

static void fill_initial(uint8_t *sector, uint64_t lba) {
    size_t index;

    for (index = 0U; index < (size_t)SECTOR_SIZE; ++index) {
        sector[index] = initial_byte(lba, index);
    }
}

static int seek_sector(FILE *file, uint64_t lba) {
    const uint64_t offset = lba * (uint64_t)SECTOR_SIZE;

    if ((file == NULL) || (lba >= IMAGE_SECTORS) ||
        (offset > (uint64_t)LONG_MAX)) {
        return -1;
    }
    return fseek(file, (long)offset, SEEK_SET);
}

static int create_image(const char *path) {
    FILE *file;
    uint8_t sector[SECTOR_SIZE];
    uint64_t lba;

    file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "cannot create raw image: %s\n", strerror(errno));
        return 1;
    }

    for (lba = 0ULL; lba < IMAGE_SECTORS; ++lba) {
        fill_initial(sector, lba);
        if (fwrite(sector, 1U, sizeof(sector), file) != sizeof(sector)) {
            fprintf(stderr, "cannot write raw image\n");
            (void)fclose(file);
            return 1;
        }
    }

    if (fflush(file) != 0 || fclose(file) != 0) {
        fprintf(stderr, "cannot finalize raw image\n");
        return 1;
    }
    return 0;
}

static int read_sector(FILE *file, uint64_t lba, uint8_t *sector) {
    if ((file == NULL) || (sector == NULL) || seek_sector(file, lba) != 0) {
        return -1;
    }
    return (fread(sector, 1U, (size_t)SECTOR_SIZE, file) ==
            (size_t)SECTOR_SIZE) ? 0 : -1;
}

static int verify_single(FILE *file) {
    uint8_t sector[SECTOR_SIZE];
    size_t index;

    if (read_sector(file, SINGLE_LBA, sector) != 0) {
        return -1;
    }
    for (index = 0U; index < (size_t)SECTOR_SIZE; ++index) {
        if (sector[index] != single_byte(index)) {
            return -1;
        }
    }
    return 0;
}

static int verify_multi(FILE *file) {
    uint8_t sector[SECTOR_SIZE];
    uint32_t relative;

    for (relative = 0U; relative < MULTI_COUNT; ++relative) {
        size_t index;
        const uint64_t lba = MULTI_FIRST_LBA + (uint64_t)relative;

        if (read_sector(file, lba, sector) != 0) {
            return -1;
        }
        for (index = 0U; index < (size_t)SECTOR_SIZE; ++index) {
            if (sector[index] != multi_byte(relative, index)) {
                return -1;
            }
        }
    }
    return 0;
}

static int verify_chunk(FILE *file) {
    uint8_t sector[SECTOR_SIZE];
    uint32_t relative;

    for (relative = 0U; relative < CHUNK_COUNT; ++relative) {
        size_t index;
        const uint64_t lba = CHUNK_FIRST_LBA + (uint64_t)relative;

        if (read_sector(file, lba, sector) != 0) {
            return -1;
        }
        for (index = 0U; index < (size_t)SECTOR_SIZE; ++index) {
            const uint8_t expected = (uint8_t)((0x6dU + (relative * 19U) +
                ((uint32_t)index * 11U)) & 0xffU);
            if (sector[index] != expected) {
                return -1;
            }
        }
    }
    return 0;
}

static int verify_initial_sector(FILE *file, uint64_t lba) {
    uint8_t sector[SECTOR_SIZE];
    size_t index;

    if (read_sector(file, lba, sector) != 0) {
        return -1;
    }
    for (index = 0U; index < (size_t)SECTOR_SIZE; ++index) {
        if (sector[index] != initial_byte(lba, index)) {
            return -1;
        }
    }
    return 0;
}

static int verify_image(const char *path) {
    FILE *file;
    long size;
    int persisted_ok;
    int left_ok;
    int right_ok;

    file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "cannot open raw image: %s\n", strerror(errno));
        return 1;
    }
    if (fseek(file, 0L, SEEK_END) != 0) {
        (void)fclose(file);
        return 1;
    }
    size = ftell(file);
    if ((size < 0L) || ((uint64_t)size != IMAGE_SIZE)) {
        fprintf(stderr, "raw image size mismatch\n");
        (void)fclose(file);
        return 1;
    }

    persisted_ok = (verify_single(file) == 0) &&
                   (verify_multi(file) == 0) &&
                   (verify_chunk(file) == 0);
    left_ok = verify_initial_sector(file, LEFT_LBA) == 0;
    right_ok = verify_initial_sector(file, RIGHT_LBA) == 0;

    if (fclose(file) != 0) {
        return 1;
    }

    printf("Host raw-disk verification:\n");
    printf("  persisted-write: %s\n", persisted_ok ? "PASS" : "FAIL");
    printf("  left-neighbor: %s\n", left_ok ? "PASS" : "FAIL");
    printf("  right-neighbor: %s\n", right_ok ? "PASS" : "FAIL");

    return (persisted_ok && left_ok && right_ok) ? 0 : 1;
}

int main(int argc, char **argv) {
    if ((argc != 3) || (argv == NULL)) {
        fprintf(stderr, "usage: virtio-block-image <create|verify> <image>\n");
        return 2;
    }
    if (strcmp(argv[1], "create") == 0) {
        return create_image(argv[2]);
    }
    if (strcmp(argv[1], "verify") == 0) {
        return verify_image(argv[2]);
    }

    fprintf(stderr, "unknown mode: %s\n", argv[1]);
    return 2;
}
