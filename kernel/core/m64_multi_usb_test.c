#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/block_device.h>
#include <boring/cpu.h>
#include <boring/input.h>
#include <boring/m64_multi_usb_test.h>
#include <boring/serial.h>
#include <boring/usb_mass_storage.h>
#include <boring/vmm.h>
#include <boring/xhci.h>
#include <boring/xhci_mixed.h>

#define M64_USB_CONTROLLER_COUNT 3U
#define M64_USB_LBA 8ULL
#define M64_USB_MAX_BLOCK 4096U
#define M64_USB_INPUT_PID 64ULL
#define M64_USB_EXPECTED_EVENTS 7U
#define M64_USB_SERVICE_LIMIT 4096U

static uint8_t before[M64_USB_MAX_BLOCK];
static uint8_t after[M64_USB_MAX_BLOCK];
static uint8_t pattern[M64_USB_MAX_BLOCK];

static void fail(const char *reason) __attribute__((noreturn));
static void fail(const char *reason) {
    serial_write_string("M64 multi-USB FAILED: ");
    serial_write_string(reason);
    serial_write_string("\n");
    x86_64_halt_forever();
}

static bool bytes_equal(const uint8_t *left, const uint8_t *right,
                        size_t length) {
    size_t index;
    for (index = 0U; index < length; ++index) {
        if (left[index] != right[index]) { return false; }
    }
    return true;
}

static bool controller_has_report_format(
    const struct xhci_state *state, enum xhci_hid_report_format report_format) {
    uint8_t device_index;
    if (state == NULL) { return false; }
    for (device_index = 0U; device_index < state->addressed_count;
         ++device_index) {
        const struct xhci_addressed_device *device =
            &state->addressed[device_index];
        uint8_t endpoint_index;
        for (endpoint_index = 0U;
             endpoint_index < device->hid_configuration.endpoint_count;
             ++endpoint_index) {
            if (device->hid_configuration.endpoints[endpoint_index].report_format ==
                report_format) {
                return true;
            }
        }
    }
    return false;
}

static bool controller_has_storage(const struct xhci_state *state) {
    uint8_t device_index;
    if (state == NULL) { return false; }
    for (device_index = 0U; device_index < state->addressed_count;
         ++device_index) {
        const struct xhci_addressed_device *device =
            &state->addressed[device_index];
        struct usb_mass_storage_configuration configuration;
        void *descriptor_virtual = NULL;
        if (!device->descriptors_ready ||
            (device->descriptor_buffer_physical == 0ULL) ||
            (device->descriptors.configuration_length == 0U) ||
            !vmm_pmm_frame_to_hhdm(device->descriptor_buffer_physical,
                                   &descriptor_virtual)) {
            continue;
        }
        if (usb_mass_storage_parse_configuration(
                (const uint8_t *)descriptor_virtual,
                device->descriptors.configuration_length, device->speed,
                &configuration)) {
            return true;
        }
    }
    return false;
}

static void enumerate_controllers(void) {
    uint8_t index;
    for (index = 0U; index < xhci_controller_count(); ++index) {
        struct xhci_state *state = xhci_get_controller(index);
        if ((state == NULL) ||
            (xhci_get_controller_status(index) != XHCI_CONTROLLER_RUNNING)) {
            fail("controller registry state");
        }
        if (state->connected_ports == 0ULL) { continue; }
        if (!xhci_address_connected(state) ||
            !xhci_discover_descriptors(state) ||
            !xhci_configure_hid_devices_mixed(state)) {
            fail("per-controller address/descriptor/HID");
        }
    }
}

