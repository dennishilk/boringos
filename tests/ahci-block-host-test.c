#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <boring/ahci_block.h>

static void words_zero(uint16_t *words) {
    size_t index;
    for (index = 0U; index < 256U; ++index) {
        words[index] = 0U;
    }
}

static bool expect(bool condition, const char *label) {
    if (!condition) {
        (void)fprintf(stderr, "ahci-block-host-test: FAIL: %s\n", label);
        return false;
    }
    return true;
}

int main(void) {
    uint16_t words[256];
    struct ahci_identify_info info;
    uint8_t fis[AHCI_BLOCK_FIS_BYTES];
    uint32_t bytes = 0U;

    words_zero(words);
    words[83] = (uint16_t)(1U << 10);
    words[100] = 0x9abcU;
    words[101] = 0x5678U;
    words[102] = 0x1234U;
    words[103] = 0x0000U;
    if (!expect(ahci_identify_decode(words, 256U, &info), "LBA48 decode") ||
        !expect(info.lba48, "LBA48 capability") ||
        !expect(info.block_count == 0x0000123456789abcULL, "LBA48 capacity") ||
        !expect(info.logical_block_size == 512U, "default logical sector") ||
        !expect(ahci_build_identify_fis(fis, sizeof(fis)), "IDENTIFY FIS") ||
        !expect((fis[0] == 0x27U) && (fis[1] == 0x80U) &&
                (fis[2] == 0xecU), "IDENTIFY FIS fields") ||
        !expect(ahci_build_read_fis(&info, 0x0000010203040506ULL, 4U,
                                    fis, sizeof(fis)), "READ DMA EXT FIS") ||
        !expect((fis[2] == 0x25U) && (fis[4] == 0x06U) &&
                (fis[5] == 0x05U) && (fis[6] == 0x04U) &&
                (fis[8] == 0x03U) && (fis[9] == 0x02U) &&
                (fis[10] == 0x01U) && (fis[12] == 4U) &&
                (fis[13] == 0U), "READ DMA EXT fields") ||
        !expect(ahci_dma_transfer_bytes(512U, 8U, &bytes) &&
                (bytes == 4096U), "4K PRDT bound") ||
        !expect(!ahci_dma_transfer_bytes(512U, 9U, &bytes),
                "oversize PRDT rejection")) {
        return 1;
    }

    words_zero(words);
    words[83] = (uint16_t)(1U << 10);
    words[100] = 100U;
    words[106] = 0x5000U;
    words[117] = 2048U;
    words[118] = 0U;
    if (!expect(ahci_identify_decode(words, 256U, &info), "4K decode") ||
        !expect(info.logical_block_size == 4096U, "4K logical sector") ||
        !expect(ahci_dma_transfer_bytes(4096U, 1U, &bytes) &&
                (bytes == 4096U), "single 4K transfer") ||
        !expect(!ahci_dma_transfer_bytes(4096U, 2U, &bytes),
                "two 4K transfer rejection")) {
        return 1;
    }

    words_zero(words);
    words[60] = 0x1234U;
    words[61] = 0x0001U;
    if (!expect(ahci_identify_decode(words, 256U, &info), "LBA28 fallback") ||
        !expect(!info.lba48, "LBA28 capability") ||
        !expect(info.block_count == 0x00011234ULL, "LBA28 capacity") ||
        !expect(ahci_build_read_fis(&info, 0x1234ULL, 2U,
                                    fis, sizeof(fis)), "READ DMA FIS") ||
        !expect((fis[2] == 0xc8U) && (fis[4] == 0x34U) &&
                (fis[5] == 0x12U) && (fis[12] == 2U),
                "READ DMA fields") ||
        !expect(!ahci_build_read_fis(&info, info.block_count, 1U,
                                     fis, sizeof(fis)),
                "read range rejection")) {
        return 1;
    }

    words_zero(words);
    words[60] = 1U;
    words[106] = 0x5000U;
    words[117] = 2049U;
    if (!expect(!ahci_identify_decode(words, 256U, &info),
                "oversize logical sector rejection") ||
        !expect(!ahci_identify_decode(NULL, 256U, &info), "null words") ||
        !expect(!ahci_identify_decode(words, 255U, &info), "short IDENTIFY") ||
        !expect(!ahci_dma_transfer_bytes(512U, 0U, &bytes), "zero transfer")) {
        return 1;
    }

    (void)printf("ahci-block-host-test: PASS\n");
    return 0;
}
