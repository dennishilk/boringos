#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/cpu.h>
#include <boring/e1000.h>
#include <boring/io.h>
#include <boring/pci.h>
#include <boring/pmm.h>
#include <boring/vmm.h>

#define E1000_VENDOR_INTEL 0x8086U
#define E1000_DEVICE_82540EM 0x100eU
#define E1000_DEVICE_82545EM 0x100fU
#define E1000_DEVICE_82546EB 0x1010U
#define E1000_DEVICE_82541PI 0x107cU
#define E1000_DEVICE_82574L 0x10d3U

#define E1000_MMIO_BYTES ((size_t)0x20000U)
#define E1000_DMA_LIMIT 0x100000000ULL
#define E1000_RING_SIZE 8U
#define E1000_RX_BUFFER_SIZE 2048U
#define E1000_FRAME_MAX 1518U
#define E1000_DMA_FRAME_COUNT 7U
#define E1000_DMA_RX_RING 0U
#define E1000_DMA_TX_RING 1U
#define E1000_DMA_RX_FIRST 2U
#define E1000_DMA_RX_PAGES 4U
#define E1000_DMA_TX_BUFFER 6U
#define E1000_RESET_SPINS 1000000U
#define E1000_TX_SPINS 10000000U

#define E1000_REG_CTRL 0x0000U
#define E1000_REG_STATUS 0x0008U
#define E1000_REG_ICR 0x00c0U
#define E1000_REG_IMC 0x00d8U
#define E1000_REG_RCTL 0x0100U
#define E1000_REG_TCTL 0x0400U
#define E1000_REG_TIPG 0x0410U
#define E1000_REG_RDBAL 0x2800U
#define E1000_REG_RDBAH 0x2804U
#define E1000_REG_RDLEN 0x2808U
#define E1000_REG_RDH 0x2810U
#define E1000_REG_RDT 0x2818U
#define E1000_REG_TDBAL 0x3800U
#define E1000_REG_TDBAH 0x3804U
#define E1000_REG_TDLEN 0x3808U
#define E1000_REG_TDH 0x3810U
#define E1000_REG_TDT 0x3818U
#define E1000_REG_RAL 0x5400U
#define E1000_REG_RAH 0x5404U

#define E1000_CTRL_SLU (1U << 6)
#define E1000_CTRL_RST (1U << 26)
#define E1000_STATUS_LU (1U << 1)
#define E1000_RCTL_EN (1U << 1)
#define E1000_RCTL_BAM (1U << 15)
#define E1000_RCTL_SECRC (1U << 26)
#define E1000_TCTL_EN (1U << 1)
#define E1000_TCTL_PSP (1U << 3)
#define E1000_TCTL_CT (15U << 4)
#define E1000_TCTL_COLD (64U << 12)
#define E1000_TCTL_RTLC (1U << 24)
#define E1000_TX_CMD_EOP (1U << 0)
#define E1000_TX_CMD_IFCS (1U << 1)
#define E1000_TX_CMD_RS (1U << 3)
#define E1000_DESC_DD (1U << 0)
#define E1000_DESC_EOP (1U << 1)
#define E1000_RAH_AV (1U << 31)

struct e1000_rx_desc {
    uint64_t address;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
};

struct e1000_tx_desc {
    uint64_t address;
    uint16_t length;
    uint8_t checksum_offset;
    uint8_t command;
    uint8_t status;
    uint8_t checksum_start;
    uint16_t special;
};

_Static_assert(sizeof(struct e1000_rx_desc) == 16U,
               "e1000 RX descriptor size changed");
_Static_assert(sizeof(struct e1000_tx_desc) == 16U,
               "e1000 TX descriptor size changed");
_Static_assert((E1000_RING_SIZE * sizeof(struct e1000_rx_desc)) == 128U,
               "e1000 RX ring length must remain register-valid");
_Static_assert((E1000_RING_SIZE * sizeof(struct e1000_tx_desc)) == 128U,
               "e1000 TX ring length must remain register-valid");

