#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/ahci.h>
#include <boring/block_device.h>
#include <boring/cpu.h>
#include <boring/pci.h>
#include <boring/pci_inventory.h>
#include <boring/pmm.h>
#include <boring/vmm.h>

#define AHCI_PCI_COMMAND_MEMORY (1U << 1)
#define AHCI_PCI_COMMAND_BUS_MASTER (1U << 2)
#define AHCI_ABAR_INDEX 5U
#define AHCI_CAP_OFFSET 0x00U
#define AHCI_GHC_OFFSET 0x04U
#define AHCI_PI_OFFSET 0x0cU
#define AHCI_VS_OFFSET 0x10U
#define AHCI_CAP2_OFFSET 0x24U
#define AHCI_BOHC_OFFSET 0x28U
#define AHCI_GHC_HR (1U << 0)
#define AHCI_GHC_AE (1U << 31)
#define AHCI_CAP_S64A (1U << 31)
#define AHCI_CAP2_BOH (1U << 0)
#define AHCI_BOHC_BOS (1U << 0)
#define AHCI_BOHC_OOS (1U << 1)
#define AHCI_PORT_BASE 0x100U
#define AHCI_PORT_STRIDE 0x80U
#define AHCI_PXCLB_OFFSET 0x00U
#define AHCI_PXCLBU_OFFSET 0x04U
#define AHCI_PXFB_OFFSET 0x08U
#define AHCI_PXFBU_OFFSET 0x0cU
#define AHCI_PXIS_OFFSET 0x10U
#define AHCI_PXIE_OFFSET 0x14U
#define AHCI_PXCMD_OFFSET 0x18U
#define AHCI_PXTFD_OFFSET 0x20U
#define AHCI_PXSIG_OFFSET 0x24U
#define AHCI_PXSSTS_OFFSET 0x28U
#define AHCI_PXSERR_OFFSET 0x30U
#define AHCI_PXSACT_OFFSET 0x34U
#define AHCI_PXCI_OFFSET 0x38U
#define AHCI_PXCMD_ST (1U << 0)
#define AHCI_PXCMD_FRE (1U << 4)
#define AHCI_PXCMD_FR (1U << 14)
#define AHCI_PXCMD_CR (1U << 15)
#define AHCI_PXTFD_ERR (1U << 0)
#define AHCI_PXTFD_DRQ (1U << 3)
#define AHCI_PXTFD_BSY (1U << 7)
#define AHCI_PXIS_TFES (1U << 30)
#define AHCI_COMMAND_SLOT 0U
#define AHCI_COMMAND_SLOT_MASK (1U << AHCI_COMMAND_SLOT)
#define AHCI_COMMAND_FIS_DWORDS 5U
#define AHCI_FIS_TYPE_REG_H2D 0x27U
#define AHCI_FIS_COMMAND (1U << 7)
#define AHCI_ATA_DEVICE_LBA (1U << 6)
#define ATA_COMMAND_IDENTIFY_DEVICE 0xecU
#define ATA_COMMAND_READ_DMA_EXT 0x25U
#define AHCI_DMA_FRAME_COUNT 4U
#define AHCI_DMA_COMMAND_LIST 0U
#define AHCI_DMA_RECEIVED_FIS 1U
#define AHCI_DMA_COMMAND_TABLE 2U
#define AHCI_DMA_DATA 3U
#define AHCI_IDENTIFY_BYTES 512U
#define AHCI_PRDT_DBC_MASK 0x003fffffU

struct ahci_command_header {
    uint16_t flags;
    uint16_t prdt_length;
    uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t reserved[4];
};

struct ahci_prdt_entry {
    uint32_t dba;
    uint32_t dbau;
    uint32_t reserved;
    uint32_t dbc_ioc;
};

struct ahci_command_table {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t reserved[48];
    struct ahci_prdt_entry prdt[1];
};

struct ahci_runtime_state {
    volatile uint8_t *mmio;
    uint16_t original_pci_command;
    uint32_t original_ghc;
    uint32_t original_bohc;
    bool pci_command_changed;
    bool ghc_changed;
    bool bohc_changed;
};

struct ahci_block_runtime_state {
    uint64_t dma_physical[AHCI_DMA_FRAME_COUNT];
    void *dma_virtual[AHCI_DMA_FRAME_COUNT];
    volatile struct ahci_command_header *command_list;
    volatile struct ahci_command_table *command_table;
    uint8_t *data;
    struct block_device block_device;
    struct ahci_block_stats stats;
    uint32_t port_base;
    uint32_t original_cmd;
    uint32_t original_clb;
    uint32_t original_clbu;
    uint32_t original_fb;
    uint32_t original_fbu;
    uint32_t original_ie;
    uint16_t original_pci_command;
    bool pci_command_changed;
    bool port_programmed;
    bool initialized;
    bool busy;
};

