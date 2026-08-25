#ifndef BORING_PCI_H
#define BORING_PCI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PCI_MAX_CAPABILITIES 48U
#define PCI_VENDOR_VIRTIO 0x1af4U
#define PCI_DEVICE_VIRTIO_BLOCK_MODERN 0x1042U

struct pci_bdf {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
};

struct pci_device {
    struct pci_bdf bdf;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t header_type;
    uint8_t revision;
};

struct pci_bar {
    uint64_t base;
    bool memory;
    bool is_64bit;
    bool prefetchable;
};

bool pci_config_read8(struct pci_bdf bdf, uint16_t offset, uint8_t *value);
bool pci_config_read16(struct pci_bdf bdf, uint16_t offset, uint16_t *value);
bool pci_config_read32(struct pci_bdf bdf, uint16_t offset, uint32_t *value);
bool pci_config_write8(struct pci_bdf bdf, uint16_t offset, uint8_t value);
bool pci_config_write16(struct pci_bdf bdf, uint16_t offset, uint16_t value);
bool pci_config_write32(struct pci_bdf bdf, uint16_t offset, uint32_t value);
bool pci_find_modern_virtio_block(struct pci_device *device);
bool pci_enable_memory_bus_master(const struct pci_device *device);
bool pci_get_bar(const struct pci_device *device, uint8_t index,
                 struct pci_bar *bar);
bool pci_list_capabilities(const struct pci_device *device,
                           uint8_t *offsets,
                           size_t offsets_capacity,
                           size_t *offset_count);

#endif
