#ifndef BORING_AHCI_BLOCK_H
#define BORING_AHCI_BLOCK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/ahci.h>
#include <boring/block_device.h>

#define AHCI_BLOCK_DMA_BYTES 4096U
#define AHCI_BLOCK_IDENTIFY_BYTES 512U
#define AHCI_BLOCK_FIS_BYTES 20U
#define AHCI_BLOCK_DEVICE_NAME "sata0"

enum ahci_block_result {
    AHCI_BLOCK_RESULT_OK = 0,
    AHCI_BLOCK_RESULT_ALREADY_INITIALIZED,
    AHCI_BLOCK_RESULT_CONTROLLER_NOT_READY,
    AHCI_BLOCK_RESULT_NO_SATA_PORT,
    AHCI_BLOCK_RESULT_PCI_COMMAND,
    AHCI_BLOCK_RESULT_MMIO_MAP,
    AHCI_BLOCK_RESULT_DMA_ALLOCATION,
    AHCI_BLOCK_RESULT_PORT_SETUP,
    AHCI_BLOCK_RESULT_IDENTIFY_IO,
    AHCI_BLOCK_RESULT_IDENTIFY_DATA,
    AHCI_BLOCK_RESULT_REGISTRATION
};

struct ahci_identify_info {
    uint64_t block_count;
    uint32_t logical_block_size;
    bool lba48;
};

struct ahci_block_stats {
    uint64_t capacity_blocks;
    uint64_t commands_completed;
    uint64_t reads_completed;
    uint32_t logical_block_size;
    uint32_t max_blocks_per_transfer;
    uint8_t port;
    bool lba48;
    bool identify_complete;
    bool registered;
};

/* Pure bounded helpers shared with M56 host fixtures. */
bool ahci_identify_decode(const uint16_t *words, size_t word_count,
                          struct ahci_identify_info *info);
bool ahci_dma_transfer_bytes(uint32_t logical_block_size,
                             uint32_t block_count,
                             uint32_t *byte_count);
bool ahci_build_identify_fis(uint8_t *fis, size_t fis_length);
bool ahci_build_read_fis(const struct ahci_identify_info *info,
                         uint64_t first_block, uint16_t block_count,
                         uint8_t *fis, size_t fis_length);

/* Hardware path: one selected SATA port, synchronous read-only block device. */
enum ahci_block_result ahci_block_init(const struct ahci_state *controller);
const struct block_device *ahci_block_device(void);
bool ahci_block_get_stats(struct ahci_block_stats *stats);
const char *ahci_block_result_name(enum ahci_block_result result);

#endif