enum ahci_submit_result {
    AHCI_SUBMIT_OK = 0,
    AHCI_SUBMIT_TIMEOUT,
    AHCI_SUBMIT_DEVICE_ERROR
};

_Static_assert(sizeof(struct ahci_command_header) == 32U,
               "AHCI command header must be 32 bytes");
_Static_assert(sizeof(struct ahci_prdt_entry) == 16U,
               "AHCI PRDT entry must be 16 bytes");
_Static_assert(sizeof(struct ahci_command_table) == 144U,
               "single-PRDT AHCI command table must be 144 bytes");
_Static_assert(AHCI_IDENTIFY_BYTES <= AHCI_M56_DMA_BYTES,
               "IDENTIFY data must fit the bounded DMA page");
_Static_assert(PMM_PAGE_SIZE == AHCI_M56_DMA_BYTES,
               "M56 uses one PMM page as its bounded data bounce buffer");

static struct ahci_runtime_state runtime_state;
static struct ahci_block_runtime_state block_state;
static struct ahci_state active_state;

static enum block_device_result ahci_backend_read(void *context,
                                                   uint64_t first_block,
                                                   uint32_t block_count,
                                                   void *buffer);

static const struct block_device_ops ahci_block_ops = {
    .read = ahci_backend_read,
    .write = NULL
};

static void bytes_zero(void *buffer, size_t length) {
    uint8_t *bytes = (uint8_t *)buffer;
    size_t index;
    for (index = 0U; index < length; ++index) {
        bytes[index] = 0U;
    }
}

static void bytes_copy(uint8_t *destination, const uint8_t *source,
                       size_t length) {
    size_t index;
    for (index = 0U; index < length; ++index) {
        destination[index] = source[index];
    }
}

static uint32_t mmio_read32(const volatile uint8_t *base, uint32_t offset) {
    return *(const volatile uint32_t *)(const volatile void *)(base + offset);
}

static void mmio_write32(volatile uint8_t *base, uint32_t offset,
                         uint32_t value) {
    *(volatile uint32_t *)(volatile void *)(base + offset) = value;
}

static bool poll_mmio32(void *context, uint32_t offset, uint32_t *value) {
    const volatile uint8_t *base = (const volatile uint8_t *)context;
    if ((base == NULL) || (value == NULL) ||
        (offset > (AHCI_MMIO_WINDOW_SIZE - sizeof(uint32_t)))) {
        return false;
    }
    x86_64_pause();
    *value = mmio_read32(base, offset);
    return true;
}

static bool pci_enable_memory(const struct pci_device *device) {
    uint16_t command;
    uint16_t verify;

    if ((device == NULL) ||
        !pci_config_read16(device->bdf, 0x04U, &command)) {
        return false;
    }
    runtime_state.original_pci_command = command;
    if ((command & AHCI_PCI_COMMAND_MEMORY) != 0U) {
        return true;
    }
    if (!pci_config_write16(device->bdf, 0x04U,
                            (uint16_t)(command | AHCI_PCI_COMMAND_MEMORY)) ||
        !pci_config_read16(device->bdf, 0x04U, &verify) ||
        ((verify & AHCI_PCI_COMMAND_MEMORY) == 0U)) {
        return false;
    }
    runtime_state.pci_command_changed = true;
    return true;
}

static bool block_enable_bus_master(void) {
    uint16_t command;
    uint16_t verify;

    if (!pci_config_read16(active_state.device.bdf, 0x04U, &command)) {
        return false;
    }
    block_state.original_pci_command = command;
    if ((command & AHCI_PCI_COMMAND_BUS_MASTER) != 0U) {
        block_state.stats.bus_master_enabled = true;
        return true;
    }
    if (!pci_config_write16(active_state.device.bdf, 0x04U,
                            (uint16_t)(command | AHCI_PCI_COMMAND_BUS_MASTER)) ||
        !pci_config_read16(active_state.device.bdf, 0x04U, &verify) ||
        ((verify & AHCI_PCI_COMMAND_BUS_MASTER) == 0U)) {
        return false;
    }
    block_state.pci_command_changed = true;
    block_state.stats.bus_master_enabled = true;
    return true;
}

static bool bios_handoff(volatile uint8_t *base, struct ahci_state *state) {
    uint32_t last;

    state->bohc = mmio_read32(base, AHCI_BOHC_OFFSET);
    if ((state->cap2 & AHCI_CAP2_BOH) == 0U) {
        state->bios_handoff_complete = true;
        return true;
    }

    runtime_state.original_bohc = state->bohc;
    if ((state->bohc & AHCI_BOHC_OOS) == 0U) {
        mmio_write32(base, AHCI_BOHC_OFFSET, state->bohc | AHCI_BOHC_OOS);
        runtime_state.bohc_changed = true;
    }
    if (!ahci_wait_mask_bounded(poll_mmio32, (void *)base,
                                AHCI_BOHC_OFFSET, AHCI_BOHC_BOS, false,
                                AHCI_WAIT_LIMIT, &last)) {
        state->bohc = last;
        return false;
    }
    state->bohc = last;
    state->bios_handoff_complete = (last & AHCI_BOHC_OOS) != 0U;
    return state->bios_handoff_complete;
}

