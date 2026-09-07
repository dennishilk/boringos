#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/io.h>
#include <boring/pci.h>

#define PCI_CONFIG_ADDRESS_PORT 0x0cf8U
#define PCI_CONFIG_DATA_PORT 0x0cfcU
#define PCI_CONFIG_SPACE_SIZE 256U
#define PCI_VENDOR_NONE 0xffffU
#define PCI_HEADER_MULTIFUNCTION 0x80U
#define PCI_STATUS_CAPABILITIES (1U << 4)
#define PCI_COMMAND_MEMORY_SPACE (1U << 1)
#define PCI_COMMAND_BUS_MASTER (1U << 2)
#define PCI_BAR_COUNT 6U
#define PCI_BAR_FIRST_OFFSET 0x10U
#define PCI_BAR_IO_SPACE (1U << 0)
#define PCI_BAR_MEMORY_TYPE_MASK (3U << 1)
#define PCI_BAR_MEMORY_TYPE_32 (0U << 1)
#define PCI_BAR_MEMORY_TYPE_64 (2U << 1)
#define PCI_BAR_PREFETCHABLE (1U << 3)
#define PCI_CAPABILITY_POINTER 0x34U
#define PCI_CAPABILITY_MIN_OFFSET 0x40U
#define PCI_CAPABILITY_MAX_OFFSET 0xfcU

static bool pci_bdf_valid(struct pci_bdf bdf) {
    return (bdf.device < 32U) && (bdf.function < 8U);
}

static bool pci_access_valid(struct pci_bdf bdf,
                             uint16_t offset,
                             uint16_t width) {
    if (!pci_bdf_valid(bdf) || (width == 0U) ||
        (offset >= PCI_CONFIG_SPACE_SIZE) ||
        (width > PCI_CONFIG_SPACE_SIZE) ||
        (offset > (uint16_t)(PCI_CONFIG_SPACE_SIZE - width))) {
        return false;
    }
    return true;
}

static uint32_t pci_address(struct pci_bdf bdf, uint16_t offset) {
    return 0x80000000U |
           ((uint32_t)bdf.bus << 16U) |
           ((uint32_t)bdf.device << 11U) |
           ((uint32_t)bdf.function << 8U) |
           ((uint32_t)offset & 0xfcU);
}

bool pci_config_read8(struct pci_bdf bdf, uint16_t offset, uint8_t *value) {
    uint16_t port;

    if ((value == NULL) || !pci_access_valid(bdf, offset, 1U)) {
        return false;
    }

    x86_64_out32(PCI_CONFIG_ADDRESS_PORT, pci_address(bdf, offset));
    port = (uint16_t)(PCI_CONFIG_DATA_PORT + (offset & 3U));
    *value = x86_64_in8(port);
    return true;
}

bool pci_config_read16(struct pci_bdf bdf, uint16_t offset, uint16_t *value) {
    uint16_t port;

    if ((value == NULL) || ((offset & 1U) != 0U) ||
        !pci_access_valid(bdf, offset, 2U)) {
        return false;
    }

    x86_64_out32(PCI_CONFIG_ADDRESS_PORT, pci_address(bdf, offset));
    port = (uint16_t)(PCI_CONFIG_DATA_PORT + (offset & 2U));
    *value = x86_64_in16(port);
    return true;
}

bool pci_config_read32(struct pci_bdf bdf, uint16_t offset, uint32_t *value) {
    if ((value == NULL) || ((offset & 3U) != 0U) ||
        !pci_access_valid(bdf, offset, 4U)) {
        return false;
    }

    x86_64_out32(PCI_CONFIG_ADDRESS_PORT, pci_address(bdf, offset));
    *value = x86_64_in32(PCI_CONFIG_DATA_PORT);
    return true;
}

bool pci_config_write8(struct pci_bdf bdf, uint16_t offset, uint8_t value) {
    uint16_t port;

    if (!pci_access_valid(bdf, offset, 1U)) {
        return false;
    }

    x86_64_out32(PCI_CONFIG_ADDRESS_PORT, pci_address(bdf, offset));
    port = (uint16_t)(PCI_CONFIG_DATA_PORT + (offset & 3U));
    x86_64_out8(port, value);
    return true;
}

bool pci_config_write16(struct pci_bdf bdf, uint16_t offset, uint16_t value) {
    uint16_t port;

    if (((offset & 1U) != 0U) || !pci_access_valid(bdf, offset, 2U)) {
        return false;
    }

    x86_64_out32(PCI_CONFIG_ADDRESS_PORT, pci_address(bdf, offset));
    port = (uint16_t)(PCI_CONFIG_DATA_PORT + (offset & 2U));
    x86_64_out16(port, value);
    return true;
}

bool pci_config_write32(struct pci_bdf bdf, uint16_t offset, uint32_t value) {
    if (((offset & 3U) != 0U) || !pci_access_valid(bdf, offset, 4U)) {
        return false;
    }

    x86_64_out32(PCI_CONFIG_ADDRESS_PORT, pci_address(bdf, offset));
    x86_64_out32(PCI_CONFIG_DATA_PORT, value);
    return true;
}

static bool pci_read_device(struct pci_bdf bdf, struct pci_device *device) {
    uint16_t vendor;
    uint16_t device_id;
    uint32_t class_revision;
    uint8_t header_type;

    if ((device == NULL) || !pci_config_read16(bdf, 0x00U, &vendor) ||
        (vendor == PCI_VENDOR_NONE) ||
        !pci_config_read16(bdf, 0x02U, &device_id) ||
        !pci_config_read32(bdf, 0x08U, &class_revision) ||
        !pci_config_read8(bdf, 0x0eU, &header_type)) {
        return false;
    }

    device->bdf = bdf;
    device->vendor_id = vendor;
    device->device_id = device_id;
    device->revision = (uint8_t)(class_revision & 0xffU);
    device->subclass = (uint8_t)((class_revision >> 16U) & 0xffU);
    device->class_code = (uint8_t)((class_revision >> 24U) & 0xffU);
    device->header_type = header_type;
    return true;
}

