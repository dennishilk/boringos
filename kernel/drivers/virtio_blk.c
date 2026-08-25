#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/block_device.h>
#include <boring/cpu.h>
#include <boring/io.h>
#include <boring/pci.h>
#include <boring/pmm.h>
#include <boring/virtio_blk.h>
#include <boring/vmm.h>

#define VIRTIO_PCI_CAP_VENDOR 0x09U
#define VIRTIO_PCI_CAP_COMMON_CFG 1U
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2U
#define VIRTIO_PCI_CAP_DEVICE_CFG 4U
#define VIRTIO_PCI_CAP_BASE_LENGTH 16U
#define VIRTIO_PCI_NOTIFY_CAP_LENGTH 20U
#define VIRTIO_COMMON_MIN_LENGTH 56U
#define VIRTIO_BLK_DEVICE_CFG_MIN_LENGTH 8U

#define VIRTIO_COMMON_DEVICE_FEATURE_SELECT 0U
#define VIRTIO_COMMON_DEVICE_FEATURE 4U
#define VIRTIO_COMMON_DRIVER_FEATURE_SELECT 8U
#define VIRTIO_COMMON_DRIVER_FEATURE 12U
#define VIRTIO_COMMON_NUM_QUEUES 18U
#define VIRTIO_COMMON_DEVICE_STATUS 20U
#define VIRTIO_COMMON_CONFIG_GENERATION 21U
#define VIRTIO_COMMON_QUEUE_SELECT 22U
#define VIRTIO_COMMON_QUEUE_SIZE 24U
#define VIRTIO_COMMON_QUEUE_ENABLE 28U
#define VIRTIO_COMMON_QUEUE_NOTIFY_OFF 30U
#define VIRTIO_COMMON_QUEUE_DESC 32U
#define VIRTIO_COMMON_QUEUE_DRIVER 40U
#define VIRTIO_COMMON_QUEUE_DEVICE 48U

#define VIRTIO_STATUS_ACKNOWLEDGE 1U
#define VIRTIO_STATUS_DRIVER 2U
#define VIRTIO_STATUS_DRIVER_OK 4U
#define VIRTIO_STATUS_FEATURES_OK 8U
#define VIRTIO_STATUS_FAILED 128U

#define VIRTIO_F_VERSION_1 (1ULL << 32U)
#define VIRTIO_BLK_F_RO (1ULL << 5U)

#define VIRTQ_DESC_F_NEXT 1U
#define VIRTQ_DESC_F_WRITE 2U
#define VIRTQ_AVAIL_F_NO_INTERRUPT 1U
#define VIRTQ_DESC_HEADER 0U
#define VIRTQ_DESC_DATA 1U
#define VIRTQ_DESC_STATUS 2U

#define VIRTIO_BLK_T_IN 0U
#define VIRTIO_BLK_T_OUT 1U
#define VIRTIO_BLK_S_OK 0U
#define VIRTIO_BLK_STATUS_PENDING 0xffU
#define VIRTIO_BLK_SECTOR_SIZE 512U
#define VIRTIO_BLK_BOUNCE_SECTORS ((uint32_t)(PMM_PAGE_SIZE / VIRTIO_BLK_SECTOR_SIZE))
#define VIRTIO_BLK_POLL_SPIN_LIMIT 50000000U
#define VIRTIO_BLK_RESET_SPIN_LIMIT 1000000U
#define VIRTIO_CONFIG_READ_RETRIES 8U

#define DMA_DESC_FRAME 0U
#define DMA_AVAIL_FRAME 1U
#define DMA_USED_FRAME 2U
#define DMA_REQUEST_FRAME 3U
#define DMA_BOUNCE_FRAME 4U

struct virtio_pci_region {
    uint8_t bar_index;
    uint32_t offset;
    uint32_t length;
    uint32_t notify_off_multiplier;
    struct pci_bar bar;
    bool present;
};

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
};

struct virtio_blk_request_header {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};

struct virtio_blk_state {
    struct pci_device pci;
    struct virtio_pci_region common_region;
    struct virtio_pci_region notify_region;
    struct virtio_pci_region device_region;
    volatile uint8_t *common;
    volatile uint8_t *notify;
    volatile uint8_t *device_config;
    volatile uint16_t *notify_address;
    uint64_t dma_physical[VIRTIO_BLK_DMA_FRAME_COUNT];
    void *dma_virtual[VIRTIO_BLK_DMA_FRAME_COUNT];
    volatile struct virtq_desc *descriptors;
    volatile uint8_t *avail_ring;
    volatile uint8_t *used_ring;
    volatile struct virtio_blk_request_header *request_header;
    volatile uint8_t *request_status;
    uint8_t *bounce;
    uint16_t avail_index;
    uint16_t used_index;
    struct block_device block_device;
    struct virtio_blk_stats stats;
    bool initialized;
    bool busy;
};

_Static_assert(sizeof(struct virtq_desc) == 16U,
               "split virtqueue descriptor must be 16 bytes");