static bool enable_ahci_mode(volatile uint8_t *base, struct ahci_state *state) {
    uint32_t last;
    uint32_t ghc = mmio_read32(base, AHCI_GHC_OFFSET);

    runtime_state.original_ghc = ghc;
    if ((ghc & AHCI_GHC_HR) != 0U) {
        if (!ahci_wait_mask_bounded(poll_mmio32, (void *)base,
                                    AHCI_GHC_OFFSET, AHCI_GHC_HR, false,
                                    AHCI_WAIT_LIMIT, &last)) {
            return false;
        }
        ghc = last;
        runtime_state.original_ghc = ghc;
    }
    if ((ghc & AHCI_GHC_AE) == 0U) {
        mmio_write32(base, AHCI_GHC_OFFSET, ghc | AHCI_GHC_AE);
        runtime_state.ghc_changed = true;
    }
    state->ghc = mmio_read32(base, AHCI_GHC_OFFSET);
    state->ahci_enabled = (state->ghc & AHCI_GHC_AE) != 0U;
    return state->ahci_enabled && ((state->ghc & AHCI_GHC_HR) == 0U);
}

static bool inspect_ports(volatile uint8_t *base, struct ahci_state *state) {
    uint32_t implemented_mask;
    uint8_t port;

    if (!ahci_bounded_ports(state->cap, state->pi,
                            &state->hardware_ports,
                            &state->inspected_ports,
                            &implemented_mask,
                            &state->ports_truncated)) {
        return false;
    }

    for (port = 0U; port < state->inspected_ports; ++port) {
        struct ahci_port_state *port_state = &state->ports[port];
        const uint32_t port_base = AHCI_PORT_BASE +
            ((uint32_t)port * AHCI_PORT_STRIDE);

        port_state->index = port;
        if ((implemented_mask & (1U << port)) == 0U) {
            continue;
        }
        if (port_base > (AHCI_MMIO_WINDOW_SIZE - AHCI_PORT_STRIDE)) {
            return false;
        }

        port_state->implemented = true;
        port_state->cmd = mmio_read32(base, port_base + AHCI_PXCMD_OFFSET);
        port_state->tfd = mmio_read32(base, port_base + AHCI_PXTFD_OFFSET);
        port_state->sig = mmio_read32(base, port_base + AHCI_PXSIG_OFFSET);
        port_state->ssts = mmio_read32(base, port_base + AHCI_PXSSTS_OFFSET);
        port_state->engine_active =
            (port_state->cmd & (AHCI_PXCMD_ST | AHCI_PXCMD_FRE |
                                AHCI_PXCMD_FR | AHCI_PXCMD_CR)) != 0U;
        if (!ahci_decode_port_status(port_state->ssts, port_state->sig,
                                     &port_state->facts)) {
            return false;
        }
        ++state->implemented_ports;
        if (port_state->facts.present) {
            ++state->present_ports;
        }
        if (port_state->facts.sata) {
            ++state->sata_ports;
        }
    }
    return true;
}

static bool block_wait_port_mask(uint32_t relative_offset, uint32_t mask,
                                 bool want_set, uint32_t limit) {
    uint32_t last;
    if ((runtime_state.mmio == NULL) ||
        (block_state.port_base > (AHCI_MMIO_WINDOW_SIZE - AHCI_PORT_STRIDE))) {
        return false;
    }
    return ahci_wait_mask_bounded(poll_mmio32, (void *)runtime_state.mmio,
                                  block_state.port_base + relative_offset,
                                  mask, want_set, limit, &last);
}

static bool block_stop_engine(void) {
    uint32_t cmd;

    if (runtime_state.mmio == NULL) {
        return false;
    }

    cmd = mmio_read32(runtime_state.mmio,
                      block_state.port_base + AHCI_PXCMD_OFFSET);
    if ((cmd & AHCI_PXCMD_ST) != 0U) {
        mmio_write32(runtime_state.mmio,
                     block_state.port_base + AHCI_PXCMD_OFFSET,
                     cmd & ~AHCI_PXCMD_ST);
    }
    if (!block_wait_port_mask(AHCI_PXCMD_OFFSET, AHCI_PXCMD_CR, false,
                              AHCI_WAIT_LIMIT)) {
        return false;
    }

    cmd = mmio_read32(runtime_state.mmio,
                      block_state.port_base + AHCI_PXCMD_OFFSET);
    if ((cmd & AHCI_PXCMD_FRE) != 0U) {
        mmio_write32(runtime_state.mmio,
                     block_state.port_base + AHCI_PXCMD_OFFSET,
                     cmd & ~AHCI_PXCMD_FRE);
    }
    return block_wait_port_mask(AHCI_PXCMD_OFFSET, AHCI_PXCMD_FR, false,
                                AHCI_WAIT_LIMIT);
}

