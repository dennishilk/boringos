#include <stdint.h>

#include <boring/cpu.h>
#include <boring/serial.h>
#include <boring/xhci.h>
#include <boring/xhci_test.h>

static void fail(const char *reason) __attribute__((noreturn));
static void fail(const char *reason) {
    serial_write_string("M48 xHCI QEMU FAILED: ");
    serial_write_string(reason);
    serial_write_string("\n");
    x86_64_halt_forever();
}

void xhci_test_run(void) {
    struct xhci_state state;
    if (!xhci_init(&state)) { fail("controller initialization"); }
    if (!state.controller_running || !state.legacy_handoff_complete) {
        fail("controller state");
    }
    if (state.capabilities.max_slots == 0U ||
        state.capabilities.max_ports == 0U ||
        state.capabilities.max_ports > XHCI_MAX_PORTS ||
        state.dcbaa_physical == 0ULL ||
        state.command_ring_physical == 0ULL ||
        state.event_ring_physical == 0ULL || state.erst_physical == 0ULL) {
        fail("bounded capabilities or DMA transport");
    }
    if (state.connected_ports == 0ULL) { fail("no connected USB ports"); }
    serial_write_string("M48 xHCI controller: ");
    serial_write_hex_u64((uint64_t)state.device.vendor_id);
    serial_write_string(":");
    serial_write_hex_u64((uint64_t)state.device.device_id);
    serial_write_string("\nM48 xHCI ports: ");
    serial_write_u64((uint64_t)state.capabilities.max_ports);
    serial_write_string("\nM48 xHCI slots: ");
    serial_write_u64((uint64_t)state.capabilities.max_slots);
    serial_write_string("\nM48 xHCI connected bitmap: ");
    serial_write_hex_u64(state.connected_ports);
    serial_write_string("\nM48 xHCI DMA command/event transport online\n");
    serial_write_string("M48 xHCI/USB-HID foundation QEMU passed.\n");
    x86_64_halt_forever();
}