_Static_assert(sizeof(struct virtq_used_elem) == 8U,
               "split virtqueue used element must be 8 bytes");
_Static_assert(sizeof(struct virtio_blk_request_header) == 16U,
               "virtio block request header must be 16 bytes");
_Static_assert(VIRTIO_BLK_BOUNCE_SECTORS == 8U,
               "one 4 KiB bounce frame must hold eight sectors");

static struct virtio_blk_state virtio_state;

static enum block_device_result virtio_backend_read(void *context,
                                                     uint64_t first_block,
                                                     uint32_t block_count,
                                                     void *buffer);
static enum block_device_result virtio_backend_write(void *context,
                                                      uint64_t first_block,
                                                      uint32_t block_count,
                                                      const void *buffer);

static const struct block_device_ops virtio_block_ops = {
    .read = virtio_backend_read,
    .write = virtio_backend_write
};

static uint8_t virtio_mmio_read8(volatile uint8_t *base, size_t offset) {
    return *(volatile uint8_t *)(base + offset);
}

static uint16_t virtio_mmio_read16(volatile uint8_t *base, size_t offset) {
    return *(volatile uint16_t *)(void *)(base + offset);
}

static uint32_t virtio_mmio_read32(volatile uint8_t *base, size_t offset) {
    return *(volatile uint32_t *)(void *)(base + offset);
}

static void virtio_mmio_write8(volatile uint8_t *base,
                               size_t offset,
                               uint8_t value) {
    *(volatile uint8_t *)(base + offset) = value;
}

static void virtio_mmio_write16(volatile uint8_t *base,
                                size_t offset,
                                uint16_t value) {
    *(volatile uint16_t *)(void *)(base + offset) = value;
}

static void virtio_mmio_write32(volatile uint8_t *base,
                                size_t offset,
                                uint32_t value) {
    *(volatile uint32_t *)(void *)(base + offset) = value;
}

static void virtio_mmio_write64_split(volatile uint8_t *base,
                                      size_t offset,
                                      uint64_t value) {
    virtio_mmio_write32(base, offset, (uint32_t)(value & 0xffffffffULL));
    virtio_mmio_write32(base, offset + 4U, (uint32_t)(value >> 32U));
}

static void virtio_zero_page(void *page) {
    volatile uint64_t *words = (volatile uint64_t *)page;
    size_t index;

    for (index = 0U; index < ((size_t)PMM_PAGE_SIZE / sizeof(uint64_t)); ++index) {
        words[index] = 0ULL;
    }
}

static void virtio_copy_to_dma(uint8_t *destination,
                               const uint8_t *source,
                               size_t length) {
    size_t index;

    for (index = 0U; index < length; ++index) {
        destination[index] = source[index];
    }
}

static void virtio_copy_from_dma(uint8_t *destination,
                                 const uint8_t *source,
                                 size_t length) {
    size_t index;

    for (index = 0U; index < length; ++index) {
        destination[index] = source[index];
    }
}

static bool virtio_config_read8(uint8_t offset, uint8_t *value) {
    return pci_config_read8(virtio_state.pci.bdf, (uint16_t)offset, value);
}

static bool virtio_config_read32(uint8_t offset, uint32_t *value) {
    return pci_config_read32(virtio_state.pci.bdf, (uint16_t)offset, value);
}

static bool virtio_parse_region(uint8_t cap_offset,
                                uint8_t expected_type,
                                struct virtio_pci_region *region) {
    uint8_t cap_id;
    uint8_t cap_len;
    uint8_t cfg_type;
    uint8_t bar_index;
    uint32_t offset;
    uint32_t length;
    uint16_t cap_end;
    struct pci_bar bar;

    if ((region == NULL) ||
        !virtio_config_read8(cap_offset, &cap_id) ||
        (cap_id != VIRTIO_PCI_CAP_VENDOR) ||
        !virtio_config_read8((uint8_t)(cap_offset + 2U), &cap_len) ||
        (cap_len < VIRTIO_PCI_CAP_BASE_LENGTH)) {
        return false;
    }

    cap_end = (uint16_t)cap_offset + (uint16_t)cap_len;
    if (cap_end > 256U ||
        !virtio_config_read8((uint8_t)(cap_offset + 3U), &cfg_type) ||
        (cfg_type != expected_type) ||
        !virtio_config_read8((uint8_t)(cap_offset + 4U), &bar_index) ||
        (bar_index >= 6U) ||
        !virtio_config_read32((uint8_t)(cap_offset + 8U), &offset) ||
        !virtio_config_read32((uint8_t)(cap_offset + 12U), &length) ||
        (length == 0U) ||
        !pci_get_bar(&virtio_state.pci, bar_index, &bar) || !bar.memory) {
        return false;
    }

    if (bar.base > (UINT64_MAX - (uint64_t)offset)) {
        return false;
    }
    {
        const uint64_t region_base = bar.base + (uint64_t)offset;
        const uint64_t region_last_delta = (uint64_t)length - 1ULL;
        if (region_base > (UINT64_MAX - region_last_delta)) {
            return false;
        }
    }

    region->bar_index = bar_index;
    region->offset = offset;
    region->length = length;
    region->notify_off_multiplier = 0U;
    region->bar = bar;
    region->present = true;

    if (expected_type == VIRTIO_PCI_CAP_NOTIFY_CFG) {
        if ((cap_len < VIRTIO_PCI_NOTIFY_CAP_LENGTH) ||
            !virtio_config_read32((uint8_t)(cap_offset + 16U),
                                  &region->notify_off_multiplier)) {
            region->present = false;
            return false;
        }
    }

    return true;
}

