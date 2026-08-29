#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/ahci_block.h>
#include <boring/cpu.h>
#include <boring/io.h>
#include <boring/pci.h>
#include <boring/pmm.h>
#include <boring/vmm.h>

#define AHCI_CAP_S64A (1U << 31)
#define AHCI_PORT_BASE 0x100U
#define AHCI_PORT_STRIDE 0x80U
#define AHCI_PXCLB 0x00U
#define AHCI_PXCLBU 0x04U
#define AHCI_PXFB 0x08U
#define AHCI_PXFBU 0x0cU
#define AHCI_PXIS 0x10U
#define AHCI_PXIE 0x14U
#define AHCI_PXCMD 0x18U
#define AHCI_PXTFD 0x20U
#define AHCI_PXSERR 0x30U
#define AHCI_PXSACT 0x34U
#define AHCI_PXCI 0x38U
#define AHCI_PXCMD_ST (1U << 0)
#define AHCI_PXCMD_FRE (1U << 4)
#define AHCI_PXCMD_FR (1U << 14)
#define AHCI_PXCMD_CR (1U << 15)
#define AHCI_PXTFD_ERR (1U << 0)
#define AHCI_PXTFD_DRQ (1U << 3)
#define AHCI_PXTFD_BSY (1U << 7)
#define AHCI_PXIS_TFES (1U << 30)
#define AHCI_COMMAND_FIS_DWORDS 5U
#define AHCI_COMMAND_WRITE (1U << 6)
#define AHCI_PRDT_DBC_MASK 0x003fffffU
#define AHCI_BLOCK_POLL_LIMIT 50000000U
#define AHCI_DMA_FRAME_COUNT 4U
#define AHCI_DMA_COMMAND_LIST 0U
#define AHCI_DMA_RECEIVED_FIS 1U
#define AHCI_DMA_COMMAND_TABLE 2U
#define AHCI_DMA_BOUNCE 3U

struct ahci_command_header {
    uint16_t flags;
    uint16_t prdt_length;
    uint32_t prd_byte_count;
    uint32_t command_table_base;
    uint32_t command_table_base_upper;
    uint32_t reserved[4];
};

struct ahci_prdt_entry {
    uint32_t data_base;
    uint32_t data_base_upper;
    uint32_t reserved;
    uint32_t byte_count_flags;
};

struct ahci_command_table {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t reserved[48];
    struct ahci_prdt_entry prdt[1];
};

struct ahci_block_runtime {
    volatile uint8_t *mmio;
    uint64_t dma_physical[AHCI_DMA_FRAME_COUNT];
    void *dma_virtual[AHCI_DMA_FRAME_COUNT];
    volatile struct ahci_command_header *headers;
    volatile struct ahci_command_table *table;
    uint8_t *bounce;
    struct ahci_identify_info identify;
    struct block_device block_device;
    struct ahci_block_stats stats;
    uint32_t original_cmd;
    uint32_t original_clb;
    uint32_t original_clbu;
    uint32_t original_fb;
    uint32_t original_fbu;
    uint32_t original_ie;
    bool port_saved;
    bool initialized;
    bool busy;
};

_Static_assert(sizeof(struct ahci_command_header) == 32U,
               "AHCI command header must be 32 bytes");
_Static_assert(sizeof(struct ahci_prdt_entry) == 16U,
               "AHCI PRDT entry must be 16 bytes");
_Static_assert(sizeof(struct ahci_command_table) == 144U,
               "single-entry AHCI command table must be 144 bytes");

static struct ahci_block_runtime runtime;

static enum block_device_result ahci_backend_read(void *context,
                                                   uint64_t first_block,
                                                   uint32_t block_count,
                                                   void *buffer);
static enum block_device_result ahci_backend_write(void *context,
                                                    uint64_t first_block,
                                                    uint32_t block_count,
                                                    const void *buffer);

static const struct block_device_ops ahci_block_ops = {
    .read = ahci_backend_read,
    .write = ahci_backend_write
};

static void bytes_zero(void *buffer, size_t length) {
    uint8_t *bytes = (uint8_t *)buffer;
    size_t index;
    for (index = 0U; index < length; ++index) {
        bytes[index] = 0U;
    }
}

