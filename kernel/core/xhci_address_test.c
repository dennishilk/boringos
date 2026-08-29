#include <stdbool.h>
#include <stdint.h>

#include <boring/cpu.h>
#include <boring/serial.h>
#include <boring/xhci.h>
#include <boring/xhci_address_test.h>

#define M49_EXPECTED_ATTACHED_DEVICES 2U

#ifdef BORING_M51_CONFIGURATION_ACCEPTANCE

static void fail(const char *reason) __attribute__((noreturn));
static void fail(const char *reason) {
    serial_write_string("M51 xHCI configuration FAILED: ");
    serial_write_string(reason);
    serial_write_string("\n");
    x86_64_halt_forever();
}

void xhci_address_test_run(void) {
    struct xhci_state state;
    uint32_t set_configuration_completions = 0U;
    uint32_t configure_endpoint_completions = 0U;
    uint32_t transfer_events = 0U;
    uint32_t evaluate_completions = 0U;
    uint8_t index;
    if (!xhci_init(&state)) { fail("controller initialization"); }
    if (!xhci_address_connected(&state)) { fail("M49 addressing"); }
    if ((state.addressed_count != M49_EXPECTED_ATTACHED_DEVICES) || state.addressing_truncated) {
        fail("M49 addressed prerequisite");
    }
    if (!xhci_discover_descriptors(&state)) { fail("M50 descriptors"); }
    if (!xhci_configure_hid_devices(&state)) { fail("M51 real configuration"); }
    for (index = 0U; index < state.addressed_count; ++index) {
        const struct xhci_addressed_device *device = &state.addressed[index];
        uint8_t endpoint_index;
        if (!device->addressed || !device->descriptors_ready ||
            !device->device_configured || !device->hid_endpoint_ready ||
            device->control_outstanding ||
            (device->hid_configuration.configuration_value == 0U) ||
            (device->hid_configuration.endpoint_count == 0U) ||
            (device->hid_configuration.endpoint_count > XHCI_MAX_HID_ENDPOINTS) ||
            (device->set_configuration_completions != 1U) ||
            (device->configure_endpoint_completions != 1U)) {
            fail("bounded configured state");
        }
        serial_write_string("M51 configured device port=");
        serial_write_u64((uint64_t)device->root_port_id);
        serial_write_string(" slot=");
        serial_write_u64((uint64_t)device->slot_id);
        serial_write_string(" configuration=");
        serial_write_u64((uint64_t)device->hid_configuration.configuration_value);
        serial_write_string(" hid_endpoints=");
        serial_write_u64((uint64_t)device->hid_configuration.endpoint_count);
        serial_write_string("\n");
        for (endpoint_index = 0U; endpoint_index < device->hid_configuration.endpoint_count;
             ++endpoint_index) {
            const struct xhci_hid_endpoint_descriptor *endpoint =
                &device->hid_configuration.endpoints[endpoint_index];
            uint8_t mapped = 0U;
            if ((device->hid_ring_physical[endpoint_index] == 0ULL) ||
                !xhci_usb_endpoint_id(endpoint->endpoint_address, &mapped) ||
                (mapped != endpoint->endpoint_id) ||
                ((endpoint->endpoint_address & 0x80U) == 0U) ||
                (endpoint->max_packet == 0U) || (endpoint->interval == 0U)) {
                fail("dynamic endpoint state");
            }
            serial_write_string("M51 HID endpoint slot=");
            serial_write_u64((uint64_t)device->slot_id);
            serial_write_string(" address=");
            serial_write_u64((uint64_t)endpoint->endpoint_address);
            serial_write_string(" endpoint_id=");
            serial_write_u64((uint64_t)endpoint->endpoint_id);
            serial_write_string(" max_packet=");
            serial_write_u64((uint64_t)endpoint->max_packet);
            serial_write_string(" interval=");
            serial_write_u64((uint64_t)endpoint->interval);
            serial_write_string(" xhci_interval=");
            serial_write_u64((uint64_t)endpoint->xhci_interval);
            serial_write_string("\n");
        }
        if ((UINT32_MAX - set_configuration_completions < device->set_configuration_completions) ||
            (UINT32_MAX - configure_endpoint_completions < device->configure_endpoint_completions) ||
            (UINT32_MAX - transfer_events < device->transfer_events) ||
            (UINT32_MAX - evaluate_completions < device->evaluate_context_completions)) {
            fail("counter overflow");
        }
        set_configuration_completions += device->set_configuration_completions;
        configure_endpoint_completions += device->configure_endpoint_completions;
        transfer_events += device->transfer_events;
        evaluate_completions += device->evaluate_context_completions;
    }
    if ((set_configuration_completions != state.addressed_count) ||
        (configure_endpoint_completions != state.addressed_count) ||
        (state.command_completions != ((uint32_t)state.addressed_count * 3U) + evaluate_completions)) {
        fail("real completion accounting");
    }
    serial_write_string("M51 real SET_CONFIGURATION completions: ");
    serial_write_u64((uint64_t)set_configuration_completions);
    serial_write_string("\nM51 real Configure Endpoint completions: ");
    serial_write_u64((uint64_t)configure_endpoint_completions);
    serial_write_string("\nM51 real Transfer Events consumed: ");
    serial_write_u64((uint64_t)transfer_events);
    serial_write_string("\nM51 xHCI HID endpoint setup QEMU passed.\n");
    x86_64_halt_forever();
}