static enum virtio_blk_result virtio_discover_capabilities(void) {
    uint8_t offsets[PCI_MAX_CAPABILITIES];
    size_t count;
    size_t index;

    if (!pci_list_capabilities(&virtio_state.pci, offsets,
                               (size_t)PCI_MAX_CAPABILITIES, &count)) {
        return VIRTIO_BLK_RESULT_BAD_CAPABILITY;
    }

    for (index = 0U; index < count; ++index) {
        uint8_t cap_id;
        uint8_t cfg_type;

        if (!virtio_config_read8(offsets[index], &cap_id)) {
            return VIRTIO_BLK_RESULT_BAD_CAPABILITY;
        }
        if (cap_id != VIRTIO_PCI_CAP_VENDOR) {
            continue;
        }
        if (!virtio_config_read8((uint8_t)(offsets[index] + 3U), &cfg_type)) {
            return VIRTIO_BLK_RESULT_BAD_CAPABILITY;
        }

        if ((cfg_type == VIRTIO_PCI_CAP_COMMON_CFG) &&
            !virtio_state.common_region.present) {
            if (!virtio_parse_region(offsets[index], cfg_type,
                                     &virtio_state.common_region)) {
                return VIRTIO_BLK_RESULT_BAD_CAPABILITY;
            }
        } else if ((cfg_type == VIRTIO_PCI_CAP_NOTIFY_CFG) &&
                   !virtio_state.notify_region.present) {
            if (!virtio_parse_region(offsets[index], cfg_type,
                                     &virtio_state.notify_region)) {
                return VIRTIO_BLK_RESULT_BAD_CAPABILITY;
            }
        } else if ((cfg_type == VIRTIO_PCI_CAP_DEVICE_CFG) &&
                   !virtio_state.device_region.present) {
            if (!virtio_parse_region(offsets[index], cfg_type,
                                     &virtio_state.device_region)) {
                return VIRTIO_BLK_RESULT_BAD_CAPABILITY;
            }
        }
    }

    if (!virtio_state.common_region.present ||
        !virtio_state.notify_region.present ||
        !virtio_state.device_region.present) {
        return VIRTIO_BLK_RESULT_MISSING_CAPABILITY;
    }
    if ((virtio_state.common_region.length < VIRTIO_COMMON_MIN_LENGTH) ||
        (virtio_state.notify_region.length < 2U) ||
        (virtio_state.device_region.length < VIRTIO_BLK_DEVICE_CFG_MIN_LENGTH)) {
        return VIRTIO_BLK_RESULT_BAD_CAPABILITY;
    }

    virtio_state.stats.common_config_found = true;
    virtio_state.stats.notify_config_found = true;
    virtio_state.stats.device_config_found = true;
    virtio_state.stats.common_bar = virtio_state.common_region.bar_index;
    virtio_state.stats.notify_bar = virtio_state.notify_region.bar_index;
    virtio_state.stats.device_bar = virtio_state.device_region.bar_index;
    virtio_state.stats.common_bar_base = virtio_state.common_region.bar.base;
    virtio_state.stats.notify_bar_base = virtio_state.notify_region.bar.base;
    virtio_state.stats.device_bar_base = virtio_state.device_region.bar.base;
    virtio_state.stats.notify_off_multiplier =
        virtio_state.notify_region.notify_off_multiplier;
    return VIRTIO_BLK_RESULT_OK;
}

static bool virtio_map_region(const struct virtio_pci_region *region,
                              volatile uint8_t **mapped) {
    uint64_t physical;
    volatile void *virtual_address;

    if ((region == NULL) || (mapped == NULL) || !region->present ||
        (region->bar.base > (UINT64_MAX - (uint64_t)region->offset))) {
        return false;
    }
    physical = region->bar.base + (uint64_t)region->offset;
    if (!vmm_map_mmio_region(physical, (size_t)region->length,
                             &virtual_address)) {
        return false;
    }

    *mapped = (volatile uint8_t *)virtual_address;
    return true;
}

