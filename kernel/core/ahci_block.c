#include <stddef.h>
#include <stdint.h>

#include <boring/ahci_block.h>

#define ATA_IDENTIFY_LBA28_LOW 60U
#define ATA_IDENTIFY_LBA28_HIGH 61U
#define ATA_IDENTIFY_COMMAND_SET_2 83U
#define ATA_IDENTIFY_COMMAND_SET_1 82U
#define ATA_IDENTIFY_COMMAND_SET_ENABLED_1 85U
#define ATA_IDENTIFY_SECTOR_INFO 106U
#define ATA_IDENTIFY_LOGICAL_WORDS_LOW 117U
#define ATA_IDENTIFY_LOGICAL_WORDS_HIGH 118U
#define ATA_IDENTIFY_LBA48_0 100U
#define ATA_IDENTIFY_LBA48_1 101U
#define ATA_IDENTIFY_LBA48_2 102U
#define ATA_IDENTIFY_LBA48_3 103U
#define ATA_IDENTIFY_LBA48_SUPPORTED (1U << 10)
#define ATA_IDENTIFY_FLUSH_CACHE_EXT_SUPPORTED (1U << 12)
#define ATA_IDENTIFY_WRITE_CACHE_SUPPORTED (1U << 5)
#define ATA_IDENTIFY_SECTOR_INFO_VALID 0x4000U
#define ATA_IDENTIFY_SECTOR_INFO_VALID_MASK 0xc000U
#define ATA_IDENTIFY_LOGICAL_LONGER (1U << 12)
#define ATA_FIS_TYPE_REG_H2D 0x27U
#define ATA_FIS_COMMAND_BIT 0x80U
#define ATA_CMD_IDENTIFY_DEVICE 0xecU
#define ATA_CMD_READ_DMA 0xc8U
#define ATA_CMD_READ_DMA_EXT 0x25U
#define ATA_CMD_WRITE_DMA 0xcaU
#define ATA_CMD_WRITE_DMA_EXT 0x35U
#define ATA_CMD_FLUSH_CACHE 0xe7U
#define ATA_CMD_FLUSH_CACHE_EXT 0xeaU
#define ATA_DEVICE_LBA 0x40U
#define ATA_LBA28_LIMIT 0x10000000ULL
#define ATA_LBA48_MAX 0xffffffffffffULL

static void bytes_zero(uint8_t *bytes, size_t length) {
    size_t index;
    for (index = 0U; index < length; ++index) {
        bytes[index] = 0U;
    }
}

bool ahci_identify_decode(const uint16_t *words, size_t word_count,
                          struct ahci_identify_info *info) {
    uint64_t lba48_count;
    uint64_t lba28_count;
    uint32_t logical_size = 512U;
    bool lba48;

    if ((words == NULL) || (info == NULL) || (word_count < 256U)) {
        return false;
    }

    lba48_count = (uint64_t)words[ATA_IDENTIFY_LBA48_0] |
        ((uint64_t)words[ATA_IDENTIFY_LBA48_1] << 16U) |
        ((uint64_t)words[ATA_IDENTIFY_LBA48_2] << 32U) |
        ((uint64_t)words[ATA_IDENTIFY_LBA48_3] << 48U);
    lba28_count = (uint64_t)words[ATA_IDENTIFY_LBA28_LOW] |
        ((uint64_t)words[ATA_IDENTIFY_LBA28_HIGH] << 16U);
    lba48 = ((words[ATA_IDENTIFY_COMMAND_SET_2] &
              ATA_IDENTIFY_LBA48_SUPPORTED) != 0U) &&
            (lba48_count != 0ULL);

    if ((words[ATA_IDENTIFY_SECTOR_INFO] &
         ATA_IDENTIFY_SECTOR_INFO_VALID_MASK) ==
            ATA_IDENTIFY_SECTOR_INFO_VALID &&
        (words[ATA_IDENTIFY_SECTOR_INFO] &
         ATA_IDENTIFY_LOGICAL_LONGER) != 0U) {
        const uint32_t logical_words =
            (uint32_t)words[ATA_IDENTIFY_LOGICAL_WORDS_LOW] |
            ((uint32_t)words[ATA_IDENTIFY_LOGICAL_WORDS_HIGH] << 16U);
        if ((logical_words < 256U) ||
            (logical_words > (AHCI_BLOCK_DMA_BYTES / 2U))) {
            return false;
        }
        logical_size = logical_words * 2U;
    }

    if ((logical_size < 512U) ||
        (logical_size > AHCI_BLOCK_DMA_BYTES) ||
        ((AHCI_BLOCK_DMA_BYTES % logical_size) != 0U)) {
        return false;
    }

    info->block_count = lba48 ? lba48_count : lba28_count;
    info->logical_block_size = logical_size;
    info->lba48 = lba48;
    info->write_cache_supported =
        (words[ATA_IDENTIFY_COMMAND_SET_1] &
         ATA_IDENTIFY_WRITE_CACHE_SUPPORTED) != 0U;
    info->write_cache_enabled = info->write_cache_supported &&
        ((words[ATA_IDENTIFY_COMMAND_SET_ENABLED_1] &
          ATA_IDENTIFY_WRITE_CACHE_SUPPORTED) != 0U);
    info->flush_cache_ext_supported = lba48 &&
        ((words[ATA_IDENTIFY_COMMAND_SET_2] &
          ATA_IDENTIFY_FLUSH_CACHE_EXT_SUPPORTED) != 0U);
    return info->block_count != 0ULL;
}