bool pci_find_modern_virtio_block(struct pci_device *device) {
    uint16_t bus;

    if (device == NULL) {
        return false;
    }

    for (bus = 0U; bus <= 255U; ++bus) {
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
                (vendor == PCI_VENDOR_NONE)) {
                continue;
            }

            if (!pci_config_read8(function_zero, 0x0eU, &header_type)) {
                return false;
            }
            if ((header_type & PCI_HEADER_MULTIFUNCTION) != 0U) {
                function_limit = 8U;
            }

            for (function = 0U; function < function_limit; ++function) {
                struct pci_bdf bdf;
                struct pci_device candidate;

                bdf.bus = (uint8_t)bus;
                bdf.device = slot;
                bdf.function = function;

                if (!pci_read_device(bdf, &candidate)) {
                    continue;
                }

                if ((candidate.vendor_id == PCI_VENDOR_VIRTIO) &&
                    (candidate.device_id == PCI_DEVICE_VIRTIO_BLOCK_MODERN)) {
                    *device = candidate;
                    return true;
                }
            }
        }
    }

    return false;
}

bool pci_enable_memory_bus_master(const struct pci_device *device) {
    uint16_t command;
    uint16_t updated;
    uint16_t verify;

    if ((device == NULL) ||
        !pci_config_read16(device->bdf, 0x04U, &command)) {
        return false;
    }

    updated = (uint16_t)(command | PCI_COMMAND_MEMORY_SPACE |
                         PCI_COMMAND_BUS_MASTER);
    if (!pci_config_write16(device->bdf, 0x04U, updated) ||
        !pci_config_read16(device->bdf, 0x04U, &verify)) {
        return false;
    }

    return (verify & (PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER)) ==
           (PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER);
}

bool pci_get_bar(const struct pci_device *device, uint8_t index,
                 struct pci_bar *bar) {
    uint16_t offset;
    uint32_t low;
    uint32_t type;
    uint64_t base;

    if ((device == NULL) || (bar == NULL) || (index >= PCI_BAR_COUNT)) {
        return false;
    }

    offset = (uint16_t)(PCI_BAR_FIRST_OFFSET + ((uint16_t)index * 4U));
    if (!pci_config_read32(device->bdf, offset, &low) ||
        ((low & PCI_BAR_IO_SPACE) != 0U)) {
        return false;
    }

    type = low & PCI_BAR_MEMORY_TYPE_MASK;
    base = (uint64_t)(low & 0xfffffff0U);

    bar->base = 0ULL;
    bar->memory = true;
    bar->is_64bit = false;
    bar->prefetchable = (low & PCI_BAR_PREFETCHABLE) != 0U;

    if (type == PCI_BAR_MEMORY_TYPE_32) {
        if (base == 0ULL) {
            return false;
        }
        bar->base = base;
        return true;
    }

    if (type == PCI_BAR_MEMORY_TYPE_64) {
        uint32_t high;

        if (index >= (PCI_BAR_COUNT - 1U) ||
            !pci_config_read32(device->bdf, (uint16_t)(offset + 4U), &high)) {
            return false;
        }
        base |= ((uint64_t)high << 32U);
        if (base == 0ULL) {
            return false;
        }
        bar->base = base;
        bar->is_64bit = true;
        return true;
    }

    return false;
}

static bool pci_capability_pointer_valid(uint8_t pointer) {
    return (pointer >= PCI_CAPABILITY_MIN_OFFSET) &&
           (pointer <= PCI_CAPABILITY_MAX_OFFSET) &&
           ((pointer & 3U) == 0U);
}

bool pci_list_capabilities(const struct pci_device *device,
                           uint8_t *offsets,
                           size_t offsets_capacity,
                           size_t *offset_count) {
    bool visited[64] = { false };
    uint16_t status;
    uint8_t pointer;
    size_t count = 0U;

    if ((device == NULL) || (offsets == NULL) || (offset_count == NULL) ||
        (offsets_capacity == 0U) ||
        (offsets_capacity > (size_t)PCI_MAX_CAPABILITIES)) {
        return false;
    }

    *offset_count = 0U;

    if (!pci_config_read16(device->bdf, 0x06U, &status) ||
        ((status & PCI_STATUS_CAPABILITIES) == 0U) ||
        !pci_config_read8(device->bdf, PCI_CAPABILITY_POINTER, &pointer)) {
        return false;
    }

    while (pointer != 0U) {
        uint8_t next;
        const size_t visited_index = (size_t)(pointer >> 2U);

        if (!pci_capability_pointer_valid(pointer) ||
            (visited_index >= (sizeof(visited) / sizeof(visited[0]))) ||
            visited[visited_index] || (count >= offsets_capacity) ||
            (count >= (size_t)PCI_MAX_CAPABILITIES)) {
            return false;
        }

        visited[visited_index] = true;
        offsets[count] = pointer;
        ++count;

        if (!pci_config_read8(device->bdf, (uint16_t)(pointer + 1U), &next)) {
            return false;
        }
        if ((next != 0U) && !pci_capability_pointer_valid(next)) {
            return false;
        }
        pointer = next;
    }

    *offset_count = count;
    return count != 0U;
}