static void verify_cross_controller_input(uint8_t keyboard_index,
                                          uint8_t pointer_index) {
    struct boring_input_stats stats;
    struct boring_input_event events[BORING_INPUT_READ_MAX];
    size_t count = 0U;
    uint32_t attempt;
    bool keyboard_progress = false;
    bool pointer_progress = false;
    bool saw_key = false;
    bool saw_move = false;
    bool saw_down = false;
    bool saw_up = false;
    size_t index;

    if (!boring_input_init() ||
        (boring_input_claim(M64_USB_INPUT_PID) != BORING_INPUT_RESULT_OK)) {
        fail("canonical input queue ownership");
    }
    serial_write_string(
        "M64 multi-controller HID ready; inject real USB input now.\n");

    for (attempt = 0U; attempt < M64_USB_SERVICE_LIMIT; ++attempt) {
        struct xhci_state *keyboard = xhci_get_controller(keyboard_index);
        struct xhci_state *pointer = xhci_get_controller(pointer_index);
        if (!boring_input_get_stats(&stats) ||
            (stats.dropped_events != 0ULL)) {
            fail("canonical input queue stats");
        }
        if ((stats.queued_events == M64_USB_EXPECTED_EVENTS) &&
            (stats.modifiers == 0U)) {
            break;
        }
        if ((keyboard == NULL) || (pointer == NULL)) {
            fail("cross-controller HID registry");
        }

        /*
         * Multi-controller HID must be serviced cooperatively. A blocking
         * poll of controller A before controller B would leave B unarmed
         * while QMP is already delivering pointer reports.
         */
        if (xhci_service_hid_reports(keyboard)) {
            keyboard_progress = true;
        }
        if (xhci_service_hid_reports(pointer)) {
            pointer_progress = true;
        }
    }
    if (!keyboard_progress || !pointer_progress) {
        fail("cross-controller HID Interrupt-IN");
    }
    if (!boring_input_get_stats(&stats) ||
        (stats.queued_events != M64_USB_EXPECTED_EVENTS) ||
        (stats.dropped_events != 0ULL) || (stats.modifiers != 0U)) {
        fail("canonical cross-controller input end state");
    }
    if ((boring_input_read(M64_USB_INPUT_PID, events, BORING_INPUT_READ_MAX,
                           &count) != BORING_INPUT_RESULT_OK) ||
        (count != M64_USB_EXPECTED_EVENTS)) {
        fail("canonical cross-controller input read");
    }
    for (index = 0U; index < count; ++index) {
        if (events[index].type == BORING_INPUT_EVENT_KEY) {
            saw_key = true;
        } else if (events[index].type == BORING_INPUT_EVENT_MOUSE_MOVE) {
            saw_move = true;
        } else if ((events[index].type == BORING_INPUT_EVENT_MOUSE_BUTTON) &&
                   (events[index].code == BORING_MOUSE_BUTTON_LEFT)) {
            if (events[index].value1 == 1) { saw_down = true; }
            if (events[index].value1 == 0) { saw_up = true; }
        }
    }
    if (!saw_key || !saw_move || !saw_down || !saw_up) {
        fail("canonical keyboard/pointer event classes");
    }
    if (boring_input_release(M64_USB_INPUT_PID) != BORING_INPUT_RESULT_OK) {
        fail("canonical input release");
    }
    serial_write_string("CROSS_CONTROLLER_HID_INTERRUPT_IN=PASS\n");
    serial_write_string("CANONICAL_MOUSE_EVENT_ON_CONTROLLER_B=PASS\n");
}

