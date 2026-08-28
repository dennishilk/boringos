#include <boring/pci_inventory.h>
#include <boring/serial.h>

static struct boring_pci_inventory inventory;
static bool native_read(void *context, struct pci_bdf bdf, uint16_t offset, uint32_t *value) {
    (void)context;
    return pci_config_read32(bdf, offset, value);
}
static void hex(uint32_t value, unsigned digits) {
    static const char alphabet[] = "0123456789ABCDEF";
    char text[5] = {0};
    for (unsigned i = 0U; i < digits; ++i) {
        text[i] = alphabet[(value >> ((digits - 1U - i) * 4U)) & 15U];
    }
    serial_write_string(text);
}

void boring_pci_inventory_init(void) {
    const bool complete = boring_pci_inventory_collect(&inventory, native_read, (void *)0);
    for (uint32_t i = 0U; i < inventory.stored; ++i) {
        const struct boring_pci_entry *e = &inventory.entries[i];
        serial_write_string("pci-inventory: ");
        hex(e->bdf.bus, 2U); serial_write_string(":");
        hex(e->bdf.device, 2U); serial_write_string("."); hex(e->bdf.function, 1U);
        serial_write_string(" id="); hex(e->vendor_id, 4U); serial_write_string(":"); hex(e->device_id, 4U);
        serial_write_string(" class="); hex(e->class_code, 2U); serial_write_string(":"); hex(e->subclass, 2U);
        serial_write_string(" prog_if="); hex(e->prog_if, 2U);
        serial_write_string(" revision="); hex(e->revision, 2U);
        serial_write_string(" header="); hex(e->header_type, 2U); serial_write_string("\n");
    }
    serial_write_string("pci-inventory: stored="); serial_write_u64(inventory.stored);
    serial_write_string(" total="); serial_write_u64(inventory.total);
    serial_write_string(" config_reads="); serial_write_u64(inventory.config_reads);
    serial_write_string(" truncated="); serial_write_u64(inventory.truncated ? 1U : 0U);
    serial_write_string(" complete="); serial_write_u64(complete ? 1U : 0U);
    serial_write_string("\n");
}

const struct boring_pci_inventory *boring_pci_inventory_get(void) { return &inventory; }