struct e1000_runtime {
    volatile uint8_t *mmio;
    uint64_t dma_physical[E1000_DMA_FRAME_COUNT];
    void *dma_virtual[E1000_DMA_FRAME_COUNT];
    volatile struct e1000_rx_desc *rx_ring;
    volatile struct e1000_tx_desc *tx_ring;
    uint8_t *tx_buffer;
    struct e1000_info info;
    uint32_t rx_next;
    uint32_t tx_next;
};

static struct e1000_runtime runtime;

static void bytes_zero(void *buffer, size_t length) {
    uint8_t *bytes = (uint8_t *)buffer;
    size_t index;

    for (index = 0U; index < length; ++index) {
        bytes[index] = 0U;
    }
}

static void bytes_copy(void *destination, const void *source, size_t length) {
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    size_t index;

    for (index = 0U; index < length; ++index) {
        out[index] = in[index];
    }
}

static uint32_t mmio_read32(uint32_t offset) {
    return *(volatile uint32_t *)(volatile void *)(runtime.mmio + offset);
}

static void mmio_write32(uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(volatile void *)(runtime.mmio + offset) = value;
}

static bool device_supported(uint16_t vendor, uint16_t device) {
    if (vendor != E1000_VENDOR_INTEL) {
        return false;
    }
    return (device == E1000_DEVICE_82540EM) ||
           (device == E1000_DEVICE_82545EM) ||
           (device == E1000_DEVICE_82546EB) ||
           (device == E1000_DEVICE_82541PI) ||
           (device == E1000_DEVICE_82574L);
}

static bool find_device(struct pci_device *selected) {
    uint16_t bus;

    if (selected == NULL) {
        return false;
    }
    for (bus = 0U; bus < 256U; ++bus) {
        uint8_t slot;
        for (slot = 0U; slot < 32U; ++slot) {
            struct pci_bdf function_zero;
            uint16_t vendor;
            uint8_t header_type;
            uint8_t function_limit = 1U;
            uint8_t function;

            function_zero.bus = (uint8_t)bus;
            function_zero.device = slot;
            function_zero.function = 0U;
            if (!pci_config_read16(function_zero, 0x00U, &vendor) ||
                (vendor == 0xffffU)) {
                continue;
            }
            if (!pci_config_read8(function_zero, 0x0eU, &header_type)) {
                continue;
            }
            if ((header_type & 0x80U) != 0U) {
                function_limit = 8U;
            }
            for (function = 0U; function < function_limit; ++function) {
                struct pci_bdf bdf;
                uint16_t current_vendor;
                uint16_t device_id;
                uint32_t class_revision;
                uint8_t current_header;
                uint8_t class_code;
                uint8_t subclass;

                bdf.bus = (uint8_t)bus;
                bdf.device = slot;
                bdf.function = function;
                if (!pci_config_read16(bdf, 0x00U, &current_vendor) ||
                    (current_vendor == 0xffffU) ||
                    !pci_config_read16(bdf, 0x02U, &device_id) ||
                    !pci_config_read32(bdf, 0x08U, &class_revision) ||
                    !pci_config_read8(bdf, 0x0eU, &current_header)) {
                    continue;
                }
                class_code = (uint8_t)(class_revision >> 24U);
                subclass = (uint8_t)((class_revision >> 16U) & 0xffU);
                if ((class_code != 0x02U) || (subclass != 0x00U)) {
                    continue;
                }
                if (!runtime.info.ethernet_seen) {
                    runtime.info.ethernet_seen = true;
                    runtime.info.vendor_id = current_vendor;
                    runtime.info.device_id = device_id;
                    runtime.info.bus = bdf.bus;
                    runtime.info.device = bdf.device;
                    runtime.info.function = bdf.function;
                }
                if (!device_supported(current_vendor, device_id)) {
                    continue;
                }
                selected->bdf = bdf;
                selected->vendor_id = current_vendor;
                selected->device_id = device_id;
                selected->class_code = class_code;
                selected->subclass = subclass;
                selected->revision = (uint8_t)(class_revision & 0xffU);
                selected->header_type = current_header;
                runtime.info.vendor_id = current_vendor;
                runtime.info.device_id = device_id;
                runtime.info.bus = bdf.bus;
                runtime.info.device = bdf.device;
                runtime.info.function = bdf.function;
                return true;
            }
        }
    }
    return false;
}

