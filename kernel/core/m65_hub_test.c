#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/cpu.h>
#include <boring/input.h>
#include <boring/m65_hub_test.h>
#include <boring/serial.h>
#include <boring/xhci.h>
#include <boring/xhci_mixed.h>

#define M65_EXPECTED_ROOT_DEVICES 1U
#define M65_EXPECTED_DOWNSTREAM_DEVICES 1U
#define M65_INPUT_OWNER_PID 65ULL
#define M65_INPUT_EVENT_MAX 8U
#define M65_INPUT_POLL_MAX 8U

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

static void verify_downstream_mouse_input(struct xhci_state *state) {
    struct boring_input_stats stats;
    struct boring_input_event events[BORING_INPUT_READ_MAX];
    size_t event_count = 0U;
    uint32_t poll_count;
    bool saw_move = false;
    bool saw_left_down = false;
    bool saw_left_up = false;
    uint32_t completed = 0U;
    uint32_t decoded = 0U;
    uint32_t pointer_reports = 0U;
    size_t index;
    uint8_t device_index;

    if ((state == NULL) || !boring_input_init() ||
        (boring_input_claim(M65_INPUT_OWNER_PID) != BORING_INPUT_RESULT_OK)) {
        fail("canonical mouse input ownership");
    }

    serial_write_string(
        "M65 downstream mouse ready; inject real USB mouse input now.\n");
    for (poll_count = 0U; poll_count < M65_INPUT_POLL_MAX; ++poll_count) {
        if (!xhci_poll_hid_reports(state, 1U)) {
            fail("downstream mouse Interrupt-IN");
        }
        if (!boring_input_get_stats(&stats) || !stats.initialized ||
            !stats.owned || (stats.owner_pid != M65_INPUT_OWNER_PID) ||
            (stats.dropped_events != 0ULL) ||
            (stats.queued_events > M65_INPUT_EVENT_MAX)) {
            fail("bounded downstream mouse queue progress");
        }
        if (stats.queued_events >= 3U) { break; }
    }
    if ((poll_count == M65_INPUT_POLL_MAX) ||
        !boring_input_get_stats(&stats) ||
        (stats.queued_events < 3U) ||
        (stats.queued_events > M65_INPUT_EVENT_MAX) ||
        (stats.dropped_events != 0ULL)) {
        fail("downstream mouse queue end state");
    }

    if ((boring_input_read(M65_INPUT_OWNER_PID, events, BORING_INPUT_READ_MAX,
                           &event_count) != BORING_INPUT_RESULT_OK) ||
        (event_count < 3U) || (event_count > M65_INPUT_EVENT_MAX)) {
        fail("downstream mouse queue read");
    }
    for (index = 0U; index < event_count; ++index) {
        if ((events[index].type == BORING_INPUT_EVENT_MOUSE_MOVE) &&
            ((events[index].value1 != 0) || (events[index].value2 != 0))) {
            saw_move = true;
        } else if ((events[index].type == BORING_INPUT_EVENT_MOUSE_BUTTON) &&
                   (events[index].code == BORING_MOUSE_BUTTON_LEFT)) {
            if (events[index].value1 == 1) { saw_left_down = true; }
            if (events[index].value1 == 0) { saw_left_up = true; }
        }
    }
    if (!saw_move || !saw_left_down || !saw_left_up) {
        fail("canonical downstream mouse event classes");
    }

    for (device_index = 0U; device_index < state->addressed_count;
         ++device_index) {
        const struct xhci_addressed_device *device =
            &state->addressed[device_index];
        uint8_t endpoint_index;
        if (device->topology.depth == 0U) { continue; }
        for (endpoint_index = 0U;
             endpoint_index < device->hid_configuration.endpoint_count;
             ++endpoint_index) {
            const struct xhci_hid_endpoint_descriptor *endpoint =
                &device->hid_configuration.endpoints[endpoint_index];
            const struct xhci_hid_endpoint_runtime *runtime =
                &device->hid_runtime[endpoint_index];
            if (endpoint->report_format != XHCI_HID_REPORT_BOOT_MOUSE) {
                continue;
            }
            if ((UINT32_MAX - completed < runtime->completed_transfers) ||
                (UINT32_MAX - decoded < runtime->decoded_reports) ||
                (UINT32_MAX - pointer_reports < runtime->pointer_reports)) {
                fail("downstream mouse transport counter overflow");
            }
            completed += runtime->completed_transfers;
            decoded += runtime->decoded_reports;
            pointer_reports += runtime->pointer_reports;
        }
    }
    if ((completed < 3U) || (decoded != completed) ||
        (pointer_reports != decoded)) {
        fail("downstream mouse transport accounting");
    }
    if (boring_input_release(M65_INPUT_OWNER_PID) != BORING_INPUT_RESULT_OK) {
        fail("canonical mouse input release");
    }

    serial_write_string("USB_HUB_MOUSE_INTERRUPT_IN=PASS\n");
    serial_write_string("USB_HUB_MOUSE_CANONICAL_QUEUE=PASS\n");
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

    verify_downstream_mouse_input(&state);

    serial_write_string("M65 hub downstream mouse input passed.\n");
    x86_64_halt_forever();
}
