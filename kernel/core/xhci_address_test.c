#include <stdbool.h>
#include <stdint.h>

#include <boring/cpu.h>
#include <boring/input.h>
#include <boring/serial.h>
#include <boring/xhci.h>
#include <boring/xhci_address_test.h>

#define M49_EXPECTED_ATTACHED_DEVICES 2U

#ifdef BORING_M53_INPUT_ACCEPTANCE

#define M53_INPUT_OWNER_PID 53ULL
#define M53_EXPECTED_COMPLETIONS 8U
#define M53_EXPECTED_EVENTS 7U

static void fail(const char *reason) __attribute__((noreturn));
static void fail(const char *reason) {
    serial_write_string("M53 USB input queue FAILED: ");
    serial_write_string(reason);
    serial_write_string("\n");
    x86_64_halt_forever();
}

static bool m53_event_matches(const struct boring_input_event *event,
                              uint32_t type, uint32_t code,
                              int32_t value1, int32_t value2,
                              uint32_t modifiers) {
    return (event != NULL) && (event->type == type) &&
           (event->code == code) && (event->value1 == value1) &&
           (event->value2 == value2) &&
           (event->modifiers == modifiers) && (event->flags == 0U);
}

static void m53_print_i32(int32_t value) {
    const int64_t wide = (int64_t)value;
    if (wide < 0) {
        serial_write_string("-");
        serial_write_u64((uint64_t)(-wide));
    } else {
        serial_write_u64((uint64_t)wide);
    }
}

static void m53_print_event(size_t index,
                            const struct boring_input_event *event) {
    serial_write_string("M53 queue event ");
    serial_write_u64((uint64_t)index);
    serial_write_string(" type=");
    serial_write_u64((uint64_t)event->type);
    serial_write_string(" code=");
    serial_write_u64((uint64_t)event->code);
    serial_write_string(" value1=");
    m53_print_i32(event->value1);
    serial_write_string(" value2=");
    m53_print_i32(event->value2);
    serial_write_string(" modifiers=");
    serial_write_u64((uint64_t)event->modifiers);
    serial_write_string("\n");
}