static bool block_start_engine(void) {
    uint32_t cmd;

    if (runtime_state.mmio == NULL) {
        return false;
    }
    cmd = mmio_read32(runtime_state.mmio,
                      block_state.port_base + AHCI_PXCMD_OFFSET);
    cmd |= AHCI_PXCMD_FRE;
    mmio_write32(runtime_state.mmio,
                 block_state.port_base + AHCI_PXCMD_OFFSET, cmd);
    x86_64_memory_barrier();
    cmd |= AHCI_PXCMD_ST;
    mmio_write32(runtime_state.mmio,
                 block_state.port_base + AHCI_PXCMD_OFFSET, cmd);
    x86_64_memory_barrier();
    cmd = mmio_read32(runtime_state.mmio,
                      block_state.port_base + AHCI_PXCMD_OFFSET);
    block_state.stats.command_engine_started =
        (cmd & (AHCI_PXCMD_ST | AHCI_PXCMD_FRE)) ==
        (AHCI_PXCMD_ST | AHCI_PXCMD_FRE);
    return block_state.stats.command_engine_started;
}

static bool block_dma_address_supported(uint64_t physical) {
    return ((active_state.cap & AHCI_CAP_S64A) != 0U) ||
           ((physical >> 32U) == 0ULL);
}

static void block_free_dma(void) {
    size_t index;

    for (index = 0U; index < (size_t)AHCI_DMA_FRAME_COUNT; ++index) {
        if (block_state.dma_physical[index] != 0ULL) {
            (void)pmm_free_frame(block_state.dma_physical[index]);
        }
        block_state.dma_physical[index] = 0ULL;
        block_state.dma_virtual[index] = NULL;
    }
    block_state.command_list = NULL;
    block_state.command_table = NULL;
    block_state.data = NULL;
    block_state.stats.dma_frame_count = 0U;
}

static bool block_allocate_dma(void) {
    size_t index;

    for (index = 0U; index < (size_t)AHCI_DMA_FRAME_COUNT; ++index) {
        uint64_t physical = 0ULL;
        void *virtual_address = NULL;

        if (!pmm_alloc_frame(&physical) ||
            !block_dma_address_supported(physical) ||
            !vmm_pmm_frame_to_hhdm(physical, &virtual_address)) {
            if (physical != 0ULL) {
                (void)pmm_free_frame(physical);
            }
            block_free_dma();
            return false;
        }
        block_state.dma_physical[index] = physical;
        block_state.dma_virtual[index] = virtual_address;
        bytes_zero(virtual_address, (size_t)PMM_PAGE_SIZE);
    }

    block_state.command_list = (volatile struct ahci_command_header *)
        block_state.dma_virtual[AHCI_DMA_COMMAND_LIST];
    block_state.command_table = (volatile struct ahci_command_table *)
        block_state.dma_virtual[AHCI_DMA_COMMAND_TABLE];
    block_state.data = (uint8_t *)block_state.dma_virtual[AHCI_DMA_DATA];
    block_state.stats.dma_frame_count = AHCI_DMA_FRAME_COUNT;
    return true;
}

static bool block_setup_port(void) {
    const uint64_t clb = block_state.dma_physical[AHCI_DMA_COMMAND_LIST];
    const uint64_t fb = block_state.dma_physical[AHCI_DMA_RECEIVED_FIS];

    block_state.original_cmd = mmio_read32(
        runtime_state.mmio, block_state.port_base + AHCI_PXCMD_OFFSET);
    block_state.original_clb = mmio_read32(
        runtime_state.mmio, block_state.port_base + AHCI_PXCLB_OFFSET);
    block_state.original_clbu = mmio_read32(
        runtime_state.mmio, block_state.port_base + AHCI_PXCLBU_OFFSET);
    block_state.original_fb = mmio_read32(
        runtime_state.mmio, block_state.port_base + AHCI_PXFB_OFFSET);
    block_state.original_fbu = mmio_read32(
        runtime_state.mmio, block_state.port_base + AHCI_PXFBU_OFFSET);
    block_state.original_ie = mmio_read32(
        runtime_state.mmio, block_state.port_base + AHCI_PXIE_OFFSET);

    if (!block_stop_engine()) {
        return false;
    }

    mmio_write32(runtime_state.mmio,
                 block_state.port_base + AHCI_PXCLB_OFFSET,
                 (uint32_t)(clb & 0xffffffffULL));
    mmio_write32(runtime_state.mmio,
                 block_state.port_base + AHCI_PXCLBU_OFFSET,
                 (uint32_t)(clb >> 32U));
    mmio_write32(runtime_state.mmio,
                 block_state.port_base + AHCI_PXFB_OFFSET,
                 (uint32_t)(fb & 0xffffffffULL));
    mmio_write32(runtime_state.mmio,
                 block_state.port_base + AHCI_PXFBU_OFFSET,
                 (uint32_t)(fb >> 32U));
    mmio_write32(runtime_state.mmio,
                 block_state.port_base + AHCI_PXIE_OFFSET, 0U);
    mmio_write32(runtime_state.mmio,
                 block_state.port_base + AHCI_PXIS_OFFSET, UINT32_MAX);
    mmio_write32(runtime_state.mmio,
                 block_state.port_base + AHCI_PXSERR_OFFSET, UINT32_MAX);
    block_state.port_programmed = true;

    return block_start_engine();
}

