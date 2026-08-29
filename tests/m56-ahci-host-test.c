#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <boring/ahci.h>

static int failures;

static void check(bool condition, const char *name) {
    if (!condition) {
        (void)fprintf(stderr, "m56-ahci-host-test: FAIL: %s\n", name);
        ++failures;
    }
}

static void set_capacity48(uint16_t words[AHCI_IDENTIFY_WORDS],
                           uint64_t sectors) {
    words[100] = (uint16_t)(sectors & 0xffffULL);
    words[101] = (uint16_t)((sectors >> 16U) & 0xffffULL);
    words[102] = (uint16_t)((sectors >> 32U) & 0xffffULL);
    words[103] = (uint16_t)((sectors >> 48U) & 0xffffULL);
}

static void test_identify_512(void) {
    uint16_t words[AHCI_IDENTIFY_WORDS] = { 0U };
    struct ahci_identify_geometry geometry;

    words[49] = (uint16_t)(1U << 9U);
    words[83] = (uint16_t)(0x4000U | (1U << 10U));
    words[106] = 0x4000U;
    set_capacity48(words, 16384ULL);

    check(ahci_parse_identify(words, &geometry), "parse 512-byte identify");
    check(geometry.logical_blocks == 16384ULL, "48-bit capacity");
    check(geometry.logical_block_size == 512U, "default logical sector size");
    check(geometry.lba_supported, "LBA supported");
    check(geometry.lba48_supported, "LBA48 supported");
}

static void test_identify_4096(void) {
    uint16_t words[AHCI_IDENTIFY_WORDS] = { 0U };
    struct ahci_identify_geometry geometry;

    words[49] = (uint16_t)(1U << 9U);
    words[83] = (uint16_t)(0x4000U | (1U << 10U));
    words[106] = (uint16_t)(0x4000U | (1U << 12U));
    words[117] = 2048U;
    words[118] = 0U;
    set_capacity48(words, 4096ULL);

    check(ahci_parse_identify(words, &geometry), "parse extended logical sector");
    check(geometry.logical_block_size == 4096U, "4096-byte logical sector");
    check(geometry.logical_blocks == 4096ULL, "extended capacity preserved");
}

static void test_identify_lba28_fallback(void) {
    uint16_t words[AHCI_IDENTIFY_WORDS] = { 0U };
    struct ahci_identify_geometry geometry;

    words[49] = (uint16_t)(1U << 9U);
    words[60] = 0x1234U;
    words[61] = 0x0001U;

    check(ahci_parse_identify(words, &geometry), "parse LBA28 fallback");
    check(geometry.logical_blocks == 0x00011234ULL, "LBA28 capacity");
    check(!geometry.lba48_supported, "LBA28 not LBA48");
}

static void test_invalid_identify(void) {
    uint16_t words[AHCI_IDENTIFY_WORDS] = { 0U };
    struct ahci_identify_geometry geometry;

    check(!ahci_parse_identify(words, &geometry), "reject missing LBA capability");

    words[49] = (uint16_t)(1U << 9U);
    check(!ahci_parse_identify(words, &geometry), "reject zero capacity");

    words[83] = (uint16_t)(0x4000U | (1U << 10U));
    set_capacity48(words, 1ULL);
    words[106] = (uint16_t)(0x4000U | (1U << 12U));
    words[117] = 0U;
    words[118] = 0U;
    check(!ahci_parse_identify(words, &geometry), "reject zero extended sector words");
}

static void test_lba_ranges(void) {
    check(ahci_lba_range_valid(100ULL, 0ULL, 1U), "first LBA valid");
    check(ahci_lba_range_valid(100ULL, 99ULL, 1U), "last LBA valid");
    check(ahci_lba_range_valid(100ULL, 97ULL, 3U), "bounded multi-sector valid");
    check(!ahci_lba_range_valid(100ULL, 100ULL, 1U), "first invalid LBA rejected");
    check(!ahci_lba_range_valid(100ULL, 99ULL, 2U), "cross-end rejected");
    check(!ahci_lba_range_valid(100ULL, 0ULL, 0U), "zero block request rejected");
}

static void test_transfer_bytes(void) {
    uint32_t bytes = 0U;

    check(ahci_compute_transfer_bytes(512U, 8U, AHCI_M56_DMA_BYTES, &bytes),
          "full bounce transfer accepted");
    check(bytes == AHCI_M56_DMA_BYTES, "full bounce byte count");
    check(!ahci_compute_transfer_bytes(512U, 9U, AHCI_M56_DMA_BYTES, &bytes),
          "oversize bounce transfer rejected");
    check(ahci_compute_transfer_bytes(4096U, 1U, AHCI_M56_DMA_BYTES, &bytes),
          "single 4K sector accepted");
    check(!ahci_compute_transfer_bytes(4096U, 2U, AHCI_M56_DMA_BYTES, &bytes),
          "two 4K sectors rejected");
    check(!ahci_compute_transfer_bytes(UINT32_MAX, 2U, UINT32_MAX, &bytes),
          "multiplication overflow rejected");
}

int main(void) {
    test_identify_512();
    test_identify_4096();
    test_identify_lba28_fallback();
    test_invalid_identify();
    test_lba_ranges();
    test_transfer_bytes();

    if (failures != 0) {
        return 1;
    }
    (void)puts("m56-ahci-host-test: PASS");
    return 0;
}