static void virtio_unmap_regions(void) {
    if (virtio_state.device_config != NULL) {
        (void)vmm_unmap_mmio_region(virtio_state.device_config,
                                    (size_t)virtio_state.device_region.length);
        virtio_state.device_config = NULL;
    }
    if (virtio_state.notify != NULL) {
        (void)vmm_unmap_mmio_region(virtio_state.notify,
                                    (size_t)virtio_state.notify_region.length);
        virtio_state.notify = NULL;
    }
    if (virtio_state.common != NULL) {
        (void)vmm_unmap_mmio_region(virtio_state.common,
                                    (size_t)virtio_state.common_region.length);
        virtio_state.common = NULL;
    }
}

static bool virtio_reset_device(void) {
    uint32_t spins;

    virtio_mmio_write8(virtio_state.common,
                       VIRTIO_COMMON_DEVICE_STATUS, 0U);
    x86_64_memory_barrier();

    for (spins = 0U; spins < VIRTIO_BLK_RESET_SPIN_LIMIT; ++spins) {
        if (virtio_mmio_read8(virtio_state.common,
                              VIRTIO_COMMON_DEVICE_STATUS) == 0U) {
            virtio_state.stats.reset_complete = true;
            return true;
        }
        x86_64_pause();
    }

    return false;
}

static void virtio_add_status(uint8_t flag) {
    const uint8_t status = virtio_mmio_read8(
        virtio_state.common, VIRTIO_COMMON_DEVICE_STATUS);
    virtio_mmio_write8(virtio_state.common,
                       VIRTIO_COMMON_DEVICE_STATUS,
                       (uint8_t)(status | flag));
    x86_64_memory_barrier();
}

static void virtio_fail_and_reset(void) {
    if (virtio_state.common != NULL) {
        uint8_t status = virtio_mmio_read8(
            virtio_state.common, VIRTIO_COMMON_DEVICE_STATUS);
        if (status != 0U) {
            virtio_mmio_write8(virtio_state.common,
                               VIRTIO_COMMON_DEVICE_STATUS,
                               (uint8_t)(status | VIRTIO_STATUS_FAILED));
            x86_64_memory_barrier();
        }
        virtio_mmio_write8(virtio_state.common,
                           VIRTIO_COMMON_DEVICE_STATUS, 0U);
        x86_64_memory_barrier();
    }
}

static uint64_t virtio_read_device_features(void) {
    uint32_t low;
    uint32_t high;

    virtio_mmio_write32(virtio_state.common,
                        VIRTIO_COMMON_DEVICE_FEATURE_SELECT, 0U);
    low = virtio_mmio_read32(virtio_state.common,
                             VIRTIO_COMMON_DEVICE_FEATURE);
    virtio_mmio_write32(virtio_state.common,
                        VIRTIO_COMMON_DEVICE_FEATURE_SELECT, 1U);
    high = virtio_mmio_read32(virtio_state.common,
                              VIRTIO_COMMON_DEVICE_FEATURE);
    return (uint64_t)low | ((uint64_t)high << 32U);
}

static bool virtio_negotiate_features(void) {
    const uint64_t offered = virtio_read_device_features();
    uint64_t accepted = VIRTIO_F_VERSION_1;
    uint8_t status;

    virtio_state.stats.device_features = offered;
    if ((offered & VIRTIO_F_VERSION_1) == 0ULL) {
        return false;
    }
    virtio_state.stats.version_1_offered = true;

    if ((offered & VIRTIO_BLK_F_RO) != 0ULL) {
        accepted |= VIRTIO_BLK_F_RO;
        virtio_state.stats.read_only = true;
    }
    virtio_state.stats.accepted_features = accepted;

    virtio_mmio_write32(virtio_state.common,
                        VIRTIO_COMMON_DRIVER_FEATURE_SELECT, 0U);
    virtio_mmio_write32(virtio_state.common,
                        VIRTIO_COMMON_DRIVER_FEATURE,
                        (uint32_t)(accepted & 0xffffffffULL));
    virtio_mmio_write32(virtio_state.common,
                        VIRTIO_COMMON_DRIVER_FEATURE_SELECT, 1U);
    virtio_mmio_write32(virtio_state.common,
                        VIRTIO_COMMON_DRIVER_FEATURE,
                        (uint32_t)(accepted >> 32U));

    virtio_add_status(VIRTIO_STATUS_FEATURES_OK);
    status = virtio_mmio_read8(virtio_state.common,
                               VIRTIO_COMMON_DEVICE_STATUS);
    if ((status & VIRTIO_STATUS_FEATURES_OK) == 0U) {
        return false;
    }

    virtio_state.stats.features_ok = true;
    return true;
}

static void virtio_free_dma_frames(void) {
    size_t index;

    for (index = 0U; index < (size_t)VIRTIO_BLK_DMA_FRAME_COUNT; ++index) {
        if (virtio_state.dma_physical[index] != 0ULL) {
            (void)pmm_free_frame(virtio_state.dma_physical[index]);
            virtio_state.dma_physical[index] = 0ULL;
            virtio_state.dma_virtual[index] = NULL;
        }
    }
}