static bool block_restore_port(void) {
    uint32_t cmd;
    bool ok = true;

    if (!block_state.port_programmed || (runtime_state.mmio == NULL)) {
        return true;
    }

    if (!block_stop_engine()) {
        ok = false;
    }
    mmio_write32(runtime_state.mmio,
                 block_state.port_base + AHCI_PXCLB_OFFSET,
                 block_state.original_clb);
    mmio_write32(runtime_state.mmio,
                 block_state.port_base + AHCI_PXCLBU_OFFSET,
                 block_state.original_clbu);
    mmio_write32(runtime_state.mmio,
                 block_state.port_base + AHCI_PXFB_OFFSET,
                 block_state.original_fb);
    mmio_write32(runtime_state.mmio,
                 block_state.port_base + AHCI_PXFBU_OFFSET,
                 block_state.original_fbu);
    mmio_write32(runtime_state.mmio,
                 block_state.port_base + AHCI_PXIE_OFFSET,
                 block_state.original_ie);

    cmd = block_state.original_cmd & ~(AHCI_PXCMD_ST | AHCI_PXCMD_FRE);
    mmio_write32(runtime_state.mmio,
                 block_state.port_base + AHCI_PXCMD_OFFSET, cmd);
    if ((block_state.original_cmd & AHCI_PXCMD_FRE) != 0U) {
        cmd |= AHCI_PXCMD_FRE;
        mmio_write32(runtime_state.mmio,
                     block_state.port_base + AHCI_PXCMD_OFFSET, cmd);
    }
    if ((block_state.original_cmd & AHCI_PXCMD_ST) != 0U) {
        cmd |= AHCI_PXCMD_ST;
        mmio_write32(runtime_state.mmio,
                     block_state.port_base + AHCI_PXCMD_OFFSET, cmd);
    }
    block_state.port_programmed = false;
    block_state.stats.command_engine_started = false;
    return ok;
}

static bool block_cleanup(void) {
    const bool registered = block_state.stats.registered;
    const struct block_device saved_device = block_state.block_device;
    bool ok = true;

    if (!block_restore_port()) {
        ok = false;
    }
    block_free_dma();
    if (block_state.pci_command_changed &&
        !pci_config_write16(active_state.device.bdf, 0x04U,
                            block_state.original_pci_command)) {
        ok = false;
    }
    bytes_zero(&block_state, sizeof(block_state));
    if (registered) {
        block_state.block_device = saved_device;
    }
    return ok;
}

static void block_prepare_command(uint8_t command, uint64_t lba,
                                  uint16_t sector_count,
                                  uint32_t byte_count) {
    volatile struct ahci_command_header *header = block_state.command_list;
    volatile struct ahci_command_table *table = block_state.command_table;
    const uint64_t table_physical =
        block_state.dma_physical[AHCI_DMA_COMMAND_TABLE];
    const uint64_t data_physical =
        block_state.dma_physical[AHCI_DMA_DATA];

    bytes_zero(block_state.dma_virtual[AHCI_DMA_COMMAND_LIST],
               (size_t)PMM_PAGE_SIZE);
    bytes_zero(block_state.dma_virtual[AHCI_DMA_COMMAND_TABLE],
               (size_t)PMM_PAGE_SIZE);

    header->flags = AHCI_COMMAND_FIS_DWORDS;
    header->prdt_length = 1U;
    header->prdbc = 0U;
    header->ctba = (uint32_t)(table_physical & 0xffffffffULL);
    header->ctbau = (uint32_t)(table_physical >> 32U);

    table->prdt[0].dba = (uint32_t)(data_physical & 0xffffffffULL);
    table->prdt[0].dbau = (uint32_t)(data_physical >> 32U);
    table->prdt[0].reserved = 0U;
    table->prdt[0].dbc_ioc = (byte_count - 1U) & AHCI_PRDT_DBC_MASK;

    table->cfis[0] = AHCI_FIS_TYPE_REG_H2D;
    table->cfis[1] = AHCI_FIS_COMMAND;
    table->cfis[2] = command;
    if (command == ATA_COMMAND_READ_DMA_EXT) {
        table->cfis[4] = (uint8_t)(lba & 0xffULL);
        table->cfis[5] = (uint8_t)((lba >> 8U) & 0xffULL);
        table->cfis[6] = (uint8_t)((lba >> 16U) & 0xffULL);
        table->cfis[7] = AHCI_ATA_DEVICE_LBA;
        table->cfis[8] = (uint8_t)((lba >> 24U) & 0xffULL);
        table->cfis[9] = (uint8_t)((lba >> 32U) & 0xffULL);
        table->cfis[10] = (uint8_t)((lba >> 40U) & 0xffULL);
        table->cfis[12] = (uint8_t)(sector_count & 0xffU);
        table->cfis[13] = (uint8_t)(sector_count >> 8U);
    }
}

