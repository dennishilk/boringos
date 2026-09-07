#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <boring/pci_inventory.h>

struct fixture { unsigned calls, fail_at; bool dense, empty; };
static bool read_config(void *context, struct pci_bdf bdf, uint16_t offset, uint32_t *out) {
    struct fixture *f = context;
    assert(bdf.device < 32U && bdf.function < 8U);
    assert(offset == 0U || offset == 8U || offset == 12U);
    if (++f->calls == f->fail_at) { return false; }
    if (!f->dense && !f->empty) {
        /* Fn1 on a single-function slot must never be touched. */
        assert(!(bdf.bus == 0U && bdf.device == 1U && bdf.function != 0U));
    }
    const bool present = !f->empty && (f->dense ||
        (bdf.bus == 0U && bdf.device == 1U && bdf.function == 0U) ||
        (bdf.bus == 0U && bdf.device == 2U && (bdf.function == 0U || bdf.function == 3U || bdf.function == 7U)) ||
        (bdf.bus == 255U && bdf.device == 31U && (bdf.function == 0U || bdf.function == 7U)));
    if (!present) { assert(offset == 0U); *out = 0xffffffffU; return true; }
    if (offset == 0U) { *out = 0x10421AF4U; }
    if (offset == 8U) { *out = 0x01020304U; }
    if (offset == 12U) { *out = (f->dense || bdf.device != 1U) ? 0x00800000U : 0U; }
    return true;
}

int main(void) {
    static struct { uint64_t before; struct boring_pci_inventory info; uint64_t after; } guarded;
    struct fixture f = {0};
    memset(&guarded, 0xA5, sizeof(guarded));
    struct boring_pci_inventory *i = &guarded.info;
    assert(boring_pci_inventory_collect(i, read_config, &f));
    assert(i->complete && !i->truncated && i->stored == 6U && i->total == 6U);
    assert(i->config_reads == f.calls && f.calls == 8218U);
    assert(i->entries[0].bdf.device == 1U && i->entries[0].header_type == 0U);
    assert(i->entries[2].bdf.function == 3U && i->entries[3].bdf.function == 7U);
    assert(i->entries[5].bdf.bus == 255U && i->entries[5].bdf.device == 31U && i->entries[5].bdf.function == 7U);
    assert(i->entries[2].vendor_id == 0x1AF4U && i->entries[2].device_id == 0x1042U);
    assert(i->entries[2].class_code == 1U && i->entries[2].subclass == 2U);
    assert(i->entries[2].prog_if == 3U && i->entries[2].revision == 4U);
    assert(guarded.before == 0xA5A5A5A5A5A5A5A5ULL && guarded.after == guarded.before);
    f = (struct fixture){.dense = true};
    assert(boring_pci_inventory_collect(i, read_config, &f));
    assert(i->complete && i->truncated && i->stored == BORING_PCI_INVENTORY_MAX);
    assert(i->total == 65536U && f.calls == 196608U);
    assert(i->entries[255].bdf.bus == 0U && i->entries[255].bdf.device == 31U && i->entries[255].bdf.function == 7U);
    f = (struct fixture){.dense = true, .fail_at = 800U};
    assert(!boring_pci_inventory_collect(i, read_config, &f));
    assert(!i->complete && i->truncated && i->stored == 256U && i->total == 266U);
    f = (struct fixture){.empty = true};
    assert(boring_pci_inventory_collect(i, read_config, &f));
    assert(i->complete && !i->truncated && i->stored == 0U && i->total == 0U && f.calls == 8192U);
    f = (struct fixture){.fail_at = 1U};
    assert(!boring_pci_inventory_collect(i, read_config, &f));
    assert(!i->complete && i->config_reads == 1U && i->total == 0U);
    assert(guarded.before == 0xA5A5A5A5A5A5A5A5ULL && guarded.after == guarded.before);
    puts("PCI inventory multifunction, bus255, 65536-function bound, truncation, read errors and canaries passed.");
    return 0;
}