static bool virtio_allocate_dma_frames(void) {
    size_t index;

    for (index = 0U; index < (size_t)VIRTIO_BLK_DMA_FRAME_COUNT; ++index) {
        uint64_t physical = 0ULL;
        void *virtual_address;

        if (!pmm_alloc_frame(&physical) ||
            !vmm_pmm_frame_to_hhdm(physical, &virtual_address)) {
            if (physical != 0ULL) {
                (void)pmm_free_frame(physical);
            }
            virtio_free_dma_frames();
            return false;
        }

        virtio_state.dma_physical[index] = physical;
        virtio_state.dma_virtual[index] = virtual_address;
        virtio_zero_page(virtual_address);
    }

    virtio_state.stats.dma_frame_count = VIRTIO_BLK_DMA_FRAME_COUNT;
    return true;
}

static enum virtio_blk_result virtio_setup_queue(void) {
    uint16_t queue_max;
    uint16_t queue_enable;
    uint16_t num_queues;
    uint16_t notify_off;
    uint64_t notify_relative;

    num_queues = virtio_mmio_read16(virtio_state.common,
                                    VIRTIO_COMMON_NUM_QUEUES);
    if (num_queues == 0U) {
        return VIRTIO_BLK_RESULT_QUEUE_UNAVAILABLE;
    }

    virtio_mmio_write16(virtio_state.common,
                        VIRTIO_COMMON_QUEUE_SELECT, 0U);
    queue_max = virtio_mmio_read16(virtio_state.common,
                                   VIRTIO_COMMON_QUEUE_SIZE);
    queue_enable = virtio_mmio_read16(virtio_state.common,
                                      VIRTIO_COMMON_QUEUE_ENABLE);
    if ((queue_max < VIRTIO_BLK_QUEUE_SIZE) || (queue_enable != 0U)) {
        return VIRTIO_BLK_RESULT_QUEUE_UNAVAILABLE;
    }

    if (!virtio_allocate_dma_frames()) {
        return VIRTIO_BLK_RESULT_DMA_ALLOCATION;
    }

    virtio_state.descriptors =
        (volatile struct virtq_desc *)virtio_state.dma_virtual[DMA_DESC_FRAME];
    virtio_state.avail_ring =
        (volatile uint8_t *)virtio_state.dma_virtual[DMA_AVAIL_FRAME];
    virtio_state.used_ring =
        (volatile uint8_t *)virtio_state.dma_virtual[DMA_USED_FRAME];
    virtio_state.request_header =
        (volatile struct virtio_blk_request_header *)
            virtio_state.dma_virtual[DMA_REQUEST_FRAME];
    virtio_state.request_status =
        (volatile uint8_t *)virtio_state.dma_virtual[DMA_REQUEST_FRAME] +
        sizeof(struct virtio_blk_request_header);
    virtio_state.bounce =
        (uint8_t *)virtio_state.dma_virtual[DMA_BOUNCE_FRAME];

    *(volatile uint16_t *)(void *)(virtio_state.avail_ring + 0U) =
        VIRTQ_AVAIL_F_NO_INTERRUPT;
    *(volatile uint16_t *)(void *)(virtio_state.avail_ring + 2U) = 0U;
    virtio_state.avail_index = 0U;
    virtio_state.used_index = 0U;

    virtio_mmio_write16(virtio_state.common,
                        VIRTIO_COMMON_QUEUE_SIZE,
                        VIRTIO_BLK_QUEUE_SIZE);
    virtio_mmio_write64_split(virtio_state.common,
                              VIRTIO_COMMON_QUEUE_DESC,
                              virtio_state.dma_physical[DMA_DESC_FRAME]);
    virtio_mmio_write64_split(virtio_state.common,
                              VIRTIO_COMMON_QUEUE_DRIVER,
                              virtio_state.dma_physical[DMA_AVAIL_FRAME]);
    virtio_mmio_write64_split(virtio_state.common,
                              VIRTIO_COMMON_QUEUE_DEVICE,
                              virtio_state.dma_physical[DMA_USED_FRAME]);
    virtio_mmio_write16(virtio_state.common,
                        VIRTIO_COMMON_QUEUE_ENABLE, 1U);
    x86_64_memory_barrier();

    if (virtio_mmio_read16(virtio_state.common,
                           VIRTIO_COMMON_QUEUE_ENABLE) != 1U) {
        return VIRTIO_BLK_RESULT_QUEUE_SETUP;
    }

    notify_off = virtio_mmio_read16(virtio_state.common,
                                    VIRTIO_COMMON_QUEUE_NOTIFY_OFF);
    notify_relative = (uint64_t)notify_off *
        (uint64_t)virtio_state.notify_region.notify_off_multiplier;
    if ((notify_relative > UINT32_MAX) ||
        (notify_relative > (uint64_t)virtio_state.notify_region.length) ||
        ((uint64_t)virtio_state.notify_region.length - notify_relative < 2ULL)) {
        return VIRTIO_BLK_RESULT_QUEUE_SETUP;
    }

