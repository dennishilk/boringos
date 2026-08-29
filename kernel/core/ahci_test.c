#include <stdint.h>

#include <boring/ahci.h>
#include <boring/ahci_test.h>
#include <boring/cpu.h>
#include <boring/serial.h>

static void fail(const char *reason) __attribute__((noreturn));
static void fail(const char *reason) {
    serial_write_string("M55 AHCI QEMU FAILED: ");
    serial_write_string(reason);
    serial_write_string("\n");
    x86_64_halt_forever();
}

static void write_bdf(const struct pci_bdf *bdf) {
    serial_write_hex_u64((uint64_t)bdf->bus);
    serial_write_string(":");
    serial_write_hex_u64((uint64_t)bdf->device);
    serial_write_string(".");
    serial_write_hex_u64((uint64_t)bdf->function);
}

void ahci_test_run(void) {
    struct ahci_state state;
    uint8_t port;
    bool found_sata = false;

    if (!ahci_init(&state)) {
        fail("controller initialization");
    }
    if (!state.initialized || !state.ahci_enabled ||
        !state.bios_handoff_complete ||
        (state.prog_if != AHCI_PCI_PROG_IF)) {
        fail("controller state");
    }
    if ((state.hardware_ports == 0U) || (state.inspected_ports == 0U) ||
        (state.inspected_ports > AHCI_BORING_MAX_PORTS) ||
        (state.implemented_ports == 0U) || (state.sata_ports == 0U)) {
        fail("bounded ports or SATA presence");
    }

    serial_write_string("M55 AHCI BDF: ");
    write_bdf(&state.device.bdf);
    serial_write_string("\nM55 AHCI ABAR: ");
    serial_write_hex_u64(state.abar_physical);
    serial_write_string("\nM55 AHCI CAP: ");
    serial_write_hex_u64((uint64_t)state.cap);
    serial_write_string("\nM55 AHCI CAP2: ");
    serial_write_hex_u64((uint64_t)state.cap2);
    serial_write_string("\nM55 AHCI VS: ");
    serial_write_hex_u64((uint64_t)state.vs);
    serial_write_string("\nM55 AHCI GHC: ");
    serial_write_hex_u64((uint64_t)state.ghc);
    serial_write_string("\nM55 AHCI PI: ");
    serial_write_hex_u64((uint64_t)state.pi);
    serial_write_string("\nM55 AHCI inspected ports: ");
    serial_write_u64((uint64_t)state.inspected_ports);
    serial_write_string("\n");

    for (port = 0U; port < state.inspected_ports; ++port) {
        const struct ahci_port_state *p = &state.ports[port];
        if (!p->implemented) {
            continue;
        }
        serial_write_string("M55 AHCI port ");
        serial_write_u64((uint64_t)port);
        serial_write_string(" SSTS=");
        serial_write_hex_u64((uint64_t)p->ssts);
        serial_write_string(" SIG=");
        serial_write_hex_u64((uint64_t)p->sig);
        serial_write_string(" TFD=");
        serial_write_hex_u64((uint64_t)p->tfd);
        serial_write_string(" CMD=");
        serial_write_hex_u64((uint64_t)p->cmd);
        serial_write_string(" DET=");
        serial_write_u64((uint64_t)p->facts.det);
        serial_write_string(" IPM=");
        serial_write_u64((uint64_t)p->facts.ipm);
        serial_write_string("\n");
        if (p->facts.sata) {
            found_sata = true;
            serial_write_string("M55 AHCI SATA present: port=");
            serial_write_u64((uint64_t)port);
            serial_write_string("\n");
        }
    }
    if (!found_sata) {
        fail("no real SATA signature");
    }
    if (!ahci_shutdown()) {
        fail("cleanup");
    }

    serial_write_string("M55 AHCI cleanup: PASS\n");
    serial_write_string("M55 AHCI/SATA controller foundation QEMU passed.\n");
    x86_64_halt_forever();
}