static void dma_page_zero(void *page) {
    volatile uint64_t *words = (volatile uint64_t *)page;
    size_t index;
    for (index = 0U; index < ((size_t)PMM_PAGE_SIZE / sizeof(uint64_t)); ++index) {
        words[index] = 0ULL;
    }
}

static void bytes_copy(uint8_t *destination, const uint8_t *source,
                       size_t length) {
    size_t index;
    for (index = 0U; index < length; ++index) {
        destination[index] = source[index];
    }
}

static uint32_t mmio_read32(uint32_t offset) {
    return *(volatile uint32_t *)(volatile void *)(runtime.mmio + offset);
}

static void mmio_write32(uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(volatile void *)(runtime.mmio + offset) = value;
}

static bool port_register(uint8_t port, uint32_t relative,
                          uint32_t *offset) {
    const uint32_t base = AHCI_PORT_BASE +
        ((uint32_t)port * AHCI_PORT_STRIDE);
    if ((offset == NULL) || (port >= AHCI_BORING_MAX_PORTS) ||
        (relative > (AHCI_PORT_STRIDE - sizeof(uint32_t))) ||
        (base > (AHCI_MMIO_WINDOW_SIZE - AHCI_PORT_STRIDE))) {
        return false;
    }
    *offset = base + relative;
    return true;
}

static bool wait_mask(uint32_t offset, uint32_t mask, bool want_set,
                      uint32_t limit) {
    uint32_t spin;
    for (spin = 0U; spin < limit; ++spin) {
        const uint32_t value = mmio_read32(offset);
        if (((value & mask) != 0U) == want_set) {
            return true;
        }
        x86_64_pause();
    }
    return false;
}

static bool dma_address_supported(uint32_t cap, uint64_t physical) {
    return ((cap & AHCI_CAP_S64A) != 0U) || ((physical >> 32U) == 0ULL);
}

static void free_dma_frames(void) {
    size_t index;
    for (index = 0U; index < (size_t)AHCI_DMA_FRAME_COUNT; ++index) {
        if (runtime.dma_physical[index] != 0ULL) {
            (void)pmm_free_frame(runtime.dma_physical[index]);
            runtime.dma_physical[index] = 0ULL;
            runtime.dma_virtual[index] = NULL;
        }
    }
}

static bool allocate_dma_frames(uint32_t cap) {
    size_t index;
    for (index = 0U; index < (size_t)AHCI_DMA_FRAME_COUNT; ++index) {
        uint64_t physical = 0ULL;
        void *virtual_address = NULL;
        if (!pmm_alloc_frame(&physical) ||
            !vmm_pmm_frame_to_hhdm(physical, &virtual_address) ||
            !dma_address_supported(cap, physical) ||
            ((physical & (PMM_PAGE_SIZE - 1ULL)) != 0ULL)) {
            if (physical != 0ULL) {
                (void)pmm_free_frame(physical);
            }
            free_dma_frames();
            return false;
        }
        runtime.dma_physical[index] = physical;
        runtime.dma_virtual[index] = virtual_address;
        dma_page_zero(virtual_address);
    }

    runtime.headers = (volatile struct ahci_command_header *)
        runtime.dma_virtual[AHCI_DMA_COMMAND_LIST];
    runtime.table = (volatile struct ahci_command_table *)
        runtime.dma_virtual[AHCI_DMA_COMMAND_TABLE];
    runtime.bounce = (uint8_t *)runtime.dma_virtual[AHCI_DMA_BOUNCE];
    return true;
}

static bool select_sata_port(const struct ahci_state *controller,
                             uint8_t *selected) {
    uint8_t port;
    if ((controller == NULL) || (selected == NULL) ||
        (controller->inspected_ports == 0U) ||
        (controller->inspected_ports > AHCI_BORING_MAX_PORTS)) {
        return false;
    }
    for (port = 0U; port < controller->inspected_ports; ++port) {
        if (controller->ports[port].implemented &&
            controller->ports[port].facts.sata) {
            *selected = port;
            return true;
        }
    }
    return false;
}

