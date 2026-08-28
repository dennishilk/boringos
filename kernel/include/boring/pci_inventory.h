#ifndef BORING_PCI_INVENTORY_H
#define BORING_PCI_INVENTORY_H
#include <boring/pci.h>

#define BORING_PCI_INVENTORY_MAX 256U
struct boring_pci_entry {
    struct pci_bdf bdf;
    uint16_t vendor_id, device_id;
    uint8_t class_code, subclass, prog_if, revision, header_type;
};
struct boring_pci_inventory {
    struct boring_pci_entry entries[BORING_PCI_INVENTORY_MAX];
    uint32_t stored, total, config_reads;
    bool complete, truncated;
};
typedef bool (*boring_pci_reader)(void *context, struct pci_bdf bdf,
                                 uint16_t offset, uint32_t *value);

/* One PCI segment, all 256 buses/32 slots, up to eight functions when fn0
 * advertises multifunction. Read-only config enumeration; no BAR probing,
 * bus numbering, hotplug, driver binding or PCIe extended-space access.
 * Non-null caller-owned output/query required. Partial/error state is explicit.
 */
bool boring_pci_inventory_collect(struct boring_pci_inventory *info,
                                  boring_pci_reader read, void *context);
void boring_pci_inventory_init(void);
const struct boring_pci_inventory *boring_pci_inventory_get(void);

#endif