#elif defined(BORING_M50_DESCRIPTOR_ACCEPTANCE)

static void fail(const char *reason) __attribute__((noreturn));
static void fail(const char *reason) {
    serial_write_string("M50 xHCI descriptors FAILED: ");
    serial_write_string(reason);
    serial_write_string("\n");
    x86_64_halt_forever();
}

static void print_device(const struct xhci_addressed_device *device) {
    serial_write_string("M50 descriptor device port=");
    serial_write_u64((uint64_t)device->root_port_id);
    serial_write_string(" slot=");
    serial_write_u64((uint64_t)device->slot_id);
    serial_write_string(" speed=");
    serial_write_u64((uint64_t)device->speed);
    serial_write_string(" vid=");
    serial_write_u64((uint64_t)device->descriptors.vendor_id);
    serial_write_string(" pid=");
    serial_write_u64((uint64_t)device->descriptors.product_id);
    serial_write_string(" configuration_length=");
    serial_write_u64((uint64_t)device->descriptors.configuration_length);
    serial_write_string(" interfaces=");
    serial_write_u64((uint64_t)device->descriptors.interface_count);
    serial_write_string("\n");
}

void xhci_address_test_run(void) {
    struct xhci_state state;
    uint32_t transfer_events = 0U;
    uint32_t descriptor_bytes = 0U;
    uint32_t evaluate_completions = 0U;
    uint8_t index;

    if (!xhci_init(&state)) { fail("controller initialization"); }
    if (!xhci_address_connected(&state)) { fail("M49 address connected devices"); }
    if ((state.addressed_count != M49_EXPECTED_ATTACHED_DEVICES) ||
        state.addressing_truncated ||
        (state.command_completions !=
         (uint32_t)M49_EXPECTED_ATTACHED_DEVICES * 2U)) {
        fail("M49 addressed-device prerequisite");
    }
    if (!xhci_discover_descriptors(&state)) {
        fail("real EP0 descriptor discovery");
    }
    for (index = 0U; index < state.addressed_count; ++index) {
        const struct xhci_addressed_device *device = &state.addressed[index];
        const uint32_t expected_events = 4U + device->short_packets;
        const uint32_t expected_bytes =
            35U + (uint32_t)device->descriptors.configuration_length;
        uint8_t previous;
        if (!device->addressed || !device->descriptors_ready ||
            device->control_outstanding ||
            (device->root_port_id == 0U) ||
            (device->root_port_id > state.capabilities.max_ports) ||
            (device->slot_id == 0U) ||
            (device->slot_id > state.capabilities.max_slots) ||
            (device->speed == 0U) || (device->speed > 5U) ||
            (device->descriptor_buffer_physical == 0ULL) ||
            (device->descriptors.configuration_length < 9U) ||
            (device->descriptors.configuration_length >
             XHCI_DESCRIPTOR_BUFFER_BYTES) ||
            (device->descriptors.configuration_count == 0U) ||
            (device->descriptors.ep0_max_packet != device->ep0_max_packet) ||
            (device->transfer_events != expected_events) ||
            (device->descriptor_bytes != expected_bytes) ||
            (device->short_packets > 4U) ||
            (device->evaluate_context_completions > 1U)) {
            fail("bounded descriptor-ready state");
        }
        for (previous = 0U; previous < index; ++previous) {
            if ((state.addressed[previous].slot_id == device->slot_id) ||
                (state.addressed[previous].root_port_id ==
                 device->root_port_id)) {
                fail("unique port and slot mapping");
            }
        }
        if ((UINT32_MAX - transfer_events < device->transfer_events) ||
            (UINT32_MAX - descriptor_bytes < device->descriptor_bytes) ||
            (UINT32_MAX - evaluate_completions <
             device->evaluate_context_completions)) {
            fail("acceptance counter overflow");
        }
        transfer_events += device->transfer_events;
        descriptor_bytes += device->descriptor_bytes;
        evaluate_completions += device->evaluate_context_completions;
        print_device(device);
    }
    if (state.command_completions !=
        ((uint32_t)M49_EXPECTED_ATTACHED_DEVICES * 2U) +
        evaluate_completions) {
        fail("dynamic Evaluate Context command count");
    }
    serial_write_string("M50 real transfer events: ");
    serial_write_u64((uint64_t)transfer_events);
    serial_write_string("\nM50 real descriptor bytes: ");
    serial_write_u64((uint64_t)descriptor_bytes);
    serial_write_string("\nM50 Evaluate Context completions: ");
    serial_write_u64((uint64_t)evaluate_completions);
    serial_write_string("\nM50 xHCI EP0 descriptor discovery QEMU passed.\n");
    x86_64_halt_forever();
}