static void free_dma(void) {
    size_t index;

    for (index = 0U; index < (size_t)E1000_DMA_FRAME_COUNT; ++index) {
        if (runtime.dma_physical[index] != 0ULL) {
            (void)pmm_free_frame(runtime.dma_physical[index]);
            runtime.dma_physical[index] = 0ULL;
            runtime.dma_virtual[index] = NULL;
        }
    }
}

static bool allocate_dma(void) {
    size_t index;

    for (index = 0U; index < (size_t)E1000_DMA_FRAME_COUNT; ++index) {
        uint64_t physical = 0ULL;
        void *virtual_address = NULL;

        if (!pmm_alloc_frame_in_range(0ULL, E1000_DMA_LIMIT, &physical) ||
            !vmm_pmm_frame_to_hhdm(physical, &virtual_address)) {
            if (physical != 0ULL) {
                (void)pmm_free_frame(physical);
            }
            free_dma();
            return false;
        }
        runtime.dma_physical[index] = physical;
        runtime.dma_virtual[index] = virtual_address;
        bytes_zero(virtual_address, (size_t)PMM_PAGE_SIZE);
    }
    runtime.rx_ring = (volatile struct e1000_rx_desc *)
        runtime.dma_virtual[E1000_DMA_RX_RING];
    runtime.tx_ring = (volatile struct e1000_tx_desc *)
        runtime.dma_virtual[E1000_DMA_TX_RING];
    runtime.tx_buffer = (uint8_t *)runtime.dma_virtual[E1000_DMA_TX_BUFFER];
    return true;
}

static bool read_mac(void) {
    const uint32_t low = mmio_read32(E1000_REG_RAL);
    const uint32_t high = mmio_read32(E1000_REG_RAH);
    bool nonzero = false;
    bool all_ff = true;

    if ((high & E1000_RAH_AV) == 0U) {
        return false;
    }
    size_t index;

    runtime.info.mac[0] = (uint8_t)(low & 0xffU);
    runtime.info.mac[1] = (uint8_t)((low >> 8U) & 0xffU);
    runtime.info.mac[2] = (uint8_t)((low >> 16U) & 0xffU);
    runtime.info.mac[3] = (uint8_t)((low >> 24U) & 0xffU);
    runtime.info.mac[4] = (uint8_t)(high & 0xffU);
    runtime.info.mac[5] = (uint8_t)((high >> 8U) & 0xffU);
    for (index = 0U; index < 6U; ++index) {
        if (runtime.info.mac[index] != 0U) {
            nonzero = true;
        }
        if (runtime.info.mac[index] != 0xffU) {
            all_ff = false;
        }
    }
    return nonzero && !all_ff && ((runtime.info.mac[0] & 1U) == 0U);
}

static bool reset_controller(void) {
    uint32_t spin;
    uint32_t ctrl = mmio_read32(E1000_REG_CTRL);

    mmio_write32(E1000_REG_IMC, 0xffffffffU);
    (void)mmio_read32(E1000_REG_ICR);
    mmio_write32(E1000_REG_CTRL, ctrl | E1000_CTRL_RST);
    for (spin = 0U; spin < E1000_RESET_SPINS; ++spin) {
        if ((mmio_read32(E1000_REG_CTRL) & E1000_CTRL_RST) == 0U) {
            mmio_write32(E1000_REG_IMC, 0xffffffffU);
            (void)mmio_read32(E1000_REG_ICR);
            return true;
        }
        x86_64_pause();
    }
    return false;
}

