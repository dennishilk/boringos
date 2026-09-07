#ifndef BORING_VIRTIO_BLK_H
#define BORING_VIRTIO_BLK_H

#include <stdbool.h>
#include <stdint.h>

#include <boring/block_device.h>

#define VIRTIO_BLK_QUEUE_SIZE 8U
#define VIRTIO_BLK_DMA_FRAME_COUNT 5U

enum virtio_blk_result {
    VIRTIO_BLK_RESULT_OK = 0,
    VIRTIO_BLK_RESULT_ALREADY_INITIALIZED,
    VIRTIO_BLK_RESULT_NO_DEVICE,
    VIRTIO_BLK_RESULT_PCI_COMMAND,
    VIRTIO_BLK_RESULT_BAD_CAPABILITY,
    VIRTIO_BLK_RESULT_MISSING_CAPABILITY,
    VIRTIO_BLK_RESULT_MMIO_MAP,
    VIRTIO_BLK_RESULT_RESET_TIMEOUT,
    VIRTIO_BLK_RESULT_VERSION_1_REQUIRED,
    VIRTIO_BLK_RESULT_FEATURES_REJECTED,
    VIRTIO_BLK_RESULT_QUEUE_UNAVAILABLE,
    VIRTIO_BLK_RESULT_DMA_ALLOCATION,
    VIRTIO_BLK_RESULT_QUEUE_SETUP,
    VIRTIO_BLK_RESULT_BAD_GEOMETRY,
    VIRTIO_BLK_RESULT_REGISTRATION
};

struct virtio_blk_stats {
    uint8_t pci_bus;
    uint8_t pci_device;
    uint8_t pci_function;
    uint16_t pci_vendor_id;
    uint16_t pci_device_id;
    uint8_t common_bar;
    uint8_t notify_bar;
    uint8_t device_bar;
    uint64_t common_bar_base;
    uint64_t notify_bar_base;
    uint64_t device_bar_base;
    uint64_t device_features;
    uint64_t accepted_features;
    uint64_t capacity_sectors;
    uint64_t submissions;
    uint64_t completions;
    uint32_t notify_off_multiplier;
    uint32_t poll_spin_limit;
    uint16_t queue_size;
    uint16_t queue_notify_off;
    uint8_t dma_frame_count;
    bool pci_discovered;
    bool memory_space_bus_master;
    bool common_config_found;
    bool notify_config_found;
    bool device_config_found;
    bool reset_complete;
    bool version_1_offered;
    bool features_ok;
    bool queue_enabled;
    bool driver_ok;
    bool read_only;
};

enum virtio_blk_result virtio_blk_init(void);
const struct block_device *virtio_blk_device(void);
bool virtio_blk_get_stats(struct virtio_blk_stats *stats);
const char *virtio_blk_result_name(enum virtio_blk_result result);

#endif
