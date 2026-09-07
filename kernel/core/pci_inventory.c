#include <boring/pci_inventory.h>

static bool config(struct boring_pci_inventory *info, boring_pci_reader read,
                   void *context, struct pci_bdf bdf, uint16_t offset, uint32_t *value) {
    ++info->config_reads;
    return read(context, bdf, offset, value);
}

bool boring_pci_inventory_collect(struct boring_pci_inventory *info,
                                  boring_pci_reader read, void *context) {
    unsigned char *bytes = (unsigned char *)info;
    for (size_t i = 0U; i < sizeof(*info); ++i) { bytes[i] = 0U; }
    for (uint16_t bus = 0U; bus < 256U; ++bus) {
        for (uint8_t slot = 0U; slot < 32U; ++slot) {
            uint8_t limit = 1U;
            for (uint8_t function = 0U; function < limit; ++function) {
                const struct pci_bdf bdf = {(uint8_t)bus, slot, function};
                uint32_t identity, class_revision, header;
                if (!config(info, read, context, bdf, 0U, &identity)) { return false; }
                if ((identity & 0xffffU) == 0xffffU) { continue; }
                if (!config(info, read, context, bdf, 0x0cU, &header) ||
                    !config(info, read, context, bdf, 0x08U, &class_revision)) { return false; }
                const uint8_t type = (uint8_t)((header >> 16U) & 255U);
                if (function == 0U && (type & 0x80U) != 0U) { limit = 8U; }
                if (info->stored < BORING_PCI_INVENTORY_MAX) {
                    struct boring_pci_entry *e = &info->entries[info->stored++];
                    e->bdf = bdf;
                    e->vendor_id = (uint16_t)(identity & 0xffffU);
                    e->device_id = (uint16_t)(identity >> 16U);
                    e->class_code = (uint8_t)(class_revision >> 24U);
                    e->subclass = (uint8_t)((class_revision >> 16U) & 255U);
                    e->prog_if = (uint8_t)((class_revision >> 8U) & 255U);
                    e->revision = (uint8_t)(class_revision & 255U);
                    e->header_type = type;
                } else {
                    info->truncated = true;
                }
                ++info->total;
            }
        }
    }
    info->complete = true;
    return true;
}