static bool stop_port(uint8_t port) {
    uint32_t cmd_offset;
    uint32_t cmd;
    if (!port_register(port, AHCI_PXCMD, &cmd_offset)) {
        return false;
    }

    cmd = mmio_read32(cmd_offset);
    mmio_write32(cmd_offset, cmd & ~AHCI_PXCMD_ST);
    if (!wait_mask(cmd_offset, AHCI_PXCMD_CR, false, AHCI_WAIT_LIMIT)) {
        return false;
    }
    cmd = mmio_read32(cmd_offset);
    mmio_write32(cmd_offset, cmd & ~AHCI_PXCMD_FRE);
    return wait_mask(cmd_offset, AHCI_PXCMD_FR, false, AHCI_WAIT_LIMIT);
}

static bool save_and_stop_port(uint8_t port) {
    uint32_t offset;
    if (!port_register(port, AHCI_PXCMD, &offset)) {
        return false;
    }
    runtime.original_cmd = mmio_read32(offset);
    if (!port_register(port, AHCI_PXCLB, &offset)) {
        return false;
    }
    runtime.original_clb = mmio_read32(offset);
    if (!port_register(port, AHCI_PXCLBU, &offset)) {
        return false;
    }
    runtime.original_clbu = mmio_read32(offset);
    if (!port_register(port, AHCI_PXFB, &offset)) {
        return false;
    }
    runtime.original_fb = mmio_read32(offset);
    if (!port_register(port, AHCI_PXFBU, &offset)) {
        return false;
    }
    runtime.original_fbu = mmio_read32(offset);
    if (!port_register(port, AHCI_PXIE, &offset)) {
        return false;
    }
    runtime.original_ie = mmio_read32(offset);
    runtime.port_saved = true;
    return stop_port(port);
}

static void restore_port(void) {
    uint32_t offset;
    const uint8_t port = runtime.stats.port;
    if (!runtime.port_saved || (runtime.mmio == NULL)) {
        return;
    }
    (void)stop_port(port);
    if (port_register(port, AHCI_PXCLB, &offset)) {
        mmio_write32(offset, runtime.original_clb);
    }
    if (port_register(port, AHCI_PXCLBU, &offset)) {
        mmio_write32(offset, runtime.original_clbu);
    }
    if (port_register(port, AHCI_PXFB, &offset)) {
        mmio_write32(offset, runtime.original_fb);
    }
    if (port_register(port, AHCI_PXFBU, &offset)) {
        mmio_write32(offset, runtime.original_fbu);
    }
    if (port_register(port, AHCI_PXIE, &offset)) {
        mmio_write32(offset, runtime.original_ie);
    }
    if (port_register(port, AHCI_PXCMD, &offset)) {
        mmio_write32(offset, runtime.original_cmd);
    }
    runtime.port_saved = false;
}

static bool program_port(uint8_t port) {
    uint32_t offset;
    uint32_t cmd;
    const uint64_t clb = runtime.dma_physical[AHCI_DMA_COMMAND_LIST];
    const uint64_t fb = runtime.dma_physical[AHCI_DMA_RECEIVED_FIS];

    if (!save_and_stop_port(port)) {
        return false;
    }
    if (!port_register(port, AHCI_PXCLB, &offset)) {
        return false;
    }
    mmio_write32(offset, (uint32_t)(clb & 0xffffffffULL));
    if (!port_register(port, AHCI_PXCLBU, &offset)) {
        return false;
    }
    mmio_write32(offset, (uint32_t)(clb >> 32U));
    if (!port_register(port, AHCI_PXFB, &offset)) {
        return false;
    }
    mmio_write32(offset, (uint32_t)(fb & 0xffffffffULL));
    if (!port_register(port, AHCI_PXFBU, &offset)) {
        return false;
    }
    mmio_write32(offset, (uint32_t)(fb >> 32U));
    if (!port_register(port, AHCI_PXIE, &offset)) {
        return false;
    }
    mmio_write32(offset, 0U);
    if (!port_register(port, AHCI_PXIS, &offset)) {
        return false;
    }
    mmio_write32(offset, UINT32_MAX);
    if (!port_register(port, AHCI_PXSERR, &offset)) {
        return false;
    }
    mmio_write32(offset, UINT32_MAX);

    if (!port_register(port, AHCI_PXCMD, &offset)) {
        return false;
    }
    cmd = mmio_read32(offset) | AHCI_PXCMD_FRE;
    mmio_write32(offset, cmd);
    if (!wait_mask(offset, AHCI_PXCMD_FR, true, AHCI_WAIT_LIMIT)) {
        return false;
    }
    cmd = mmio_read32(offset) | AHCI_PXCMD_ST;
    mmio_write32(offset, cmd);
    x86_64_memory_barrier();
    return (mmio_read32(offset) & (AHCI_PXCMD_ST | AHCI_PXCMD_FRE)) ==
           (AHCI_PXCMD_ST | AHCI_PXCMD_FRE);
}