static bool setup_rings(void) {
    uint32_t index;

    for (index = 0U; index < E1000_RING_SIZE; ++index) {
        const uint32_t page = index / 2U;
        const uint32_t half = index % 2U;
        const uint64_t physical =
            runtime.dma_physical[E1000_DMA_RX_FIRST + page] +
            ((uint64_t)half * (uint64_t)E1000_RX_BUFFER_SIZE);

        runtime.rx_ring[index].address = physical;
        runtime.rx_ring[index].length = 0U;
        runtime.rx_ring[index].checksum = 0U;
        runtime.rx_ring[index].status = 0U;
        runtime.rx_ring[index].errors = 0U;
        runtime.rx_ring[index].special = 0U;

        runtime.tx_ring[index].address =
            runtime.dma_physical[E1000_DMA_TX_BUFFER];
        runtime.tx_ring[index].length = 0U;
        runtime.tx_ring[index].checksum_offset = 0U;
        runtime.tx_ring[index].command = 0U;
        runtime.tx_ring[index].status = E1000_DESC_DD;
        runtime.tx_ring[index].checksum_start = 0U;
        runtime.tx_ring[index].special = 0U;
    }
    x86_64_memory_barrier();

    mmio_write32(E1000_REG_RDBAL,
        (uint32_t)(runtime.dma_physical[E1000_DMA_RX_RING] & 0xffffffffULL));
    mmio_write32(E1000_REG_RDBAH,
        (uint32_t)(runtime.dma_physical[E1000_DMA_RX_RING] >> 32U));
    mmio_write32(E1000_REG_RDLEN,
        (uint32_t)(E1000_RING_SIZE * sizeof(struct e1000_rx_desc)));
    mmio_write32(E1000_REG_RDH, 0U);
    mmio_write32(E1000_REG_RDT, E1000_RING_SIZE - 1U);

    mmio_write32(E1000_REG_TDBAL,
        (uint32_t)(runtime.dma_physical[E1000_DMA_TX_RING] & 0xffffffffULL));
    mmio_write32(E1000_REG_TDBAH,
        (uint32_t)(runtime.dma_physical[E1000_DMA_TX_RING] >> 32U));
    mmio_write32(E1000_REG_TDLEN,
        (uint32_t)(E1000_RING_SIZE * sizeof(struct e1000_tx_desc)));
    mmio_write32(E1000_REG_TDH, 0U);
    mmio_write32(E1000_REG_TDT, 0U);

    mmio_write32(E1000_REG_TIPG, 10U | (8U << 10U) | (6U << 20U));
    mmio_write32(E1000_REG_TCTL,
        E1000_TCTL_EN | E1000_TCTL_PSP | E1000_TCTL_CT |
        E1000_TCTL_COLD | E1000_TCTL_RTLC);
    mmio_write32(E1000_REG_RCTL,
        E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC);
    runtime.rx_next = 0U;
    runtime.tx_next = 0U;
    x86_64_memory_barrier();
    return true;
}

enum e1000_result e1000_init(void) {
    struct pci_device device;
    struct pci_bar bar;
    volatile void *mapping = NULL;
    enum e1000_result failure;

    if (runtime.info.initialized) {
        runtime.info.link_up =
            (mmio_read32(E1000_REG_STATUS) & E1000_STATUS_LU) != 0U;
        return E1000_RESULT_OK;
    }
    bytes_zero(&runtime, sizeof(runtime));
    if (!find_device(&device)) {
        return runtime.info.ethernet_seen ?
            E1000_RESULT_UNSUPPORTED_DEVICE : E1000_RESULT_NO_DEVICE;
    }
    if (!pci_enable_memory_bus_master(&device)) {
        return E1000_RESULT_PCI_COMMAND;
    }
    if (!pci_get_bar(&device, 0U, &bar) || !bar.memory || (bar.base == 0ULL)) {
        return E1000_RESULT_BAR;
    }
    if (!vmm_map_mmio_region(bar.base, E1000_MMIO_BYTES, &mapping)) {
        return E1000_RESULT_MMIO;
    }
    runtime.mmio = (volatile uint8_t *)mapping;
    failure = E1000_RESULT_RESET;
    if (!reset_controller()) {
        goto fail;
    }
    failure = E1000_RESULT_MAC;
    if (!read_mac()) {
        goto fail;
    }
    failure = E1000_RESULT_DMA;
    if (!allocate_dma()) {
        goto fail;
    }
    failure = E1000_RESULT_SETUP;
    if (!setup_rings()) {
        goto fail;
    }
    mmio_write32(E1000_REG_CTRL,
                 mmio_read32(E1000_REG_CTRL) | E1000_CTRL_SLU);
    runtime.info.link_up =
        (mmio_read32(E1000_REG_STATUS) & E1000_STATUS_LU) != 0U;
    runtime.info.initialized = true;
    return E1000_RESULT_OK;

fail:
    free_dma();
    if (runtime.mmio != NULL) {
        (void)vmm_unmap_mmio_region(runtime.mmio, E1000_MMIO_BYTES);
        runtime.mmio = NULL;
    }
    return failure;
}