    virtio_state.notify_address = (volatile uint16_t *)(void *)(
        virtio_state.notify + (size_t)notify_relative);
    virtio_state.stats.queue_size = VIRTIO_BLK_QUEUE_SIZE;
    virtio_state.stats.queue_notify_off = notify_off;
    virtio_state.stats.queue_enabled = true;
    return VIRTIO_BLK_RESULT_OK;
}

static bool virtio_read_capacity(uint64_t *capacity) {
    uint8_t before;
    uint8_t after;
    uint32_t low;
    uint32_t high;
    uint32_t attempt;

    if (capacity == NULL) {
        return false;
    }

    for (attempt = 0U; attempt < VIRTIO_CONFIG_READ_RETRIES; ++attempt) {
        before = virtio_mmio_read8(virtio_state.common,
                                   VIRTIO_COMMON_CONFIG_GENERATION);
        low = virtio_mmio_read32(virtio_state.device_config, 0U);
        high = virtio_mmio_read32(virtio_state.device_config, 4U);
        after = virtio_mmio_read8(virtio_state.common,
                                  VIRTIO_COMMON_CONFIG_GENERATION);
        if (before == after) {
            *capacity = (uint64_t)low | ((uint64_t)high << 32U);
            return *capacity != 0ULL;
        }
    }

    return false;
}

static void virtio_prepare_descriptor(uint16_t index,
                                      uint64_t address,
                                      uint32_t length,
                                      uint16_t flags,
                                      uint16_t next) {
    virtio_state.descriptors[index].addr = address;
    virtio_state.descriptors[index].len = length;
    virtio_state.descriptors[index].flags = flags;
    virtio_state.descriptors[index].next = next;
}

static bool virtio_wait_for_completion(void) {
    volatile uint16_t *used_idx =
        (volatile uint16_t *)(void *)(virtio_state.used_ring + 2U);
    uint32_t spins;
    uint16_t observed = virtio_state.used_index;

    for (spins = 0U; spins < VIRTIO_BLK_POLL_SPIN_LIMIT; ++spins) {
        observed = *used_idx;
        if (observed != virtio_state.used_index) {
            break;
        }
        x86_64_pause();
    }

    if (observed == virtio_state.used_index) {
        return false;
    }

    x86_64_memory_barrier();
    if (observed != (uint16_t)(virtio_state.used_index + 1U)) {
        return false;
    }

    {
        const size_t slot = (size_t)(virtio_state.used_index %
                                     virtio_state.stats.queue_size);
        volatile struct virtq_used_elem *used =
            (volatile struct virtq_used_elem *)(void *)(
                virtio_state.used_ring + 4U +
                (slot * sizeof(struct virtq_used_elem)));
        if (used->id != (uint32_t)VIRTQ_DESC_HEADER) {
            return false;
        }
    }

    virtio_state.used_index = observed;
    ++virtio_state.stats.completions;
    return true;
}

static bool virtio_submit_chunk(bool write,
                                uint64_t sector,
                                uint32_t sector_count,
                                uint8_t *buffer) {
    const uint32_t byte_count = sector_count * VIRTIO_BLK_SECTOR_SIZE;
    volatile uint16_t *avail_idx =
        (volatile uint16_t *)(void *)(virtio_state.avail_ring + 2U);
    volatile uint16_t *avail_entries =
        (volatile uint16_t *)(void *)(virtio_state.avail_ring + 4U);
    const size_t avail_slot = (size_t)(virtio_state.avail_index %
                                      virtio_state.stats.queue_size);

    if ((buffer == NULL) || (sector_count == 0U) ||
        (sector_count > VIRTIO_BLK_BOUNCE_SECTORS) || virtio_state.busy) {
        return false;
    }

    virtio_state.busy = true;
    if (write) {
        virtio_copy_to_dma(virtio_state.bounce, buffer,
                           (size_t)byte_count);
    }

    virtio_state.request_header->type =
        write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    virtio_state.request_header->reserved = 0U;
    virtio_state.request_header->sector = sector;
    *virtio_state.request_status = VIRTIO_BLK_STATUS_PENDING;

    virtio_prepare_descriptor(VIRTQ_DESC_HEADER,
                              virtio_state.dma_physical[DMA_REQUEST_FRAME],
                              (uint32_t)sizeof(struct virtio_blk_request_header),
                              VIRTQ_DESC_F_NEXT,
                              VIRTQ_DESC_DATA);
    virtio_prepare_descriptor(VIRTQ_DESC_DATA,
                              virtio_state.dma_physical[DMA_BOUNCE_FRAME],
                              byte_count,
                              (uint16_t)(VIRTQ_DESC_F_NEXT |
                                  (write ? 0U : VIRTQ_DESC_F_WRITE)),
                              VIRTQ_DESC_STATUS);
    virtio_prepare_descriptor(VIRTQ_DESC_STATUS,
                              virtio_state.dma_physical[DMA_REQUEST_FRAME] +
                                  (uint64_t)sizeof(struct virtio_blk_request_header),
                              1U,
                              VIRTQ_DESC_F_WRITE,
                              0U);

    x86_64_memory_barrier();
    avail_entries[avail_slot] = VIRTQ_DESC_HEADER;
    x86_64_memory_barrier();
    virtio_state.avail_index = (uint16_t)(virtio_state.avail_index + 1U);
    *avail_idx = virtio_state.avail_index;
    x86_64_memory_barrier();

    *virtio_state.notify_address = 0U;
    ++virtio_state.stats.submissions;

    if (!virtio_wait_for_completion()) {
        virtio_state.busy = false;
        return false;
    }
    x86_64_memory_barrier();

    if (*virtio_state.request_status != VIRTIO_BLK_S_OK) {
        virtio_state.busy = false;
        return false;
    }

    if (!write) {
        virtio_copy_from_dma(buffer, virtio_state.bounce,
                             (size_t)byte_count);
    }

    virtio_state.busy = false;
    return true;
}