static bool prepare_command(const uint8_t *fis, uint32_t byte_count,
                            bool write) {
    volatile struct ahci_command_header *header;
    size_t index;
    const uint64_t table_physical =
        runtime.dma_physical[AHCI_DMA_COMMAND_TABLE];
    const uint64_t data_physical = runtime.dma_physical[AHCI_DMA_BOUNCE];

    if ((fis == NULL) || (byte_count > AHCI_BLOCK_DMA_BYTES) ||
        ((byte_count != 0U) &&
         ((byte_count - 1U) > AHCI_PRDT_DBC_MASK))) {
        return false;
    }

    dma_page_zero(runtime.dma_virtual[AHCI_DMA_COMMAND_LIST]);
    dma_page_zero(runtime.dma_virtual[AHCI_DMA_COMMAND_TABLE]);
    header = &runtime.headers[0];
    header->flags = (uint16_t)(AHCI_COMMAND_FIS_DWORDS |
        (write ? AHCI_COMMAND_WRITE : 0U));
    header->prdt_length = (uint16_t)((byte_count == 0U) ? 0U : 1U);
    header->prd_byte_count = 0U;
    header->command_table_base =
        (uint32_t)(table_physical & 0xffffffffULL);
    header->command_table_base_upper = (uint32_t)(table_physical >> 32U);

    for (index = 0U; index < (size_t)AHCI_BLOCK_FIS_BYTES; ++index) {
        runtime.table->cfis[index] = fis[index];
    }
    if (byte_count != 0U) {
        runtime.table->prdt[0].data_base =
            (uint32_t)(data_physical & 0xffffffffULL);
        runtime.table->prdt[0].data_base_upper =
            (uint32_t)(data_physical >> 32U);
        runtime.table->prdt[0].reserved = 0U;
        runtime.table->prdt[0].byte_count_flags = byte_count - 1U;
    }
    x86_64_memory_barrier();
    return true;
}

static bool issue_command(const uint8_t *fis, uint32_t byte_count,
                          bool write) {
    uint32_t tfd_offset;
    uint32_t is_offset;
    uint32_t ci_offset;
    uint32_t sact_offset;
    uint32_t spin;

    if (!port_register(runtime.stats.port, AHCI_PXTFD, &tfd_offset) ||
        !port_register(runtime.stats.port, AHCI_PXIS, &is_offset) ||
        !port_register(runtime.stats.port, AHCI_PXCI, &ci_offset) ||
        !port_register(runtime.stats.port, AHCI_PXSACT, &sact_offset) ||
        !wait_mask(tfd_offset, AHCI_PXTFD_BSY | AHCI_PXTFD_DRQ,
                   false, AHCI_BLOCK_POLL_LIMIT) ||
        (mmio_read32(ci_offset) != 0U) ||
        (mmio_read32(sact_offset) != 0U) ||
        !prepare_command(fis, byte_count, write)) {
        return false;
    }

    mmio_write32(is_offset, UINT32_MAX);
    x86_64_memory_barrier();
    mmio_write32(ci_offset, 1U);

    for (spin = 0U; spin < AHCI_BLOCK_POLL_LIMIT; ++spin) {
        const uint32_t is = mmio_read32(is_offset);
        if ((is & AHCI_PXIS_TFES) != 0U) {
            return false;
        }
        if ((mmio_read32(ci_offset) & 1U) == 0U) {
            const uint32_t tfd = mmio_read32(tfd_offset);
            x86_64_memory_barrier();
            if (((mmio_read32(is_offset) & AHCI_PXIS_TFES) != 0U) ||
                ((tfd & AHCI_PXTFD_ERR) != 0U)) {
                return false;
            }
            ++runtime.stats.commands_completed;
            return true;
        }
        x86_64_pause();
    }
    return false;
}