static enum ahci_submit_result block_submit(uint8_t command, uint64_t lba,
                                            uint16_t sector_count,
                                            uint32_t byte_count) {
    uint32_t ci;
    uint32_t is;
    uint32_t tfd;

    if (block_state.busy || !block_state.port_programmed ||
        (byte_count == 0U) || (byte_count > AHCI_M56_DMA_BYTES)) {
        return AHCI_SUBMIT_DEVICE_ERROR;
    }

    block_state.busy = true;
    if (!block_wait_port_mask(AHCI_PXTFD_OFFSET,
                              AHCI_PXTFD_BSY | AHCI_PXTFD_DRQ,
                              false, AHCI_COMMAND_WAIT_LIMIT) ||
        !block_wait_port_mask(AHCI_PXCI_OFFSET, AHCI_COMMAND_SLOT_MASK,
                              false, AHCI_COMMAND_WAIT_LIMIT) ||
        !block_wait_port_mask(AHCI_PXSACT_OFFSET, AHCI_COMMAND_SLOT_MASK,
                              false, AHCI_COMMAND_WAIT_LIMIT)) {
        block_state.busy = false;
        return AHCI_SUBMIT_TIMEOUT;
    }

    block_prepare_command(command, lba, sector_count, byte_count);
    mmio_write32(runtime_state.mmio,
                 block_state.port_base + AHCI_PXIS_OFFSET, UINT32_MAX);
    mmio_write32(runtime_state.mmio,
                 block_state.port_base + AHCI_PXSERR_OFFSET, UINT32_MAX);
    x86_64_memory_barrier();

    ci = mmio_read32(runtime_state.mmio,
                     block_state.port_base + AHCI_PXCI_OFFSET);
    mmio_write32(runtime_state.mmio,
                 block_state.port_base + AHCI_PXCI_OFFSET,
                 ci | AHCI_COMMAND_SLOT_MASK);
    x86_64_memory_barrier();

    if (command == ATA_COMMAND_IDENTIFY_DEVICE) {
        ++block_state.stats.identify_commands;
    } else if (command == ATA_COMMAND_READ_DMA_EXT) {
        ++block_state.stats.read_commands;
    }

    if (!block_wait_port_mask(AHCI_PXCI_OFFSET, AHCI_COMMAND_SLOT_MASK,
                              false, AHCI_COMMAND_WAIT_LIMIT)) {
        block_state.busy = false;
        return AHCI_SUBMIT_TIMEOUT;
    }
    x86_64_memory_barrier();

    is = mmio_read32(runtime_state.mmio,
                     block_state.port_base + AHCI_PXIS_OFFSET);
    tfd = mmio_read32(runtime_state.mmio,
                      block_state.port_base + AHCI_PXTFD_OFFSET);
    block_state.busy = false;
    if (((is & AHCI_PXIS_TFES) != 0U) || ((tfd & AHCI_PXTFD_ERR) != 0U)) {
        return AHCI_SUBMIT_DEVICE_ERROR;
    }
    return AHCI_SUBMIT_OK;
}

static enum block_device_result ahci_backend_read(void *context,
                                                   uint64_t first_block,
                                                   uint32_t block_count,
                                                   void *buffer) {
    uint32_t completed = 0U;
    uint8_t *bytes = (uint8_t *)buffer;

    if ((context != &block_state) || !block_state.initialized ||
        (buffer == NULL) ||
        !ahci_lba_range_valid(block_state.stats.logical_blocks,
                              first_block, block_count)) {
        return BLOCK_DEVICE_RESULT_OUT_OF_RANGE;
    }

    while (completed < block_count) {
        const uint32_t remaining = block_count - completed;
        const uint32_t chunk =
            (remaining > block_state.stats.max_blocks_per_command)
                ? block_state.stats.max_blocks_per_command : remaining;
        const uint64_t lba = first_block + (uint64_t)completed;
        uint32_t byte_count;
        size_t byte_offset;

        if (!ahci_compute_transfer_bytes(block_state.stats.logical_block_size,
                                         chunk, AHCI_M56_DMA_BYTES,
                                         &byte_count) ||
            ((size_t)completed >
             (SIZE_MAX / (size_t)block_state.stats.logical_block_size))) {
            return BLOCK_DEVICE_RESULT_IO_ERROR;
        }
        byte_offset = (size_t)completed *
                      (size_t)block_state.stats.logical_block_size;
        bytes_zero(block_state.data, (size_t)byte_count);
        if (block_submit(ATA_COMMAND_READ_DMA_EXT, lba, (uint16_t)chunk,
                         byte_count) != AHCI_SUBMIT_OK) {
            return BLOCK_DEVICE_RESULT_IO_ERROR;
        }
        bytes_copy(bytes + byte_offset, block_state.data, (size_t)byte_count);
        completed += chunk;
    }
    return BLOCK_DEVICE_RESULT_OK;
}

