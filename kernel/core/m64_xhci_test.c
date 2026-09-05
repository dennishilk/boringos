#include <stdbool.h>
#include <stdint.h>

#include <boring/cpu.h>
#include <boring/m64_xhci_test.h>
#include <boring/serial.h>
#include <boring/xhci.h>

#define M64_EXPECTED_CONTROLLERS 3U

static void fail(const char *reason) __attribute__((noreturn));
static void fail(const char *reason) {
    serial_write_string("M64 multi-xHCI FAILED: ");
    serial_write_string(reason);
    serial_write_string("\n");
    x86_64_halt_forever();
}

static bool same_bdf(const struct pci_bdf *left, const struct pci_bdf *right) {
    return (left != NULL) && (right != NULL) &&
           (left->bus == right->bus) &&
           (left->device == right->device) &&
           (left->function == right->function);
}

static void print_bdf(const struct pci_bdf *bdf) {
    serial_write_hex_u64((uint64_t)bdf->bus);
    serial_write_string(":");
    serial_write_hex_u64((uint64_t)bdf->device);
    serial_write_string(".");
    serial_write_u64((uint64_t)bdf->function);
}

void m64_xhci_test_run(void) {
    struct xhci_state primary;
    uint64_t dequeue_before[XHCI_MAX_CONTROLLERS] = {0ULL};
    uint8_t connected_index = UINT8_MAX;
    uint8_t connected_count = 0U;
    uint8_t index;
    uint8_t other;

    if (!xhci_init(&primary)) {
        fail("controller registry initialization");
    }
    if (xhci_controller_registry_truncated() ||
        (xhci_controller_count() != M64_EXPECTED_CONTROLLERS)) {
        fail("expected three bounded controllers");
    }

    for (index = 0U; index < xhci_controller_count(); ++index) {
        struct xhci_state *state = xhci_get_controller(index);
        if ((state == NULL) ||
            (xhci_get_controller_status(index) != XHCI_CONTROLLER_RUNNING) ||
            !state->controller_running ||
            (state->controller_index != index) ||
            (state->mmio_physical == 0ULL) ||
            (state->dcbaa_physical == 0ULL) ||
            (state->command_ring_physical == 0ULL) ||
            (state->event_ring_physical == 0ULL) ||
            (state->erst_physical == 0ULL)) {
            fail("controller independent runtime state");
        }
        dequeue_before[index] = state->event_dequeue_count;
        if (state->connected_ports != 0ULL) {
            connected_index = index;
            ++connected_count;
        }
        for (other = 0U; other < index; ++other) {
            const struct xhci_state *previous = xhci_get_controller(other);
            if ((previous == NULL) ||
                same_bdf(&state->device.bdf, &previous->device.bdf) ||
                (state->mmio_physical == previous->mmio_physical) ||
                (state->dcbaa_physical == previous->dcbaa_physical) ||
                (state->command_ring_physical ==
                 previous->command_ring_physical) ||
                (state->event_ring_physical ==
                 previous->event_ring_physical) ||
                (state->erst_physical == previous->erst_physical)) {
                fail("cross-controller resource alias");
            }
        }

        serial_write_string("M64 controller[");
        serial_write_u64((uint64_t)index);
        serial_write_string("] BDF=");
        print_bdf(&state->device.bdf);
        serial_write_string(" MMIO=");
        serial_write_hex_u64(state->mmio_physical);
        serial_write_string(" CMD=");
        serial_write_hex_u64(state->command_ring_physical);
        serial_write_string(" EVT=");
        serial_write_hex_u64(state->event_ring_physical);
        serial_write_string(" connected=");
        serial_write_hex_u64(state->connected_ports);
        serial_write_string("\n");
    }

    if ((connected_count != 1U) || (connected_index == UINT8_MAX)) {
        fail("test topology must expose exactly one connected controller");
    }

    {
        struct xhci_state *keyboard = xhci_get_controller(connected_index);
        uint8_t device_index;
        bool boot_keyboard = false;

        if ((keyboard == NULL) ||
            !xhci_address_connected(keyboard) ||
            !xhci_discover_descriptors(keyboard) ||
            !xhci_configure_hid_devices(keyboard) ||
            (keyboard->addressed_count == 0U) ||
            keyboard->addressing_truncated) {
            fail("real keyboard controller traffic");
        }
        for (device_index = 0U; device_index < keyboard->addressed_count;
             ++device_index) {
            const struct xhci_addressed_device *device =
                &keyboard->addressed[device_index];
            uint8_t endpoint_index;
            if (!device->addressed ||
                (device->controller_index != connected_index)) {
                fail("slot owner index");
            }
            for (endpoint_index = 0U;
                 endpoint_index < device->hid_configuration.endpoint_count;
                 ++endpoint_index) {
                const struct xhci_hid_endpoint_descriptor *endpoint =
                    &device->hid_configuration.endpoints[endpoint_index];
                if ((endpoint->report_format ==
                     XHCI_HID_REPORT_BOOT_KEYBOARD) &&
                    (endpoint->protocol == 1U)) {
                    boot_keyboard = true;
                }
            }
        }
        if (!boot_keyboard) {
            fail("real boot keyboard descriptor/configuration");
        }
        if (keyboard->event_dequeue_count <=
            dequeue_before[connected_index]) {
            fail("own controller Event Ring did not advance");
        }
    }

    for (index = 0U; index < xhci_controller_count(); ++index) {
        const struct xhci_state *state = xhci_get_controller(index);
        if ((state == NULL) || (index == connected_index)) {
            continue;
        }
        if ((state->connected_ports != 0ULL) ||
            (state->addressed_count != 0U) ||
            (state->event_dequeue_count != dequeue_before[index])) {
            fail("foreign controller state changed");
        }
    }

    serial_write_string("MULTI_XHCI_CONTROLLER_COUNT=3\n");
    serial_write_string("MULTI_XHCI_DISTINCT_BDFS=YES\n");
    serial_write_string("MULTI_XHCI_INDEPENDENT_MMIO=YES\n");
    serial_write_string("MULTI_XHCI_INDEPENDENT_COMMAND_RINGS=YES\n");
    serial_write_string("MULTI_XHCI_INDEPENDENT_EVENT_RINGS=YES\n");
    serial_write_string("KEYBOARD_ON_CONTROLLER_A=PASS\n");
    serial_write_string("EMPTY_SECONDARY_CONTROLLER_ALLOWED=PASS\n");
    serial_write_string("CROSS_CONTROLLER_EVENT_OWNERSHIP=PASS\n");
    serial_write_string("M64 real multi-xHCI QEMU passed.\n");
    x86_64_halt_forever();
}