static void cleanup_failed_init(void) {
    restore_port();
    free_dma_frames();
    if (runtime.mmio != NULL) {
        (void)vmm_unmap_mmio_region(runtime.mmio, AHCI_MMIO_WINDOW_SIZE);
        runtime.mmio = NULL;
    }
}

static enum block_device_result ahci_backend_read(void *context,
                                                   uint64_t first_block,
                                                   uint32_t block_count,
                                                   void *buffer) {
    uint8_t fis[AHCI_BLOCK_FIS_BYTES];
    uint32_t byte_count;

    if ((context != &runtime) || !runtime.initialized || runtime.busy ||
        (buffer == NULL) || (block_count == 0U) ||
        (block_count > runtime.stats.max_blocks_per_transfer) ||
        !ahci_dma_transfer_bytes(runtime.stats.logical_block_size,
                                 block_count, &byte_count) ||
        !ahci_build_read_fis(&runtime.identify, first_block,
                             (uint16_t)block_count,
                             fis, sizeof(fis))) {
        return BLOCK_DEVICE_RESULT_IO_ERROR;
    }

    runtime.busy = true;
    dma_page_zero(runtime.dma_virtual[AHCI_DMA_BOUNCE]);
    if (!issue_command(fis, byte_count, false)) {
        runtime.busy = false;
        return BLOCK_DEVICE_RESULT_IO_ERROR;
    }
    bytes_copy((uint8_t *)buffer, runtime.bounce, (size_t)byte_count);
    runtime.busy = false;
    ++runtime.stats.reads_completed;
    return BLOCK_DEVICE_RESULT_OK;
}

static enum block_device_result ahci_backend_write(void *context,
                                                    uint64_t first_block,
                                                    uint32_t block_count,
                                                    const void *buffer) {
    uint8_t fis[AHCI_BLOCK_FIS_BYTES];
    uint32_t byte_count;

    if ((context != &runtime) || !runtime.initialized || runtime.busy ||
        (buffer == NULL) || (block_count == 0U) ||
        (block_count > runtime.stats.max_blocks_per_transfer) ||
        !ahci_dma_transfer_bytes(runtime.stats.logical_block_size,
                                 block_count, &byte_count) ||
        !ahci_build_write_fis(&runtime.identify, first_block,
                              (uint16_t)block_count,
                              fis, sizeof(fis))) {
        return BLOCK_DEVICE_RESULT_IO_ERROR;
    }

    runtime.busy = true;
    bytes_copy(runtime.bounce, (const uint8_t *)buffer, (size_t)byte_count);
    x86_64_memory_barrier();
    if (!issue_command(fis, byte_count, true)) {
        runtime.busy = false;
        return BLOCK_DEVICE_RESULT_IO_ERROR;
    }
    ++runtime.stats.writes_completed;

    if (runtime.identify.write_cache_enabled) {
        if (!ahci_build_flush_fis(&runtime.identify, fis, sizeof(fis)) ||
            !issue_command(fis, 0U, false)) {
            runtime.busy = false;
            return BLOCK_DEVICE_RESULT_IO_ERROR;
        }
        ++runtime.stats.flushes_completed;
    }
    runtime.busy = false;
    return BLOCK_DEVICE_RESULT_OK;
}

enum ahci_block_result ahci_block_init(const struct ahci_state *controller) {
    volatile void *mapping = NULL;
    uint8_t fis[AHCI_BLOCK_FIS_BYTES];
    uint8_t port;
    enum block_device_result registration;

    if (runtime.initialized) {
        return AHCI_BLOCK_RESULT_ALREADY_INITIALIZED;
    }
    bytes_zero(&runtime, sizeof(runtime));
    if ((controller == NULL) || !controller->initialized ||
        !controller->ahci_enabled || (controller->abar_physical == 0ULL)) {
        return AHCI_BLOCK_RESULT_CONTROLLER_NOT_READY;
    }
    if (!select_sata_port(controller, &port)) {
        return AHCI_BLOCK_RESULT_NO_SATA_PORT;
    }
    runtime.stats.port = port;