static enum block_device_result virtio_transfer(bool write,
                                                 void *context,
                                                 uint64_t first_block,
                                                 uint32_t block_count,
                                                 void *buffer) {
    uint32_t completed = 0U;
    uint8_t *bytes = (uint8_t *)buffer;

    if ((context != &virtio_state) || !virtio_state.initialized ||
        (buffer == NULL) || (block_count == 0U) ||
        (first_block >= virtio_state.stats.capacity_sectors) ||
        ((uint64_t)block_count >
         (virtio_state.stats.capacity_sectors - first_block))) {
        return BLOCK_DEVICE_RESULT_IO_ERROR;
    }

    if (write && virtio_state.stats.read_only) {
        return BLOCK_DEVICE_RESULT_READ_ONLY;
    }

    while (completed < block_count) {
        const uint32_t remaining = block_count - completed;
        const uint32_t chunk = (remaining > VIRTIO_BLK_BOUNCE_SECTORS) ?
            VIRTIO_BLK_BOUNCE_SECTORS : remaining;
        const uint64_t sector = first_block + (uint64_t)completed;
        const size_t byte_offset = (size_t)completed *
                                   (size_t)VIRTIO_BLK_SECTOR_SIZE;

        if (!virtio_submit_chunk(write, sector, chunk, bytes + byte_offset)) {
            return BLOCK_DEVICE_RESULT_IO_ERROR;
        }
        completed += chunk;
    }

    return BLOCK_DEVICE_RESULT_OK;
}

static enum block_device_result virtio_backend_read(void *context,
                                                     uint64_t first_block,
                                                     uint32_t block_count,
                                                     void *buffer) {
    return virtio_transfer(false, context, first_block, block_count, buffer);
}

static enum block_device_result virtio_backend_write(void *context,
                                                      uint64_t first_block,
                                                      uint32_t block_count,
                                                      const void *buffer) {
    return virtio_transfer(true, context, first_block, block_count,
                           (void *)(uintptr_t)buffer);
}

static void virtio_reset_software_state(void) {
    size_t index;
    uint8_t *bytes = (uint8_t *)&virtio_state;

    for (index = 0U; index < sizeof(virtio_state); ++index) {
        bytes[index] = 0U;
    }
    virtio_state.stats.poll_spin_limit = VIRTIO_BLK_POLL_SPIN_LIMIT;
}

enum virtio_blk_result virtio_blk_init(void) {
    enum virtio_blk_result result;
    uint64_t capacity;
    enum block_device_result registration;

    if (virtio_state.initialized) {
        return VIRTIO_BLK_RESULT_ALREADY_INITIALIZED;
    }
    virtio_reset_software_state();

    if (!pci_find_modern_virtio_block(&virtio_state.pci)) {
        return VIRTIO_BLK_RESULT_NO_DEVICE;
    }
    virtio_state.stats.pci_discovered = true;
    virtio_state.stats.pci_bus = virtio_state.pci.bdf.bus;
    virtio_state.stats.pci_device = virtio_state.pci.bdf.device;
    virtio_state.stats.pci_function = virtio_state.pci.bdf.function;
    virtio_state.stats.pci_vendor_id = virtio_state.pci.vendor_id;
    virtio_state.stats.pci_device_id = virtio_state.pci.device_id;

    if (!pci_enable_memory_bus_master(&virtio_state.pci)) {
        return VIRTIO_BLK_RESULT_PCI_COMMAND;
    }
    virtio_state.stats.memory_space_bus_master = true;

    result = virtio_discover_capabilities();
    if (result != VIRTIO_BLK_RESULT_OK) {
        return result;
    }

    if (!virtio_map_region(&virtio_state.common_region, &virtio_state.common) ||
        !virtio_map_region(&virtio_state.notify_region, &virtio_state.notify) ||
        !virtio_map_region(&virtio_state.device_region,
                           &virtio_state.device_config)) {
        virtio_unmap_regions();
        return VIRTIO_BLK_RESULT_MMIO_MAP;
    }