static bool rollback(void) {
    bool ok = true;

    if (runtime_state.mmio != NULL) {
        if (runtime_state.ghc_changed) {
            mmio_write32(runtime_state.mmio, AHCI_GHC_OFFSET,
                         runtime_state.original_ghc);
        }
        if (runtime_state.bohc_changed) {
            mmio_write32(runtime_state.mmio, AHCI_BOHC_OFFSET,
                         runtime_state.original_bohc);
        }
        if (!vmm_unmap_mmio_region(runtime_state.mmio,
                                   AHCI_MMIO_WINDOW_SIZE)) {
            ok = false;
        }
    }
    if (runtime_state.pci_command_changed) {
        if (!pci_config_write16(active_state.device.bdf, 0x04U,
                                runtime_state.original_pci_command)) {
            ok = false;
        }
    }
    bytes_zero(&runtime_state, sizeof(runtime_state));
    return ok;
}

bool ahci_init(struct ahci_state *state) {
    struct pci_bar bar;
    volatile void *mapping = NULL;
    const struct boring_pci_inventory *inventory = boring_pci_inventory_get();
    bool ok = false;

    if (state == NULL) {
        return false;
    }
    bytes_zero(state, sizeof(*state));
    bytes_zero(&active_state, sizeof(active_state));
    bytes_zero(&runtime_state, sizeof(runtime_state));
    bytes_zero(&block_state, sizeof(block_state));

    if (!ahci_select_controller(inventory, &active_state.device,
                                &active_state.prog_if) ||
        !pci_get_bar(&active_state.device, AHCI_ABAR_INDEX, &bar) ||
        !ahci_validate_abar(&bar, AHCI_MMIO_WINDOW_SIZE) ||
        !pci_enable_memory(&active_state.device) ||
        !vmm_map_mmio_region(bar.base, AHCI_MMIO_WINDOW_SIZE, &mapping)) {
        goto out;
    }

    runtime_state.mmio = (volatile uint8_t *)mapping;
    active_state.abar_physical = bar.base;
    active_state.cap = mmio_read32(runtime_state.mmio, AHCI_CAP_OFFSET);
    active_state.cap2 = mmio_read32(runtime_state.mmio, AHCI_CAP2_OFFSET);
    active_state.vs = mmio_read32(runtime_state.mmio, AHCI_VS_OFFSET);
    active_state.pi = mmio_read32(runtime_state.mmio, AHCI_PI_OFFSET);

    if ((active_state.vs == 0U) ||
        !bios_handoff(runtime_state.mmio, &active_state) ||
        !enable_ahci_mode(runtime_state.mmio, &active_state) ||
        !inspect_ports(runtime_state.mmio, &active_state)) {
        goto out;
    }

    active_state.initialized = true;
    *state = active_state;
    ok = true;

out:
    if (!ok) {
        (void)rollback();
        bytes_zero(&active_state, sizeof(active_state));
        bytes_zero(state, sizeof(*state));
    }
    return ok;
}

enum ahci_block_result ahci_block_init(void) {
    struct ahci_identify_geometry geometry;
    enum block_device_result registration;
    enum ahci_submit_result submit;
    uint8_t port;
    bool found = false;

    if (block_state.initialized) {
        return AHCI_BLOCK_RESULT_ALREADY_INITIALIZED;
    }
    if (!active_state.initialized || (runtime_state.mmio == NULL)) {
        return AHCI_BLOCK_RESULT_CONTROLLER_NOT_INITIALIZED;
    }

    bytes_zero(&block_state, sizeof(block_state));
    block_state.stats.command_wait_limit = AHCI_COMMAND_WAIT_LIMIT;
    block_state.stats.read_only = true;

    for (port = 0U; port < active_state.inspected_ports; ++port) {
        if (active_state.ports[port].implemented &&
            active_state.ports[port].facts.sata) {
            block_state.stats.port = port;
            block_state.port_base = AHCI_PORT_BASE +
                                    ((uint32_t)port * AHCI_PORT_STRIDE);
            found = true;
            break;
        }
    }
    if (!found) {
        return AHCI_BLOCK_RESULT_NO_SATA_DEVICE;
    }