    if (!pci_enable_memory_bus_master(&controller->device)) {
        return AHCI_BLOCK_RESULT_PCI_COMMAND;
    }
    if (!vmm_map_mmio_region(controller->abar_physical,
                             AHCI_MMIO_WINDOW_SIZE, &mapping)) {
        return AHCI_BLOCK_RESULT_MMIO_MAP;
    }
    runtime.mmio = (volatile uint8_t *)mapping;
    if (!allocate_dma_frames(controller->cap)) {
        cleanup_failed_init();
        return AHCI_BLOCK_RESULT_DMA_ALLOCATION;
    }
    if (!program_port(port)) {
        cleanup_failed_init();
        return AHCI_BLOCK_RESULT_PORT_SETUP;
    }

    dma_page_zero(runtime.dma_virtual[AHCI_DMA_BOUNCE]);
    if (!ahci_build_identify_fis(fis, sizeof(fis)) ||
        !issue_command(fis, AHCI_BLOCK_IDENTIFY_BYTES, false)) {
        cleanup_failed_init();
        return AHCI_BLOCK_RESULT_IDENTIFY_IO;
    }
    if (!ahci_identify_decode((const uint16_t *)(const void *)runtime.bounce,
                              AHCI_BLOCK_IDENTIFY_BYTES / sizeof(uint16_t),
                              &runtime.identify)) {
        cleanup_failed_init();
        return AHCI_BLOCK_RESULT_IDENTIFY_DATA;
    }

    runtime.stats.capacity_blocks = runtime.identify.block_count;
    runtime.stats.logical_block_size = runtime.identify.logical_block_size;
    runtime.stats.max_blocks_per_transfer =
        AHCI_BLOCK_DMA_BYTES / runtime.identify.logical_block_size;
    runtime.stats.lba48 = runtime.identify.lba48;
    runtime.stats.write_cache_supported =
        runtime.identify.write_cache_supported;
    runtime.stats.write_cache_enabled = runtime.identify.write_cache_enabled;
    runtime.stats.flush_cache_ext_supported =
        runtime.identify.flush_cache_ext_supported;
    runtime.stats.identify_complete = true;

    runtime.block_device.name = AHCI_BLOCK_DEVICE_NAME;
    runtime.block_device.logical_block_size = runtime.identify.logical_block_size;
    runtime.block_device.block_count = runtime.identify.block_count;
    runtime.block_device.read_only = false;
    runtime.block_device.context = &runtime;
    runtime.block_device.ops = &ahci_block_ops;

    registration = block_device_register(&runtime.block_device);
    if (registration != BLOCK_DEVICE_RESULT_OK) {
        cleanup_failed_init();
        return AHCI_BLOCK_RESULT_REGISTRATION;
    }
    runtime.stats.registered = true;
    runtime.initialized = true;
    return AHCI_BLOCK_RESULT_OK;
}

const struct block_device *ahci_block_device(void) {
    return runtime.initialized ? &runtime.block_device : NULL;
}

bool ahci_block_get_stats(struct ahci_block_stats *stats) {
    if (stats == NULL) {
        return false;
    }
    *stats = runtime.stats;
    return runtime.stats.identify_complete;
}

const char *ahci_block_result_name(enum ahci_block_result result) {
    switch (result) {
        case AHCI_BLOCK_RESULT_OK:
            return "ok";
        case AHCI_BLOCK_RESULT_ALREADY_INITIALIZED:
            return "already-initialized";
        case AHCI_BLOCK_RESULT_CONTROLLER_NOT_READY:
            return "controller-not-ready";
        case AHCI_BLOCK_RESULT_NO_SATA_PORT:
            return "no-sata-port";
        case AHCI_BLOCK_RESULT_PCI_COMMAND:
            return "pci-command";
        case AHCI_BLOCK_RESULT_MMIO_MAP:
            return "mmio-map";
        case AHCI_BLOCK_RESULT_DMA_ALLOCATION:
            return "dma-allocation";
        case AHCI_BLOCK_RESULT_PORT_SETUP:
            return "port-setup";
        case AHCI_BLOCK_RESULT_IDENTIFY_IO:
            return "identify-io";
        case AHCI_BLOCK_RESULT_IDENTIFY_DATA:
            return "identify-data";
        case AHCI_BLOCK_RESULT_REGISTRATION:
            return "registration";
        default:
            return "unknown";
    }
}
