#include <stdbool.h>
#include <stdint.h>

#include <boring/cpu.h>
#include <boring/m65_hub_test.h>
#include <boring/serial.h>
#include <boring/xhci.h>
#include <boring/xhci_mixed.h>

#define M65_EXPECTED_ROOT_DEVICES 1U
#define M65_EXPECTED_DOWNSTREAM_DEVICES 1U

static void fail(const char *reason) __attribute__((noreturn));
static void fail(const char *reason) {
    serial_write_string("M65 USB hub FAILED: ");
    serial_write_string(reason);
    serial_write_string("\n");
    x86_64_halt_forever();
}

static const struct xhci_addressed_device *find_hub(
    const struct xhci_state *state) {
    uint8_t index;
    if (state == NULL) { return NULL; }
    for (index = 0U; index < state->addressed_count; ++index) {
        const struct xhci_addressed_device *device = &state->addressed[index];
        if (device->descriptors_ready &&
            (device->descriptors.device_class == 9U)) {
            return device;
        }
    }
    return NULL;
}

static const struct xhci_addressed_device *find_downstream_mouse(
    const struct xhci_state *state) {
    uint8_t device_index;
    if (state == NULL) { return NULL; }
    for (device_index = 0U; device_index < state->addressed_count;
         ++device_index) {
        const struct xhci_addressed_device *device =
            &state->addressed[device_index];
        uint8_t endpoint_index;
        if (!device->descriptors_ready || (device->topology.depth == 0U)) {
            continue;
        }
        for (endpoint_index = 0U;
             endpoint_index < device->hid_configuration.endpoint_count;
             ++endpoint_index) {
            if (device->hid_configuration.endpoints[endpoint_index].report_format ==
                XHCI_HID_REPORT_BOOT_MOUSE) {
                return device;
            }
        }
    }
    return NULL;
}

void m65_hub_test_run(void) {
    struct xhci_state state;
    const struct xhci_addressed_device *hub;
    const struct xhci_addressed_device *mouse;

    if (!xhci_init(&state) || xhci_controller_registry_truncated() ||
        (xhci_controller_count() != 1U)) {
        fail("controller initialization");
    }
    if (!xhci_address_connected(&state) ||
        (state.addressed_count != M65_EXPECTED_ROOT_DEVICES) ||
        state.addressing_truncated) {
        fail("root hub addressing");
    }
    if (!xhci_discover_descriptors(&state)) {
        fail("root hub descriptors");
    }
    hub = find_hub(&state);
    if ((hub == NULL) || (hub->topology.depth != 0U) ||
        (hub->topology.route_string != 0U) ||
        (hub->topology.root_port == 0U)) {
        fail("root hub classification");
    }

    if (!xhci_enumerate_hubs(&state)) {
        fail("hub class-control enumeration");
    }
    hub = find_hub(&state);
    if ((hub == NULL) || !hub->hub_ready ||
        (hub->hub_descriptor.port_count == 0U) ||
        (hub->hub_descriptor.port_count > BORING_USB_HUB_DESCRIPTOR_MAX_PORTS)) {
        fail("hub descriptor state");
    }
    serial_write_string("USB_HUB_DESCRIPTOR=PASS\n");

    if (state.hub_ports_powered < hub->hub_descriptor.port_count) {
        fail("hub port power accounting");
    }
    serial_write_string("USB_HUB_PORT_POWER=PASS\n");

    if (state.hub_ports_reset < M65_EXPECTED_DOWNSTREAM_DEVICES) {
        fail("hub port reset accounting");
    }
    serial_write_string("USB_HUB_PORT_STATUS=PASS\n");
    serial_write_string("USB_HUB_PORT_RESET=PASS\n");

    if ((state.downstream_devices_addressed !=
         M65_EXPECTED_DOWNSTREAM_DEVICES) ||
        (state.addressed_count !=
         M65_EXPECTED_ROOT_DEVICES + M65_EXPECTED_DOWNSTREAM_DEVICES)) {
        fail("downstream addressed count");
    }

    if (!xhci_discover_descriptors(&state)) {
        fail("downstream descriptors");
    }
    if (!xhci_configure_hid_devices_mixed(&state)) {
        fail("downstream HID configuration");
    }
    hub = find_hub(&state);
    mouse = find_downstream_mouse(&state);
    if ((hub == NULL) || (mouse == NULL) ||
        (mouse->controller_index != hub->controller_index) ||
        (mouse->root_port_id != hub->root_port_id) ||
        (mouse->topology.root_port != hub->topology.root_port) ||
        (mouse->topology.depth != 1U) ||
        (mouse->topology.route_string == 0U) ||
        (mouse->topology.parent_hub_slot != hub->slot_id) ||
        (mouse->topology.downstream_port != 1U) ||
        (mouse->topology.route_string != 1U) ||
        (mouse->slot_id == hub->slot_id) ||
        !mouse->device_configured || !mouse->hid_endpoint_ready) {
        fail("downstream topology identity");
    }
    serial_write_string("USB_HUB_ROUTE_STRING=PASS\n");
    serial_write_string("USB_HUB_DOWNSTREAM_ADDRESS=PASS\n");
    serial_write_string("USB_HUB_DOWNSTREAM_MOUSE=PASS\n");
    serial_write_string("M65 hub downstream mouse enumeration passed.\n");
    x86_64_halt_forever();
}