void xhci_address_test_run(void) {
    struct xhci_state state;
    struct boring_input_stats input_stats;
    struct boring_input_event events[BORING_INPUT_READ_MAX];
    size_t event_count = 0U;
    uint32_t completed = 0U;
    uint32_t decoded = 0U;
    uint8_t device_index;
    size_t index;

    if (!boring_input_init() ||
        (boring_input_claim(M53_INPUT_OWNER_PID) != BORING_INPUT_RESULT_OK)) {
        fail("canonical input ownership");
    }
    if (!xhci_init(&state)) { fail("controller initialization"); }
    if (!xhci_address_connected(&state)) { fail("M49 addressing"); }
    if ((state.addressed_count != M49_EXPECTED_ATTACHED_DEVICES) ||
        state.addressing_truncated) {
        fail("M49 addressed prerequisite");
    }
    if (!xhci_discover_descriptors(&state)) { fail("M50 descriptors"); }
    if (!xhci_configure_hid_devices(&state)) { fail("M51 configuration"); }

    serial_write_string("M53 USB input queue ready; inject real USB input now.\n");
    if (!xhci_poll_hid_reports(&state, M53_EXPECTED_COMPLETIONS)) {
        fail("real Interrupt-IN transfer completion");
    }

    for (device_index = 0U; device_index < state.addressed_count;
         ++device_index) {
        const struct xhci_addressed_device *device =
            &state.addressed[device_index];
        uint8_t endpoint_index;
        for (endpoint_index = 0U;
             endpoint_index < device->hid_configuration.endpoint_count;
             ++endpoint_index) {
            const struct xhci_hid_endpoint_runtime *runtime =
                &device->hid_runtime[endpoint_index];
            if ((UINT32_MAX - completed < runtime->completed_transfers) ||
                (UINT32_MAX - decoded < runtime->decoded_reports)) {
                fail("transport counter overflow");
            }
            completed += runtime->completed_transfers;
            decoded += runtime->decoded_reports;
        }
    }
    if ((completed != M53_EXPECTED_COMPLETIONS) ||
        (decoded != M53_EXPECTED_COMPLETIONS)) {
        fail("real M52 transport accounting");
    }
    if (!boring_input_get_stats(&input_stats)) {
    fail("canonical queue stats");
}
serial_write_string("M53 queue precheck queued=");
serial_write_u64((uint64_t)input_stats.queued_events);
serial_write_string(" dropped=");
serial_write_u64(input_stats.dropped_events);
serial_write_string(" modifiers=");
serial_write_u64((uint64_t)input_stats.modifiers);
serial_write_string(" owner=");
serial_write_u64(input_stats.owner_pid);
serial_write_string(" owned=");
serial_write_u64(input_stats.owned ? 1ULL : 0ULL);
serial_write_string(" initialized=");
serial_write_u64(input_stats.initialized ? 1ULL : 0ULL);
serial_write_string("\n");
if (!input_stats.initialized || !input_stats.owned ||
    (input_stats.owner_pid != M53_INPUT_OWNER_PID) ||
    (input_stats.dropped_events != 0ULL) ||
    (input_stats.queued_events != M53_EXPECTED_EVENTS) ||
    (input_stats.modifiers != 0U)) {
    fail("canonical queue state");
}
    if (boring_input_read(M53_INPUT_OWNER_PID, events,
                          BORING_INPUT_READ_MAX, &event_count) !=
            BORING_INPUT_RESULT_OK ||
        (event_count != M53_EXPECTED_EVENTS)) {
        fail("canonical queue read");
    }

    if (!m53_event_matches(&events[0], BORING_INPUT_EVENT_KEY,
                           BORING_KEY_LEFT_SUPER, BORING_KEY_DOWN_VALUE,
                           0, BORING_MOD_SUPER) ||
        !m53_event_matches(&events[1], BORING_INPUT_EVENT_KEY,
                           BORING_KEY_A, BORING_KEY_DOWN_VALUE,
                           0, BORING_MOD_SUPER) ||
        !m53_event_matches(&events[2], BORING_INPUT_EVENT_MOUSE_MOVE,
                           0U, 2345, 3456, BORING_MOD_SUPER) ||
        !m53_event_matches(&events[3], BORING_INPUT_EVENT_MOUSE_BUTTON,
                           BORING_MOUSE_BUTTON_LEFT, 1, 0,
                           BORING_MOD_SUPER) ||
        !m53_event_matches(&events[4], BORING_INPUT_EVENT_KEY,
                           BORING_KEY_A, BORING_KEY_UP_VALUE,
                           0, BORING_MOD_SUPER) ||
        !m53_event_matches(&events[5], BORING_INPUT_EVENT_MOUSE_BUTTON,
                           BORING_MOUSE_BUTTON_LEFT, 0, 0,
                           BORING_MOD_SUPER) ||
        !m53_event_matches(&events[6], BORING_INPUT_EVENT_KEY,
                           BORING_KEY_LEFT_SUPER, BORING_KEY_UP_VALUE,
                           0, 0U)) {
        fail("canonical event ordering");
    }
    if (!boring_input_get_stats(&input_stats) ||
        (input_stats.queued_events != 0U) ||
        (input_stats.modifiers != 0U)) {
        fail("queue drain state");
    }

    serial_write_string("M53 real USB Transfer completions: ");
    serial_write_u64((uint64_t)completed);
    serial_write_string("\nM53 decoded HID reports: ");
    serial_write_u64((uint64_t)decoded);
    serial_write_string("\nM53 canonical input events: ");
    serial_write_u64((uint64_t)event_count);
    serial_write_string("\n");
    for (index = 0U; index < event_count; ++index) {
        m53_print_event(index, &events[index]);
    }
    if (boring_input_release(M53_INPUT_OWNER_PID) != BORING_INPUT_RESULT_OK) {
        fail("canonical input release");
    }
    serial_write_string("M53 real USB input queue QEMU passed.\n");
    x86_64_halt_forever();
}

#elif defined(BORING_M52_HID_ACCEPTANCE)

static void fail(const char *reason) __attribute__((noreturn));
static void fail(const char *reason) {
    serial_write_string("M52 xHCI HID reports FAILED: ");
    serial_write_string(reason);
    serial_write_string("\n");
    x86_64_halt_forever();
}