void m64_multi_usb_test_run(void) {
    struct xhci_state primary;
    const struct block_device *usb0;
    const struct usb_mass_storage_stats *stats;
    uint64_t pointer_dequeue_before;
    uint64_t storage_dequeue_before;
    uint32_t block_size;
    uint8_t keyboard_index = UINT8_MAX;
    uint8_t pointer_index = UINT8_MAX;
    uint8_t storage_index = UINT8_MAX;
    uint8_t index;
    size_t byte_index;

    block_device_init();
    if (!xhci_init(&primary) || xhci_controller_registry_truncated() ||
        (xhci_controller_count() != M64_USB_CONTROLLER_COUNT)) {
        fail("three-controller initialization");
    }
    enumerate_controllers();

    for (index = 0U; index < xhci_controller_count(); ++index) {
        const struct xhci_state *state = xhci_get_controller(index);
        if (controller_has_report_format(
                state, XHCI_HID_REPORT_BOOT_KEYBOARD)) {
            keyboard_index = index;
        }
        if (controller_has_report_format(state, XHCI_HID_REPORT_BOOT_MOUSE) ||
            controller_has_report_format(
                state, XHCI_HID_REPORT_QEMU_ABSOLUTE_TABLET)) {
            pointer_index = index;
        }
        if (controller_has_storage(state)) { storage_index = index; }
    }
    if ((keyboard_index == UINT8_MAX) || (pointer_index == UINT8_MAX) ||
        (storage_index == UINT8_MAX) ||
        (keyboard_index != storage_index) ||
        (pointer_index == storage_index)) {
        fail("QEMU topology classification");
    }

    pointer_dequeue_before =
        xhci_get_controller(pointer_index)->event_dequeue_count;
    storage_dequeue_before =
        xhci_get_controller(storage_index)->event_dequeue_count;

    if (!usb_mass_storage_init(&primary)) {
        fail("controller-bound usb0 initialization");
    }
    usb0 = usb_mass_storage_get_block_device();
    stats = usb_mass_storage_get_stats();
    if ((usb0 == NULL) || (stats == NULL) || !stats->registered ||
        !stats->configured || (stats->controller_index != storage_index) ||
        (block_device_find("usb0") != usb0)) {
        fail("usb0 ownership/registration");
    }
    block_size = stats->logical_block_size;
    if ((block_size == 0U) || (block_size > M64_USB_MAX_BLOCK) ||
        (stats->block_count <= M64_USB_LBA)) {
        fail("usb0 bounded geometry");
    }

    if (block_device_read(usb0, M64_USB_LBA, 1U, before) !=
        BLOCK_DEVICE_RESULT_OK) {
        fail("usb0 READ(10) before write");
    }
    for (byte_index = 0U; byte_index < (size_t)block_size; ++byte_index) {
        pattern[byte_index] =
            (uint8_t)(0x64U + (uint8_t)(byte_index % 29U));
    }
    if (bytes_equal(before, pattern, (size_t)block_size)) {
        pattern[0] ^= 0x5aU;
    }
    if (block_device_write(usb0, M64_USB_LBA, 1U, pattern) !=
            BLOCK_DEVICE_RESULT_OK ||
        block_device_read(usb0, M64_USB_LBA, 1U, after) !=
            BLOCK_DEVICE_RESULT_OK ||
        !bytes_equal(after, pattern, (size_t)block_size)) {
        fail("WRITE(10)/flush/readback");
    }
    stats = usb_mass_storage_get_stats();
    if ((stats == NULL) || (stats->write_commands == 0U) ||
        (stats->flush_commands == 0U) ||
        stats->force_unit_access_writes ||
        stats->synchronize_cache_unsupported ||
        (stats->flush_diagnostic != (uint8_t)USB_FLUSH_NONE)) {
        fail("M63 normal flush policy regression");
    }
    if ((xhci_get_controller(pointer_index)->event_dequeue_count !=
         pointer_dequeue_before) ||
        (xhci_get_controller(storage_index)->event_dequeue_count <=
         storage_dequeue_before)) {
        fail("storage/HID Event Ring ownership");
    }

    serial_write_string("KEYBOARD_ON_CONTROLLER_A=PASS\n");
    serial_write_string("STORAGE_ON_CONTROLLER_A=PASS\n");
    serial_write_string("HID_ON_CONTROLLER_B=PASS\n");
    serial_write_string("USB_STORAGE_REGRESSION=PASS\n");
    serial_write_string("M63_FLUSH_POLICY_REGRESSION=PASS\n");
    serial_write_string("CROSS_CONTROLLER_EVENT_OWNERSHIP=PASS\n");

    verify_cross_controller_input(keyboard_index, pointer_index);

    block_device_init();
    usb_mass_storage_cleanup();
    serial_write_string("M64 multi-controller USB storage/HID passed.\n");
    x86_64_halt_forever();
}