bool e1000_send(const void *frame, size_t length) {
    volatile struct e1000_tx_desc *descriptor;
    uint32_t spin;
    uint32_t index;

    if (!runtime.info.initialized || (frame == NULL) ||
        (length < 14U) || (length > E1000_FRAME_MAX)) {
        return false;
    }
    index = runtime.tx_next;
    descriptor = &runtime.tx_ring[index];
    for (spin = 0U; spin < E1000_TX_SPINS; ++spin) {
        if ((descriptor->status & E1000_DESC_DD) != 0U) {
            break;
        }
        x86_64_pause();
    }
    if ((descriptor->status & E1000_DESC_DD) == 0U) {
        return false;
    }
    bytes_copy(runtime.tx_buffer, frame, length);
    descriptor->address = runtime.dma_physical[E1000_DMA_TX_BUFFER];
    descriptor->length = (uint16_t)length;
    descriptor->checksum_offset = 0U;
    descriptor->command =
        E1000_TX_CMD_EOP | E1000_TX_CMD_IFCS | E1000_TX_CMD_RS;
    descriptor->status = 0U;
    descriptor->checksum_start = 0U;
    descriptor->special = 0U;
    x86_64_memory_barrier();
    runtime.tx_next = (index + 1U) % E1000_RING_SIZE;
    mmio_write32(E1000_REG_TDT, runtime.tx_next);

    for (spin = 0U; spin < E1000_TX_SPINS; ++spin) {
        if ((descriptor->status & E1000_DESC_DD) != 0U) {
            return true;
        }
        x86_64_pause();
    }
    return false;
}

bool e1000_receive(void *frame, size_t capacity, size_t *length) {
    volatile struct e1000_rx_desc *descriptor;
    uint32_t index;
    size_t received;
    uint32_t page;
    uint32_t half;
    const uint8_t *source;

    if ((length == NULL) || !runtime.info.initialized) {
        return false;
    }
    *length = 0U;
    index = runtime.rx_next;
    descriptor = &runtime.rx_ring[index];
    if ((descriptor->status & E1000_DESC_DD) == 0U) {
        return true;
    }
    x86_64_memory_barrier();
    received = (size_t)descriptor->length;
    page = index / 2U;
    half = index % 2U;
    source = (const uint8_t *)
        runtime.dma_virtual[E1000_DMA_RX_FIRST + page] +
        ((size_t)half * (size_t)E1000_RX_BUFFER_SIZE);
    if (((descriptor->status & (E1000_DESC_DD | E1000_DESC_EOP)) ==
         (E1000_DESC_DD | E1000_DESC_EOP)) &&
        (descriptor->errors == 0U) && (received >= 14U) &&
        (received <= E1000_FRAME_MAX) && (received <= capacity) &&
        (frame != NULL)) {
        bytes_copy(frame, source, received);
        *length = received;
    }
    descriptor->length = 0U;
    descriptor->checksum = 0U;
    descriptor->status = 0U;
    descriptor->errors = 0U;
    descriptor->special = 0U;
    x86_64_memory_barrier();
    mmio_write32(E1000_REG_RDT, index);
    runtime.rx_next = (index + 1U) % E1000_RING_SIZE;
    return true;
}

bool e1000_get_info(struct e1000_info *info) {
    if (info == NULL) {
        return false;
    }
    if (runtime.info.initialized && (runtime.mmio != NULL)) {
        runtime.info.link_up =
            (mmio_read32(E1000_REG_STATUS) & E1000_STATUS_LU) != 0U;
    }
    *info = runtime.info;
    return true;
}