bool ahci_dma_transfer_bytes(uint32_t logical_block_size,
                             uint32_t block_count,
                             uint32_t *byte_count) {
    uint32_t bytes;

    if ((byte_count == NULL) || (logical_block_size == 0U) ||
        (block_count == 0U) ||
        (logical_block_size > AHCI_BLOCK_DMA_BYTES) ||
        ((AHCI_BLOCK_DMA_BYTES % logical_block_size) != 0U) ||
        (block_count > (UINT32_MAX / logical_block_size))) {
        return false;
    }
    bytes = logical_block_size * block_count;
    if ((bytes == 0U) || (bytes > AHCI_BLOCK_DMA_BYTES)) {
        return false;
    }
    *byte_count = bytes;
    return true;
}

bool ahci_build_identify_fis(uint8_t *fis, size_t fis_length) {
    if ((fis == NULL) || (fis_length < (size_t)AHCI_BLOCK_FIS_BYTES)) {
        return false;
    }
    bytes_zero(fis, (size_t)AHCI_BLOCK_FIS_BYTES);
    fis[0] = ATA_FIS_TYPE_REG_H2D;
    fis[1] = ATA_FIS_COMMAND_BIT;
    fis[2] = ATA_CMD_IDENTIFY_DEVICE;
    return true;
}

bool ahci_build_read_fis(const struct ahci_identify_info *info,
                         uint64_t first_block, uint16_t block_count,
                         uint8_t *fis, size_t fis_length) {
    if ((info == NULL) || (fis == NULL) ||
        (fis_length < (size_t)AHCI_BLOCK_FIS_BYTES) ||
        (block_count == 0U) || (info->block_count == 0ULL) ||
        (first_block >= info->block_count) ||
        ((uint64_t)block_count > (info->block_count - first_block))) {
        return false;
    }

    bytes_zero(fis, (size_t)AHCI_BLOCK_FIS_BYTES);
    fis[0] = ATA_FIS_TYPE_REG_H2D;
    fis[1] = ATA_FIS_COMMAND_BIT;
    fis[4] = (uint8_t)(first_block & 0xffULL);
    fis[5] = (uint8_t)((first_block >> 8U) & 0xffULL);
    fis[6] = (uint8_t)((first_block >> 16U) & 0xffULL);

    if (info->lba48) {
        if (first_block > ATA_LBA48_MAX) {
            return false;
        }
        fis[2] = ATA_CMD_READ_DMA_EXT;
        fis[7] = ATA_DEVICE_LBA;
        fis[8] = (uint8_t)((first_block >> 24U) & 0xffULL);
        fis[9] = (uint8_t)((first_block >> 32U) & 0xffULL);
        fis[10] = (uint8_t)((first_block >> 40U) & 0xffULL);
        fis[12] = (uint8_t)((uint32_t)block_count & 0xffU);
        fis[13] = (uint8_t)(((uint32_t)block_count >> 8U) & 0xffU);
    } else {
        if ((first_block >= ATA_LBA28_LIMIT) ||
            (block_count > 255U) ||
            ((uint64_t)block_count > (ATA_LBA28_LIMIT - first_block))) {
            return false;
        }
        fis[2] = ATA_CMD_READ_DMA;
        fis[7] = (uint8_t)(ATA_DEVICE_LBA |
            (uint8_t)((first_block >> 24U) & 0x0fULL));
        fis[12] = (uint8_t)block_count;
    }
    return true;
}

bool ahci_build_write_fis(const struct ahci_identify_info *info,
                          uint64_t first_block, uint16_t block_count,
                          uint8_t *fis, size_t fis_length) {
    if (!ahci_build_read_fis(info, first_block, block_count,
                             fis, fis_length)) {
        return false;
    }
    fis[2] = info->lba48 ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_WRITE_DMA;
    return true;
}

bool ahci_build_flush_fis(const struct ahci_identify_info *info,
                          uint8_t *fis, size_t fis_length) {
    if ((info == NULL) || (fis == NULL) ||
        (fis_length < (size_t)AHCI_BLOCK_FIS_BYTES)) {
        return false;
    }
    bytes_zero(fis, (size_t)AHCI_BLOCK_FIS_BYTES);
    fis[0] = ATA_FIS_TYPE_REG_H2D;
    fis[1] = ATA_FIS_COMMAND_BIT;
    fis[2] = info->flush_cache_ext_supported ?
        ATA_CMD_FLUSH_CACHE_EXT : ATA_CMD_FLUSH_CACHE;
    return true;
}