    if (!virtio_reset_device()) {
        virtio_fail_and_reset();
        virtio_unmap_regions();
        return VIRTIO_BLK_RESULT_RESET_TIMEOUT;
    }

    virtio_add_status(VIRTIO_STATUS_ACKNOWLEDGE);
    virtio_add_status(VIRTIO_STATUS_DRIVER);

    if ((virtio_read_device_features() & VIRTIO_F_VERSION_1) == 0ULL) {
        virtio_state.stats.device_features = virtio_read_device_features();
        virtio_fail_and_reset();
        virtio_unmap_regions();
        return VIRTIO_BLK_RESULT_VERSION_1_REQUIRED;
    }

    if (!virtio_negotiate_features()) {
        virtio_fail_and_reset();
        virtio_unmap_regions();
        return VIRTIO_BLK_RESULT_FEATURES_REJECTED;
    }

    result = virtio_setup_queue();
    if (result != VIRTIO_BLK_RESULT_OK) {
        virtio_fail_and_reset();
        virtio_free_dma_frames();
        virtio_unmap_regions();
        return result;
    }

    if (!virtio_read_capacity(&capacity) ||
        (capacity > (UINT64_MAX / (uint64_t)VIRTIO_BLK_SECTOR_SIZE))) {
        virtio_fail_and_reset();
        virtio_free_dma_frames();
        virtio_unmap_regions();
        return VIRTIO_BLK_RESULT_BAD_GEOMETRY;
    }
    virtio_state.stats.capacity_sectors = capacity;

    virtio_state.block_device.name = "vblk0";
    virtio_state.block_device.logical_block_size = VIRTIO_BLK_SECTOR_SIZE;
    virtio_state.block_device.block_count = capacity;
    virtio_state.block_device.read_only = virtio_state.stats.read_only;
    virtio_state.block_device.context = &virtio_state;
    virtio_state.block_device.ops = &virtio_block_ops;

    virtio_add_status(VIRTIO_STATUS_DRIVER_OK);
    if ((virtio_mmio_read8(virtio_state.common,
                           VIRTIO_COMMON_DEVICE_STATUS) &
         VIRTIO_STATUS_DRIVER_OK) == 0U) {
        virtio_fail_and_reset();
        virtio_free_dma_frames();
        virtio_unmap_regions();
        return VIRTIO_BLK_RESULT_QUEUE_SETUP;
    }
    virtio_state.stats.driver_ok = true;

    registration = block_device_register(&virtio_state.block_device);
    if (registration != BLOCK_DEVICE_RESULT_OK) {
        virtio_fail_and_reset();
        virtio_free_dma_frames();
        virtio_unmap_regions();
        return VIRTIO_BLK_RESULT_REGISTRATION;
    }

    virtio_state.initialized = true;
    return VIRTIO_BLK_RESULT_OK;
}

const struct block_device *virtio_blk_device(void) {
    return virtio_state.initialized ? &virtio_state.block_device : NULL;
}

bool virtio_blk_get_stats(struct virtio_blk_stats *stats) {
    if (stats == NULL) {
        return false;
    }
    *stats = virtio_state.stats;
    return virtio_state.stats.pci_discovered;
}

const char *virtio_blk_result_name(enum virtio_blk_result result) {
    switch (result) {
        case VIRTIO_BLK_RESULT_OK:
            return "ok";
        case VIRTIO_BLK_RESULT_ALREADY_INITIALIZED:
            return "already-initialized";
        case VIRTIO_BLK_RESULT_NO_DEVICE:
            return "no-device";
        case VIRTIO_BLK_RESULT_PCI_COMMAND:
            return "pci-command";
        case VIRTIO_BLK_RESULT_BAD_CAPABILITY:
            return "bad-capability";
        case VIRTIO_BLK_RESULT_MISSING_CAPABILITY:
            return "missing-capability";
        case VIRTIO_BLK_RESULT_MMIO_MAP:
            return "mmio-map";
        case VIRTIO_BLK_RESULT_RESET_TIMEOUT:
            return "reset-timeout";
        case VIRTIO_BLK_RESULT_VERSION_1_REQUIRED:
            return "version-1-required";
        case VIRTIO_BLK_RESULT_FEATURES_REJECTED:
            return "features-rejected";
        case VIRTIO_BLK_RESULT_QUEUE_UNAVAILABLE:
            return "queue-unavailable";
        case VIRTIO_BLK_RESULT_DMA_ALLOCATION:
            return "dma-allocation";
        case VIRTIO_BLK_RESULT_QUEUE_SETUP:
            return "queue-setup";
        case VIRTIO_BLK_RESULT_BAD_GEOMETRY:
            return "bad-geometry";
        case VIRTIO_BLK_RESULT_REGISTRATION:
            return "registration";
        default:
            return "unknown";
    }
}