#else

static void fail(const char *reason) __attribute__((noreturn));
static void fail(const char *reason) {
    serial_write_string("M49 xHCI addressing FAILED: ");
    serial_write_string(reason);
    serial_write_string("\n");
    x86_64_halt_forever();
}

void xhci_address_test_run(void) {
    struct xhci_state state;
    uint8_t index;
    if (!xhci_init(&state)) { fail("controller initialization"); }
    if (!xhci_address_connected(&state)) { fail("address connected devices"); }
    if ((state.addressed_count != M49_EXPECTED_ATTACHED_DEVICES) ||
        state.addressing_truncated ||
        (state.command_completions !=
         (uint32_t)M49_EXPECTED_ATTACHED_DEVICES * 2U)) {
        fail("addressed count or command completions");
    }
    for (index = 0U; index < state.addressed_count; ++index) {
        const struct xhci_addressed_device *device = &state.addressed[index];
        uint8_t previous;
        if (!device->addressed || (device->root_port_id == 0U) ||
            (device->root_port_id > state.capabilities.max_ports) ||
            (device->slot_id == 0U) ||
            (device->slot_id > state.capabilities.max_slots) ||
            (device->speed == 0U) || (device->speed > 5U) ||
            ((state.connected_ports &
              (1ULL << (device->root_port_id - 1U))) == 0ULL) ||
            (device->input_context_physical == 0ULL) ||
            (device->device_context_physical == 0ULL) ||
            (device->ep0_ring_physical == 0ULL)) {
            fail("bounded addressed-device state");
        }
        for (previous = 0U; previous < index; ++previous) {
            if ((state.addressed[previous].slot_id == device->slot_id) ||
                (state.addressed[previous].root_port_id ==
                 device->root_port_id)) {
                fail("unique port and slot mapping");
            }
        }
        serial_write_string("M49 addressed device port=");
        serial_write_u64((uint64_t)device->root_port_id);
        serial_write_string(" slot=");
        serial_write_u64((uint64_t)device->slot_id);
        serial_write_string(" speed=");
        serial_write_u64((uint64_t)device->speed);
        serial_write_string("\n");
    }
    serial_write_string("M49 real Enable Slot completions: ");
    serial_write_u64((uint64_t)state.addressed_count);
    serial_write_string("\nM49 real Address Device completions: ");
    serial_write_u64((uint64_t)state.addressed_count);
    serial_write_string("\nM49 command completions consumed: ");
    serial_write_u64((uint64_t)state.command_completions);
    serial_write_string("\nM49 xHCI USB device addressing QEMU passed.\n");
    x86_64_halt_forever();
}

#endif