    if (!block_enable_bus_master()) {
        (void)block_cleanup();
        return AHCI_BLOCK_RESULT_PCI_COMMAND;
    }
    if (!block_allocate_dma()) {
        (void)block_cleanup();
        return ((active_state.cap & AHCI_CAP_S64A) == 0U)
            ? AHCI_BLOCK_RESULT_DMA_ADDRESS
            : AHCI_BLOCK_RESULT_DMA_ALLOCATION;
    }
    if (!block_setup_port()) {
        (void)block_cleanup();
        return AHCI_BLOCK_RESULT_ENGINE_TIMEOUT;
    }

    bytes_zero(block_state.data, AHCI_IDENTIFY_BYTES);
    submit = block_submit(ATA_COMMAND_IDENTIFY_DEVICE, 0ULL, 0U,
                          AHCI_IDENTIFY_BYTES);
    if (submit != AHCI_SUBMIT_OK) {
        (void)block_cleanup();
        return (submit == AHCI_SUBMIT_TIMEOUT)
            ? AHCI_BLOCK_RESULT_COMMAND_TIMEOUT
            : AHCI_BLOCK_RESULT_DEVICE_ERROR;
    }
    if (!ahci_parse_identify((const uint16_t *)(const void *)block_state.data,
                             &geometry)) {
        (void)block_cleanup();
        return AHCI_BLOCK_RESULT_IDENTIFY;
    }
    if (!geometry.lba_supported || !geometry.lba48_supported) {
        (void)block_cleanup();
        return AHCI_BLOCK_RESULT_UNSUPPORTED_DEVICE;
    }
    if ((geometry.logical_block_size == 0U) ||
        (geometry.logical_block_size > AHCI_M56_DMA_BYTES) ||
        ((AHCI_M56_DMA_BYTES / geometry.logical_block_size) == 0U)) {
        (void)block_cleanup();
        return AHCI_BLOCK_RESULT_BAD_GEOMETRY;
    }

    block_state.stats.logical_blocks = geometry.logical_blocks;
    block_state.stats.logical_block_size = geometry.logical_block_size;
    block_state.stats.max_blocks_per_command =
        AHCI_M56_DMA_BYTES / geometry.logical_block_size;
    block_state.stats.lba48 = geometry.lba48_supported;

    block_state.block_device.name = "ahci0";
    block_state.block_device.logical_block_size = geometry.logical_block_size;
    block_state.block_device.block_count = geometry.logical_blocks;
    block_state.block_device.read_only = true;
    block_state.block_device.context = &block_state;
    block_state.block_device.ops = &ahci_block_ops;

    registration = block_device_register(&block_state.block_device);
    if (registration != BLOCK_DEVICE_RESULT_OK) {
        (void)block_cleanup();
        return AHCI_BLOCK_RESULT_REGISTRATION;
    }
    block_state.stats.registered = true;
    block_state.initialized = true;
    return AHCI_BLOCK_RESULT_OK;
}

bool ahci_shutdown(void) {
    const bool block_ok = block_cleanup();
    const bool ok = rollback();
    bytes_zero(&active_state, sizeof(active_state));
    return block_ok && ok;
}

const struct ahci_state *ahci_get_state(void) {
    return &active_state;
}

const struct block_device *ahci_block_device(void) {
    return block_state.initialized ? &block_state.block_device : NULL;
}

bool ahci_block_get_stats(struct ahci_block_stats *stats) {
    if (stats == NULL) {
        return false;
    }
    *stats = block_state.stats;
    return block_state.initialized || (block_state.stats.identify_commands != 0ULL);
}

const char *ahci_block_result_name(enum ahci_block_result result) {
    switch (result) {
        case AHCI_BLOCK_RESULT_OK:
            return "ok";
        case AHCI_BLOCK_RESULT_ALREADY_INITIALIZED:
            return "already-initialized";
        case AHCI_BLOCK_RESULT_CONTROLLER_NOT_INITIALIZED:
            return "controller-not-initialized";
        case AHCI_BLOCK_RESULT_NO_SATA_DEVICE:
            return "no-sata-device";
        case AHCI_BLOCK_RESULT_PCI_COMMAND:
            return "pci-command";
        case AHCI_BLOCK_RESULT_DMA_ALLOCATION:
            return "dma-allocation";
        case AHCI_BLOCK_RESULT_DMA_ADDRESS:
            return "dma-address";
        case AHCI_BLOCK_RESULT_ENGINE_TIMEOUT:
            return "engine-timeout";
        case AHCI_BLOCK_RESULT_COMMAND_TIMEOUT:
            return "command-timeout";
        case AHCI_BLOCK_RESULT_DEVICE_ERROR:
            return "device-error";
        case AHCI_BLOCK_RESULT_IDENTIFY:
            return "identify";
        case AHCI_BLOCK_RESULT_UNSUPPORTED_DEVICE:
            return "unsupported-device";
        case AHCI_BLOCK_RESULT_BAD_GEOMETRY:
            return "bad-geometry";
        case AHCI_BLOCK_RESULT_REGISTRATION:
            return "registration";
        default:
            return "unknown";
    }
}