void xhci_address_test_run(void) {
    struct xhci_state state;
    uint32_t submitted = 0U;
    uint32_t completed = 0U;
    uint32_t report_bytes = 0U;
    uint32_t decoded = 0U;
    uint32_t key_presses = 0U;
    uint32_t key_releases = 0U;
    uint32_t pointer_reports = 0U;
    uint8_t index;

    if (!xhci_init(&state)) { fail("controller initialization"); }
    if (!xhci_address_connected(&state)) { fail("M49 addressing"); }
    if ((state.addressed_count != M49_EXPECTED_ATTACHED_DEVICES) ||
        state.addressing_truncated) {
        fail("M49 addressed prerequisite");
    }
    if (!xhci_discover_descriptors(&state)) { fail("M50 descriptors"); }
    if (!xhci_configure_hid_devices(&state)) { fail("M51 configuration"); }

    serial_write_string("M52 USB HID transfers ready; inject real USB input now.\n");
    if (!xhci_poll_hid_reports(&state, 3U)) {
        fail("real Interrupt-IN transfer completion");
    }

    for (index = 0U; index < state.addressed_count; ++index) {
        const struct xhci_addressed_device *device = &state.addressed[index];
        uint8_t endpoint_index;
        if (!device->addressed || !device->descriptors_ready ||
            !device->device_configured || !device->hid_endpoint_ready) {
            fail("inherited configured state");
        }
        for (endpoint_index = 0U;
             endpoint_index < device->hid_configuration.endpoint_count;
             ++endpoint_index) {
            const struct xhci_hid_endpoint_descriptor *endpoint =
                &device->hid_configuration.endpoints[endpoint_index];
            const struct xhci_hid_endpoint_runtime *runtime =
                &device->hid_runtime[endpoint_index];
            if ((runtime->report_buffer_physical == 0ULL) ||
                (runtime->submitted_transfers == 0U) ||
                (runtime->completed_transfers == 0U) ||
                (runtime->decoded_reports != runtime->completed_transfers) ||
                (runtime->report_bytes == 0U) ||
                (runtime->last_report_length == 0U) ||
                (runtime->last_report_length > endpoint->max_packet) ||
                (runtime->submitted_transfers < runtime->completed_transfers)) {
                fail("bounded endpoint report state");
            }
            if ((UINT32_MAX - submitted < runtime->submitted_transfers) ||
                (UINT32_MAX - completed < runtime->completed_transfers) ||
                (UINT32_MAX - report_bytes < runtime->report_bytes) ||
                (UINT32_MAX - decoded < runtime->decoded_reports) ||
                (UINT32_MAX - key_presses < runtime->key_presses) ||
                (UINT32_MAX - key_releases < runtime->key_releases) ||
                (UINT32_MAX - pointer_reports < runtime->pointer_reports)) {
                fail("counter overflow");
            }
            submitted += runtime->submitted_transfers;
            completed += runtime->completed_transfers;
            report_bytes += runtime->report_bytes;
            decoded += runtime->decoded_reports;
            key_presses += runtime->key_presses;
            key_releases += runtime->key_releases;
            pointer_reports += runtime->pointer_reports;

            serial_write_string("M52 HID report slot=");
            serial_write_u64((uint64_t)device->slot_id);
            serial_write_string(" endpoint_id=");
            serial_write_u64((uint64_t)endpoint->endpoint_id);
            serial_write_string(" protocol=");
            serial_write_u64((uint64_t)endpoint->protocol);
            serial_write_string(" submitted=");
            serial_write_u64((uint64_t)runtime->submitted_transfers);
            serial_write_string(" completed=");
            serial_write_u64((uint64_t)runtime->completed_transfers);
            serial_write_string(" bytes=");
            serial_write_u64((uint64_t)runtime->report_bytes);
            serial_write_string(" short=");
            serial_write_u64((uint64_t)runtime->short_packets);
            serial_write_string("\n");

            if (endpoint->protocol == 1U) {
                serial_write_string("M52 keyboard transitions presses=");
                serial_write_u64((uint64_t)runtime->key_presses);
                serial_write_string(" releases=");
                serial_write_u64((uint64_t)runtime->key_releases);
                serial_write_string(" last_usage=");
                serial_write_u64((uint64_t)runtime->last_key_usage);
                serial_write_string(" last_down=");
                serial_write_u64(runtime->last_key_down ? 1ULL : 0ULL);
                serial_write_string("\n");
            } else if ((endpoint->protocol == 0U) ||
                       (endpoint->protocol == 2U)) {
                if (!runtime->pointer_valid) {
                    fail("pointer report was not decoded");
                }
                serial_write_string("M52 pointer report x=");
                serial_write_u64((uint64_t)runtime->last_pointer_x);
                serial_write_string(" y=");
                serial_write_u64((uint64_t)runtime->last_pointer_y);
                serial_write_string(" buttons=");
                serial_write_u64((uint64_t)runtime->last_pointer_buttons);
                serial_write_string("\n");
            }
        }
    }
    if ((completed < 3U) || (decoded != completed) ||
        (submitted < completed) || (report_bytes == 0U) ||
        (key_presses == 0U) || (key_releases == 0U) ||
        (pointer_reports == 0U)) {
        fail("dynamic decoded input evidence");
    }
    serial_write_string("M52 real Interrupt-IN submissions: ");
    serial_write_u64((uint64_t)submitted);
    serial_write_string("\nM52 real Interrupt-IN completions: ");
    serial_write_u64((uint64_t)completed);
    serial_write_string("\nM52 real HID report bytes: ");
    serial_write_u64((uint64_t)report_bytes);
    serial_write_string("\nM52 decoded HID reports: ");
    serial_write_u64((uint64_t)decoded);
    serial_write_string("\nM52 xHCI HID interrupt-IN QEMU passed.\n");
    x86_64_halt_forever();
}

#elif defined(BORING_M51_CONFIGURATION_ACCEPTANCE)

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
