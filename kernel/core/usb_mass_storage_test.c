#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/block_device.h>
#include <boring/cpu.h>
#include <boring/input.h>
#include <boring/serial.h>
#include <boring/usb_mass_storage.h>
#include <boring/usb_mass_storage_test.h>
#include <boring/xhci.h>
#include <boring/xhci_mixed.h>

#define M60_TEST_LBA 8ULL
#define M60_MAX_BLOCK_BYTES 4096U
#define M60_INPUT_OWNER_PID 60ULL
#define M60_EXPECTED_EVENTS 7U
#define M60_MAX_HID_COMPLETIONS 16U

static uint8_t neighbor_before[M60_MAX_BLOCK_BYTES];
static uint8_t neighbor_next_before[M60_MAX_BLOCK_BYTES];
static uint8_t target_before[M60_MAX_BLOCK_BYTES];
static uint8_t neighbor_after[M60_MAX_BLOCK_BYTES];
static uint8_t target_after[M60_MAX_BLOCK_BYTES];
static uint8_t write_pattern[M60_MAX_BLOCK_BYTES];

static void fail(const char *reason) __attribute__((noreturn));
static void fail(const char *reason) {
    serial_write_string("M60 USB MASS STORAGE TEST FAILED: ");
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

static bool input_event_matches(const struct boring_input_event *event,
                                uint32_t type, uint32_t code,
                                int32_t value1, int32_t value2,
                                uint32_t modifiers) {
    return (event != NULL) && (event->type == type) &&
           (event->code == code) && (event->value1 == value1) &&
           (event->value2 == value2) &&
           (event->modifiers == modifiers) && (event->flags == 0U);
}

static void make_pattern(uint8_t *bytes, size_t length) {
    size_t index;
    for (index = 0U; index < length; ++index) {
        bytes[index] = (uint8_t)(0x60U + (uint8_t)(index % 31U));
    }
}

static void csw_fixture(uint8_t bytes[USB_MASS_STORAGE_CSW_SIZE],
                        uint32_t signature, uint32_t tag,
                        uint32_t residue, uint8_t status) {
    size_t index;
    for (index = 0U; index < USB_MASS_STORAGE_CSW_SIZE; ++index) {
        bytes[index] = 0U;
    }
    bytes[0] = (uint8_t)signature;
    bytes[1] = (uint8_t)(signature >> 8U);
    bytes[2] = (uint8_t)(signature >> 16U);
    bytes[3] = (uint8_t)(signature >> 24U);
    bytes[4] = (uint8_t)tag;
    bytes[5] = (uint8_t)(tag >> 8U);
    bytes[6] = (uint8_t)(tag >> 16U);
    bytes[7] = (uint8_t)(tag >> 24U);
    bytes[8] = (uint8_t)residue;
    bytes[9] = (uint8_t)(residue >> 8U);
    bytes[10] = (uint8_t)(residue >> 16U);
    bytes[11] = (uint8_t)(residue >> 24U);
    bytes[12] = status;
}

static void validate_sense_parsing(void) {
    uint8_t fixed[18] = {0};
    uint8_t descriptor[4] = {0};
    struct usb_mass_storage_sense sense;

    fixed[0] = 0x70U;
    fixed[2] = 0x05U;
    fixed[7] = 10U;
    fixed[12] = 0x20U;
    fixed[13] = 0x00U;
    if (!usb_mass_storage_parse_sense(fixed, sizeof(fixed), &sense) ||
        (sense.response_code != 0x70U) || (sense.sense_key != 0x05U) ||
        (sense.asc != 0x20U) || (sense.ascq != 0x00U)) {
        fail("fixed REQUEST SENSE parser");
    }

    descriptor[0] = 0x72U;
    descriptor[1] = 0x03U;
    descriptor[2] = 0x11U;
    descriptor[3] = 0x22U;
    if (!usb_mass_storage_parse_sense(descriptor, sizeof(descriptor), &sense) ||
        (sense.response_code != 0x72U) || (sense.sense_key != 0x03U) ||
        (sense.asc != 0x11U) || (sense.ascq != 0x22U)) {
        fail("descriptor REQUEST SENSE parser");
    }

    fixed[0] = 0x74U;
    if (usb_mass_storage_parse_sense(fixed, sizeof(fixed), &sense) ||
        usb_mass_storage_parse_sense(descriptor, 3U, &sense)) {
        fail("invalid REQUEST SENSE accepted");
    }
    serial_write_string("M63 REQUEST SENSE parser: PASS\n");
}

static void validate_csw_rejections(void) {
    uint8_t csw[USB_MASS_STORAGE_CSW_SIZE];
    uint32_t residue = 0U;
    uint8_t status = 0U;

    csw_fixture(csw, USB_MASS_STORAGE_CSW_SIGNATURE, 7U, 0U, 0U);
    if (!usb_mass_storage_validate_csw(csw, sizeof(csw), 7U, 512U,
                                       &residue, &status) ||
        (residue != 0U) || (status != 0U)) {
        fail("valid CSW fixture");
    }
    csw_fixture(csw, 0x11111111U, 7U, 0U, 0U);
    if (usb_mass_storage_validate_csw(csw, sizeof(csw), 7U, 512U,
                                      &residue, &status)) {
        fail("bad CSW signature accepted");
    }
    serial_write_string("M60 bad CSW signature: REJECTED\n");

    csw_fixture(csw, USB_MASS_STORAGE_CSW_SIGNATURE, 8U, 0U, 0U);
    if (usb_mass_storage_validate_csw(csw, sizeof(csw), 7U, 512U,
                                      &residue, &status)) {
        fail("bad CSW tag accepted");
    }
    serial_write_string("M60 bad CSW tag: REJECTED\n");

    csw_fixture(csw, USB_MASS_STORAGE_CSW_SIGNATURE, 7U, 513U, 0U);
    if (usb_mass_storage_validate_csw(csw, sizeof(csw), 7U, 512U,
                                      &residue, &status)) {
        fail("bad CSW residue accepted");
    }
    csw_fixture(csw, USB_MASS_STORAGE_CSW_SIGNATURE, 7U, 0U, 2U);
    if (usb_mass_storage_validate_csw(csw, sizeof(csw), 7U, 512U,
                                      &residue, &status)) {
        fail("bad CSW status accepted");
    }
    serial_write_string("M60 bad CSW residue/status: REJECTED\n");
}

static uint32_t configured_hid_devices(const struct xhci_state *state) {
    uint8_t index;
    uint32_t count = 0U;
    for (index = 0U; index < state->addressed_count; ++index) {
        if (state->addressed[index].hid_endpoint_ready) { ++count; }
    }
    return count;
}

static void verify_hid_queue_after_storage(struct xhci_state *state) {
    struct boring_input_stats stats;
    struct boring_input_event events[BORING_INPUT_READ_MAX];
    size_t event_count = 0U;
    uint32_t poll_count;

    if (!boring_input_init() ||
        (boring_input_claim(M60_INPUT_OWNER_PID) != BORING_INPUT_RESULT_OK)) {
        fail("canonical HID input ownership");
    }
    serial_write_string(
        "M60 HID input queue ready after storage; inject real USB input now.\n");

    for (poll_count = 0U; poll_count < M60_MAX_HID_COMPLETIONS; ++poll_count) {
        if (!xhci_poll_hid_reports(state, 1U)) {
            fail("post-storage HID Interrupt-IN completion");
        }
        if (!boring_input_get_stats(&stats) || !stats.initialized ||
            !stats.owned || (stats.owner_pid != M60_INPUT_OWNER_PID) ||
            (stats.dropped_events != 0ULL) ||
            (stats.queued_events > M60_EXPECTED_EVENTS)) {
            fail("post-storage bounded canonical HID queue");
        }
        if ((stats.queued_events == M60_EXPECTED_EVENTS) &&
            (stats.modifiers == 0U)) {
            break;
        }
    }
    if (poll_count == M60_MAX_HID_COMPLETIONS) {
        fail("post-storage canonical HID queue end state");
    }
    if (!boring_input_get_stats(&stats) ||
        (stats.queued_events != M60_EXPECTED_EVENTS) ||
        (stats.dropped_events != 0ULL) || (stats.modifiers != 0U)) {
        fail("post-storage canonical HID queue state");
    }
    serial_write_string("M60 canonical HID queue precheck queued=");
    serial_write_u64((uint64_t)stats.queued_events);
    serial_write_string(" dropped=");
    serial_write_u64(stats.dropped_events);
    serial_write_string(" modifiers=");
    serial_write_u64((uint64_t)stats.modifiers);
    serial_write_string("\n");

    if ((boring_input_read(M60_INPUT_OWNER_PID, events,
                           BORING_INPUT_READ_MAX, &event_count) !=
         BORING_INPUT_RESULT_OK) ||
        (event_count != M60_EXPECTED_EVENTS)) {
        fail("post-storage canonical HID queue read");
    }
    if (!input_event_matches(&events[0], BORING_INPUT_EVENT_KEY,
                             BORING_KEY_LEFT_SUPER, BORING_KEY_DOWN_VALUE,
                             0, BORING_MOD_SUPER) ||
        !input_event_matches(&events[1], BORING_INPUT_EVENT_KEY,
                             BORING_KEY_A, BORING_KEY_DOWN_VALUE,
                             0, BORING_MOD_SUPER) ||
        !input_event_matches(&events[2], BORING_INPUT_EVENT_MOUSE_MOVE,
                             0U, 2345, 3456, BORING_MOD_SUPER) ||
        !input_event_matches(&events[3], BORING_INPUT_EVENT_MOUSE_BUTTON,
                             BORING_MOUSE_BUTTON_LEFT, 1, 0,
                             BORING_MOD_SUPER) ||
        !input_event_matches(&events[4], BORING_INPUT_EVENT_KEY,
                             BORING_KEY_A, BORING_KEY_UP_VALUE,
                             0, BORING_MOD_SUPER) ||
        !input_event_matches(&events[5], BORING_INPUT_EVENT_MOUSE_BUTTON,
                             BORING_MOUSE_BUTTON_LEFT, 0, 0,
                             BORING_MOD_SUPER) ||
        !input_event_matches(&events[6], BORING_INPUT_EVENT_KEY,
                             BORING_KEY_LEFT_SUPER, BORING_KEY_UP_VALUE,
                             0, 0U)) {
        fail("post-storage canonical HID event ordering");
    }
    if (!boring_input_get_stats(&stats) || (stats.queued_events != 0U) ||
        (stats.dropped_events != 0ULL) || (stats.modifiers != 0U)) {
        fail("post-storage canonical HID queue drain");
    }
    serial_write_string("M60 canonical HID events after storage: 7\n");
    serial_write_string("M60 canonical HID ordering after storage: PASS\n");
    serial_write_string("M60 canonical HID dropped after storage: 0\n");
    if (boring_input_release(M60_INPUT_OWNER_PID) != BORING_INPUT_RESULT_OK) {
        fail("canonical HID input release");
    }
}

void usb_mass_storage_test_run(void) {
    struct xhci_state state;
    const struct block_device *usb0;
    const struct usb_mass_storage_stats *stats;
    enum block_device_result result;
    uint32_t block_size;
    uint32_t hid_count;

    validate_sense_parsing();
    validate_csw_rejections();
    block_device_init();
    if (!xhci_init(&state)) { fail("xHCI initialization"); }
    if (!xhci_address_connected(&state)) { fail("USB addressing"); }
    if (!xhci_discover_descriptors(&state)) { fail("descriptor discovery"); }

    if (!xhci_configure_hid_devices_mixed(&state)) {
        fail("mixed-class HID configuration");
    }
    hid_count = configured_hid_devices(&state);
    if (hid_count != 2U) { fail("HID coexistence configuration"); }
    serial_write_string("M60 keyboard/tablet descriptor coexistence: PASS\n");

    if (!usb_mass_storage_init(&state)) { fail("usb0 initialization"); }
    usb0 = usb_mass_storage_get_block_device();
    stats = usb_mass_storage_get_stats();
    if ((usb0 == NULL) || (stats == NULL) ||
        (block_device_find("usb0") != usb0) || !stats->registered ||
        !stats->configured) {
        fail("M21 usb0 registration");
    }
    if ((stats->bulk_in_address & 0x80U) == 0U ||
        (stats->bulk_out_address & 0x80U) != 0U ||
        (stats->bulk_in_max_packet == 0U) ||
        (stats->bulk_out_max_packet == 0U)) {
        fail("descriptor-derived bulk endpoints");
    }
    serial_write_string("M60 USB mass-storage interface detected: PASS\n");
    serial_write_string("M60 descriptor-derived Bulk OUT: PASS endpoint=");
    serial_write_hex_u64((uint64_t)stats->bulk_out_address);
    serial_write_string(" max_packet=");
    serial_write_u64((uint64_t)stats->bulk_out_max_packet);
    serial_write_string("\nM60 descriptor-derived Bulk IN: PASS endpoint=");
    serial_write_hex_u64((uint64_t)stats->bulk_in_address);
    serial_write_string(" max_packet=");
    serial_write_u64((uint64_t)stats->bulk_in_max_packet);
    serial_write_string("\nM60 Configure Endpoint: PASS\n");
    serial_write_string("M60 CBW transport: PASS\n");
    serial_write_string("M60 valid CSW: PASS\n");
    serial_write_string("M60 SCSI INQUIRY: PASS\n");
    serial_write_string("M60 TEST UNIT READY: PASS\n");
    serial_write_string("M60 READ CAPACITY(10): PASS\n");
    serial_write_string("M60 logical-sector size: ");
    serial_write_u64((uint64_t)stats->logical_block_size);
    serial_write_string("\nM60 capacity bytes: ");
    serial_write_u64(stats->byte_capacity);
    serial_write_string("\nM60 block count: ");
    serial_write_u64(stats->block_count);
    serial_write_string("\n");

    block_size = stats->logical_block_size;
    if ((block_size == 0U) || (block_size > M60_MAX_BLOCK_BYTES) ||
        (stats->block_count <= M60_TEST_LBA + 1ULL)) {
        fail("bounded geometry");
    }

    result = block_device_read(usb0, M60_TEST_LBA - 1ULL, 1U,
                               neighbor_before);
    if (result != BLOCK_DEVICE_RESULT_OK) { fail("neighbor read before"); }
    result = block_device_read(usb0, M60_TEST_LBA, 1U, target_before);
    if (result != BLOCK_DEVICE_RESULT_OK) { fail("target read before"); }
    result = block_device_read(usb0, M60_TEST_LBA + 1ULL, 1U,
                               neighbor_next_before);
    if (result != BLOCK_DEVICE_RESULT_OK) {
        fail("following neighbor read before");
    }

    make_pattern(write_pattern, (size_t)block_size);
    if (bytes_equal(write_pattern, target_before, (size_t)block_size)) {
        write_pattern[0] ^= 0x5aU;
    }
    result = block_device_write(usb0, M60_TEST_LBA, 1U, write_pattern);
    if (result != BLOCK_DEVICE_RESULT_OK) { fail("WRITE(10)"); }
    serial_write_string("M60 WRITE(10): PASS lba=8\n");
    serial_write_string("M60 SYNCHRONIZE CACHE(10): PASS\n");

    result = block_device_read(usb0, M60_TEST_LBA, 1U, target_after);
    if ((result != BLOCK_DEVICE_RESULT_OK) ||
        !bytes_equal(target_after, write_pattern, (size_t)block_size)) {
        fail("READ(10) writeback verification");
    }
    serial_write_string("M60 READ(10): PASS\n");

    result = block_device_read(usb0, M60_TEST_LBA - 1ULL, 1U,
                               neighbor_after);
    if ((result != BLOCK_DEVICE_RESULT_OK) ||
        !bytes_equal(neighbor_before, neighbor_after, (size_t)block_size)) {
        fail("neighboring sector before changed");
    }
    serial_write_string("M60 neighboring sector before: unchanged\n");

    result = block_device_read(usb0, M60_TEST_LBA + 1ULL, 1U,
                               neighbor_after);
    if ((result != BLOCK_DEVICE_RESULT_OK) ||
        !bytes_equal(neighbor_next_before, neighbor_after, (size_t)block_size)) {
        fail("neighboring sector after changed");
    }
    serial_write_string("M60 neighboring sector after: unchanged\n");

    result = block_device_read(usb0, stats->block_count, 1U, target_after);
    if (result != BLOCK_DEVICE_RESULT_OUT_OF_RANGE) {
        fail("out-of-range read accepted");
    }
    serial_write_string("M60 out-of-range read: REJECTED\n");
    result = block_device_write(usb0, stats->block_count, 1U, write_pattern);
    if (result != BLOCK_DEVICE_RESULT_OUT_OF_RANGE) {
        fail("out-of-range write accepted");
    }
    serial_write_string("M60 out-of-range write: REJECTED\n");

    stats = usb_mass_storage_get_stats();
    if ((stats == NULL) || (stats->bot_commands < 8U) ||
        (stats->bulk_in_transfers == 0U) ||
        (stats->bulk_out_transfers == 0U) ||
        (stats->read_commands < 5U) || (stats->write_commands != 1U) ||
        (stats->flush_commands != 1U)) {
        fail("transport counters");
    }
    serial_write_string("M60 BOT commands: ");
    serial_write_u64((uint64_t)stats->bot_commands);
    serial_write_string(" bulk_in=");
    serial_write_u64((uint64_t)stats->bulk_in_transfers);
    serial_write_string(" bulk_out=");
    serial_write_u64((uint64_t)stats->bulk_out_transfers);
    serial_write_string("\n");

    verify_hid_queue_after_storage(&state);
    serial_write_string("M60 HID coexistence: PASS (usb-kbd + usb-tablet + usb0)\n");

    /* Clear M21's registry before releasing backend-owned DMA. */
    block_device_init();
    usb_mass_storage_cleanup();
    if ((usb_mass_storage_get_block_device() != NULL) ||
        (usb_mass_storage_get_stats() != NULL)) {
        fail("cleanup");
    }
    serial_write_string("M60 cleanup: PASS\n");
    serial_write_string("M60 USB MASS STORAGE TEST PASSED\n");
    x86_64_halt_forever();
}
