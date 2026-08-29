#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/ahci.h>

bool ahci_select_controller(const struct boring_pci_inventory *inventory,
                            struct pci_device *device, uint8_t *prog_if) {
    const struct boring_pci_entry *match = NULL;
    uint32_t index;
    uint32_t matches = 0U;

    if ((inventory == NULL) || (device == NULL) || (prog_if == NULL) ||
        !inventory->complete || inventory->truncated ||
        (inventory->stored > BORING_PCI_INVENTORY_MAX)) {
        return false;
    }

    for (index = 0U; index < inventory->stored; ++index) {
        const struct boring_pci_entry *entry = &inventory->entries[index];
        if ((entry->class_code == AHCI_PCI_CLASS_MASS_STORAGE) &&
            (entry->subclass == AHCI_PCI_SUBCLASS_SATA) &&
            (entry->prog_if == AHCI_PCI_PROG_IF)) {
            match = entry;
            ++matches;
        }
    }
    if ((matches != 1U) || (match == NULL)) {
        return false;
    }

    device->bdf = match->bdf;
    device->vendor_id = match->vendor_id;
    device->device_id = match->device_id;
    device->class_code = match->class_code;
    device->subclass = match->subclass;
    device->header_type = match->header_type;
    device->revision = match->revision;
    *prog_if = match->prog_if;
    return true;
}

bool ahci_validate_abar(const struct pci_bar *bar, size_t length) {
    uint64_t end;

    if ((bar == NULL) || !bar->memory || bar->is_64bit ||
        bar->prefetchable || (bar->base == 0ULL) ||
        ((bar->base & 0x3ffULL) != 0ULL) ||
        (length < (size_t)AHCI_MMIO_WINDOW_SIZE)) {
        return false;
    }
    if ((uint64_t)(length - 1U) > (UINT64_MAX - bar->base)) {
        return false;
    }
    end = bar->base + (uint64_t)(length - 1U);
    return end >= bar->base;
}

bool ahci_bounded_ports(uint32_t cap, uint32_t pi,
                        uint8_t *hardware_ports,
                        uint8_t *inspected_ports,
                        uint32_t *implemented_mask,
                        bool *truncated) {
    const uint8_t hardware = (uint8_t)((cap & 0x1fU) + 1U);
    const uint8_t inspected = (hardware > AHCI_BORING_MAX_PORTS)
        ? (uint8_t)AHCI_BORING_MAX_PORTS : hardware;
    const uint32_t mask = (inspected == 32U)
        ? UINT32_MAX : ((1U << inspected) - 1U);

    if ((hardware_ports == NULL) || (inspected_ports == NULL) ||
        (implemented_mask == NULL) || (truncated == NULL) ||
        (hardware == 0U) || (hardware > 32U) || (inspected == 0U)) {
        return false;
    }

    *hardware_ports = hardware;
    *inspected_ports = inspected;
    *implemented_mask = pi & mask;
    *truncated = (hardware > AHCI_BORING_MAX_PORTS) || ((pi & ~mask) != 0U);
    return true;
}

static bool det_valid(uint8_t det) {
    return (det == 0U) || (det == 1U) || (det == 3U) || (det == 4U);
}

static bool ipm_valid(uint8_t ipm) {
    return (ipm == 0U) || (ipm == 1U) || (ipm == 2U) ||
           (ipm == 6U) || (ipm == 8U);
}

bool ahci_decode_port_status(uint32_t ssts, uint32_t sig,
                             struct ahci_port_facts *facts) {
    const uint8_t det = (uint8_t)(ssts & 0x0fU);
    const uint8_t ipm = (uint8_t)((ssts >> 8U) & 0x0fU);

    if ((facts == NULL) || !det_valid(det) || !ipm_valid(ipm)) {
        return false;
    }

    facts->det = det;
    facts->ipm = ipm;
    facts->present = (det == 3U) && (ipm != 0U);
    facts->sata = facts->present && (sig == AHCI_SIGNATURE_ATA);
    return true;
}

bool ahci_wait_mask_bounded(ahci_poll_reader read, void *context,
                            uint32_t offset, uint32_t mask, bool want_set,
                            uint32_t limit, uint32_t *last_value) {
    uint32_t attempt;
    uint32_t value = 0U;

    if ((read == NULL) || (last_value == NULL) || (mask == 0U) ||
        (limit == 0U)) {
        return false;
    }

    for (attempt = 0U; attempt < limit; ++attempt) {
        if (!read(context, offset, &value)) {
            *last_value = value;
            return false;
        }
        if (((value & mask) != 0U) == want_set) {
            *last_value = value;
            return true;
        }
    }
    *last_value = value;
    return false;
}

bool ahci_parse_identify(const uint16_t words[AHCI_IDENTIFY_WORDS],
                         struct ahci_identify_geometry *geometry) {
    uint64_t logical_blocks;
    uint32_t logical_block_size = 512U;
    bool lba_supported;
    bool lba48_supported;

    if ((words == NULL) || (geometry == NULL)) {
        return false;
    }

    lba_supported = (words[49] & (1U << 9U)) != 0U;
    lba48_supported = ((words[83] & 0xc000U) == 0x4000U) &&
                      ((words[83] & (1U << 10U)) != 0U);

    if (lba48_supported) {
        logical_blocks = (uint64_t)words[100] |
                         ((uint64_t)words[101] << 16U) |
                         ((uint64_t)words[102] << 32U) |
                         ((uint64_t)words[103] << 48U);
    } else if (lba_supported) {
        logical_blocks = (uint64_t)words[60] |
                         ((uint64_t)words[61] << 16U);
    } else {
        return false;
    }

    if (((words[106] & 0xc000U) == 0x4000U) &&
        ((words[106] & (1U << 12U)) != 0U)) {
        const uint32_t logical_words = (uint32_t)words[117] |
                                       ((uint32_t)words[118] << 16U);
        if ((logical_words == 0U) || (logical_words > (UINT32_MAX / 2U))) {
            return false;
        }
        logical_block_size = logical_words * 2U;
    }

    if ((logical_blocks == 0ULL) || (logical_block_size == 0U) ||
        (logical_blocks > (UINT64_MAX / (uint64_t)logical_block_size))) {
        return false;
    }

    geometry->logical_blocks = logical_blocks;
    geometry->logical_block_size = logical_block_size;
    geometry->lba_supported = lba_supported || lba48_supported;
    geometry->lba48_supported = lba48_supported;
    return true;
}

bool ahci_lba_range_valid(uint64_t logical_blocks, uint64_t first_block,
                          uint32_t block_count) {
    if ((logical_blocks == 0ULL) || (block_count == 0U) ||
        (first_block >= logical_blocks)) {
        return false;
    }
    return (uint64_t)block_count <= (logical_blocks - first_block);
}

bool ahci_compute_transfer_bytes(uint32_t logical_block_size,
                                 uint32_t block_count,
                                 uint32_t transfer_limit,
                                 uint32_t *byte_count) {
    uint32_t bytes;

    if ((byte_count == NULL) || (logical_block_size == 0U) ||
        (block_count == 0U) || (transfer_limit == 0U) ||
        (block_count > (UINT32_MAX / logical_block_size))) {
        return false;
    }

    bytes = logical_block_size * block_count;
    if ((bytes == 0U) || (bytes > transfer_limit)) {
        return false;
    }
    *byte_count = bytes;
    return true;
}
