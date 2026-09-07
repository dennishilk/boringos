#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <boring/ahci.h>

struct poll_fixture {
    uint32_t value;
    uint32_t reads;
    uint32_t change_after;
    bool fail_read;
};

static bool fixture_read(void *context, uint32_t offset, uint32_t *value) {
    struct poll_fixture *fixture = (struct poll_fixture *)context;
    (void)offset;
    if ((fixture == NULL) || (value == NULL) || fixture->fail_read) {
        return false;
    }
    ++fixture->reads;
    if ((fixture->change_after != 0U) &&
        (fixture->reads >= fixture->change_after)) {
        fixture->value &= ~(1U << 15U);
    }
    *value = fixture->value;
    return true;
}

static int fail(const char *name) {
    (void)fprintf(stderr, "ahci-host-test: FAIL: %s\n", name);
    return 1;
}

static void inventory_base(struct boring_pci_inventory *inventory) {
    uint8_t *bytes = (uint8_t *)inventory;
    size_t index;
    for (index = 0U; index < sizeof(*inventory); ++index) {
        bytes[index] = 0U;
    }
    inventory->complete = true;
}

static void add_ahci(struct boring_pci_inventory *inventory, uint8_t bus,
                     uint8_t device, uint8_t function) {
    struct boring_pci_entry *entry = &inventory->entries[inventory->stored];
    entry->bdf.bus = bus;
    entry->bdf.device = device;
    entry->bdf.function = function;
    entry->vendor_id = 0x1234U;
    entry->device_id = 0x5678U;
    entry->class_code = AHCI_PCI_CLASS_MASS_STORAGE;
    entry->subclass = AHCI_PCI_SUBCLASS_SATA;
    entry->prog_if = AHCI_PCI_PROG_IF;
    ++inventory->stored;
    ++inventory->total;
}

int main(void) {
    struct boring_pci_inventory inventory;
    struct pci_device device;
    struct pci_bar bar;
    struct ahci_port_facts facts;
    struct {
        uint32_t before;
        struct ahci_port_facts facts;
        uint32_t after;
    } guarded = {0x11223344U, {0U, 0U, false, false}, 0xaabbccddU};
    struct poll_fixture poll;
    uint8_t prog_if;
    uint8_t hardware;
    uint8_t inspected;
    uint32_t mask;
    uint32_t last;
    bool truncated;

    inventory_base(&inventory);
    if (ahci_select_controller(&inventory, &device, &prog_if)) {
        return fail("no-controller");
    }
    inventory.entries[0].class_code = 0x01U;
    inventory.entries[0].subclass = 0x06U;
    inventory.entries[0].prog_if = 0x00U;
    inventory.stored = 1U;
    inventory.total = 1U;
    if (ahci_select_controller(&inventory, &device, &prog_if)) {
        return fail("wrong-prog-if");
    }

    inventory_base(&inventory);
    add_ahci(&inventory, 2U, 7U, 1U);
    if (!ahci_select_controller(&inventory, &device, &prog_if) ||
        (device.bdf.bus != 2U) || (device.bdf.device != 7U) ||
        (device.bdf.function != 1U) || (prog_if != AHCI_PCI_PROG_IF)) {
        return fail("dynamic-controller-selection");
    }
    add_ahci(&inventory, 3U, 1U, 0U);
    if (ahci_select_controller(&inventory, &device, &prog_if)) {
        return fail("ambiguous-controller");
    }
    inventory.stored = 1U;
    inventory.truncated = true;
    if (ahci_select_controller(&inventory, &device, &prog_if)) {
        return fail("truncated-inventory");
    }

    bar.base = 0x4000ULL;
    bar.memory = true;
    bar.is_64bit = false;
    bar.prefetchable = false;
    if (!ahci_validate_abar(&bar, AHCI_MMIO_WINDOW_SIZE)) {
        return fail("valid-abar");
    }
    bar.memory = false;
    if (ahci_validate_abar(&bar, AHCI_MMIO_WINDOW_SIZE)) {
        return fail("io-abar");
    }
    bar.memory = true;
    bar.is_64bit = true;
    if (ahci_validate_abar(&bar, AHCI_MMIO_WINDOW_SIZE)) {
        return fail("unsupported-64bit-bar5");
    }
    bar.is_64bit = false;
    bar.prefetchable = true;
    if (ahci_validate_abar(&bar, AHCI_MMIO_WINDOW_SIZE)) {
        return fail("prefetchable-abar");
    }
    bar.prefetchable = false;
    bar.base = 0x4100ULL;
    if (ahci_validate_abar(&bar, AHCI_MMIO_WINDOW_SIZE)) {
        return fail("abar-alignment");
    }
    bar.base = UINT64_MAX & ~0x3ffULL;
    if (ahci_validate_abar(&bar, AHCI_MMIO_WINDOW_SIZE)) {
        return fail("abar-overflow");
    }

    if (!ahci_bounded_ports(31U, UINT32_MAX, &hardware, &inspected,
                            &mask, &truncated) ||
        (hardware != 32U) || (inspected != AHCI_BORING_MAX_PORTS) ||
        (mask != 0xffffU) || !truncated) {
        return fail("port-bound");
    }
    if (!ahci_bounded_ports(0U, 0U, &hardware, &inspected,
                            &mask, &truncated) ||
        (hardware != 1U) || (inspected != 1U) || (mask != 0U) || truncated) {
        return fail("no-implemented-port");
    }

    if (!ahci_decode_port_status(0x00000103U, AHCI_SIGNATURE_ATA, &facts) ||
        !facts.present || !facts.sata || (facts.det != 3U) ||
        (facts.ipm != 1U)) {
        return fail("present-sata");
    }
    if (!ahci_decode_port_status(0x00000103U, 0xeb140101U, &facts) ||
        !facts.present || facts.sata) {
        return fail("present-non-sata");
    }
    if (!ahci_decode_port_status(0U, 0U, &facts) || facts.present) {
        return fail("implemented-not-present");
    }
    if (ahci_decode_port_status(0x00000102U, AHCI_SIGNATURE_ATA, &facts) ||
        ahci_decode_port_status(0x00000303U, AHCI_SIGNATURE_ATA, &facts)) {
        return fail("invalid-ssts");
    }
    if (!ahci_decode_port_status(0x00000103U, AHCI_SIGNATURE_ATA,
                                 &guarded.facts) ||
        (guarded.before != 0x11223344U) ||
        (guarded.after != 0xaabbccddU)) {
        return fail("state-canaries");
    }

    poll.value = (1U << 15U);
    poll.reads = 0U;
    poll.change_after = 0U;
    poll.fail_read = false;
    if (ahci_wait_mask_bounded(fixture_read, &poll, 0x118U,
                               (1U << 15U), false, 4U, &last) ||
        (poll.reads != 4U) || ((last & (1U << 15U)) == 0U)) {
        return fail("bounded-engine-timeout");
    }
    poll.value = (1U << 15U);
    poll.reads = 0U;
    poll.change_after = 3U;
    if (!ahci_wait_mask_bounded(fixture_read, &poll, 0x118U,
                                (1U << 15U), false, 8U, &last) ||
        (poll.reads != 3U) || ((last & (1U << 15U)) != 0U)) {
        return fail("bounded-engine-completion");
    }
    poll.value = 1U;
    poll.reads = 0U;
    poll.change_after = 0U;
    if (ahci_wait_mask_bounded(fixture_read, &poll, 0x28U,
                               1U, false, 5U, &last) ||
        (poll.reads != 5U)) {
        return fail("bounded-bohc-timeout");
    }

    (void)puts("ahci-host-test: PASS");
    return 0;
}
