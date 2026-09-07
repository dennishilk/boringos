#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define BORING_USB_HID_RUNTIME_HOST_TEST 1
#include "../kernel/core/usb_hid_impl.inc"

#define TEST_REPORT_PHYSICAL 0x3000ULL
#define TEST_RING_PHYSICAL 0x4000ULL
#define TEST_SLOT 2U
#define TEST_ENDPOINT 3U

static uint8_t report_page[PMM_PAGE_SIZE];
static unsigned input_events;
static unsigned input_irqs;
static int failures;

bool vmm_pmm_frame_to_hhdm(uint64_t physical_address,
                           void **virtual_address) {
    if ((virtual_address == NULL) ||
        (physical_address != TEST_REPORT_PHYSICAL)) {
        return false;
    }
    *virtual_address = report_page;
    return true;
}

bool boring_input_submit_key(uint32_t code, bool down) {
    (void)down;
    if (code == (uint32_t)BORING_KEY_NONE) { return false; }
    ++input_events;
    return true;
}

bool boring_input_submit_mouse_move(int32_t dx, int32_t dy) {
    if ((dx == 0) && (dy == 0)) { return true; }
    ++input_events;
    return true;
}

bool boring_input_submit_mouse_button(uint32_t button, bool down) {
    (void)button;
    (void)down;
    ++input_events;
    return true;
}

void boring_event_input_irq(void) {
    ++input_irqs;
}

static void expect(bool condition, const char *name) {
    if (!condition) {
        (void)fprintf(stderr, "M61 HID completion FAILED: %s\n", name);
        ++failures;
    }
}

static struct xhci_trb transfer_event(uint64_t trb, uint16_t residual) {
    struct xhci_trb event = {0};
    event.parameter = trb;
    event.status = ((uint32_t)XHCI_COMPLETION_SUCCESS << 24U) |
                   (uint32_t)residual;
    event.control = ((uint32_t)XHCI_TRB_TYPE_TRANSFER_EVENT << 10U) |
                    ((uint32_t)TEST_ENDPOINT << 16U) |
                    ((uint32_t)TEST_SLOT << 24U);
    return event;
}

static struct xhci_hid_endpoint_runtime *prepare_keyboard(
    struct xhci_state *state, uint64_t expected_trb) {
    struct xhci_addressed_device *device;
    struct xhci_hid_endpoint_runtime *runtime;

    *state = (struct xhci_state){0};
    state->addressed_count = 1U;
    device = &state->addressed[0];
    device->slot_id = TEST_SLOT;
    device->device_configured = true;
    device->hid_endpoint_ready = true;
    device->hid_ring_physical[0] = TEST_RING_PHYSICAL;
    device->hid_configuration.endpoint_count = 1U;
    device->hid_configuration.endpoints[0].endpoint_id = TEST_ENDPOINT;
    device->hid_configuration.endpoints[0].max_packet =
        USB_HID_BOOT_KEY_REPORT_SIZE;
    device->hid_configuration.endpoints[0].report_format =
        XHCI_HID_REPORT_BOOT_KEYBOARD;
    runtime = &device->hid_runtime[0];
    runtime->report_buffer_physical = TEST_REPORT_PHYSICAL;
    runtime->expected_trb_physical = expected_trb;
    runtime->transfer_outstanding = true;
    return runtime;
}

int main(void) {
    struct xhci_state state;
    struct xhci_hid_endpoint_runtime *runtime;
    struct xhci_trb event;
    uint32_t completed;
    uint64_t expected;
    size_t index;

    for (index = 0U; index < sizeof(report_page); ++index) {
        report_page[index] = 0U;
    }
    expected = TEST_RING_PHYSICAL + XHCI_TRB_SIZE;
    runtime = prepare_keyboard(&state, expected);
    report_page[2] = 4U;
    report_page[3] = 4U;
    event = transfer_event(expected, 0U);
    completed = 0U;
    expect(!m52_complete_event(&state, &event, &completed),
           "invalid report rejected after valid Transfer Event");
    expect(runtime->transfer_outstanding &&
           (runtime->expected_trb_physical == expected) &&
           (runtime->completed_transfers == 0U) &&
           (runtime->report_bytes == 0U) &&
           (runtime->decoded_reports == 0U) &&
           (state.addressed[0].transfer_events == 0U) &&
           (completed == 0U),
           "decode failure leaves endpoint completion transaction untouched");
    expect((input_events == 0U) && (input_irqs == 0U),
           "invalid report publishes no input");

    report_page[2] = 0U;
    report_page[3] = 0U;
    expect(m52_complete_event(&state, &event, &completed),
           "valid unchanged keyboard report completes");
    expect(!runtime->transfer_outstanding &&
           (runtime->expected_trb_physical == 0ULL) &&
           (runtime->completed_transfers == 1U) &&
           (runtime->report_bytes == USB_HID_BOOT_KEY_REPORT_SIZE) &&
           (runtime->decoded_reports == 1U) &&
           (state.addressed[0].transfer_events == 1U) &&
           (completed == 1U),
           "unchanged report is consumed without a semantic transition");
    expect((input_events == 0U) && (input_irqs == 0U),
           "unchanged report queues no input event");

    expected = TEST_RING_PHYSICAL + (2ULL * XHCI_TRB_SIZE);
    runtime->expected_trb_physical = expected;
    runtime->transfer_outstanding = true;
    report_page[2] = 4U;
    event = transfer_event(expected, 0U);
    expect(m52_complete_event(&state, &event, &completed),
           "valid key transition completes");
    expect((runtime->completed_transfers == 2U) &&
           (runtime->decoded_reports == 2U) &&
           (runtime->key_presses == 1U) &&
           (state.addressed[0].transfer_events == 2U) &&
           (completed == 2U) &&
           (input_events == 1U) && (input_irqs == 1U),
           "real key transition reaches input submission");

    input_events = 0U;
    input_irqs = 0U;
    expected = TEST_RING_PHYSICAL + (3ULL * XHCI_TRB_SIZE);
    runtime = prepare_keyboard(&state, expected);
    state.addressed[0].hid_configuration.endpoints[0].max_packet = 4U;
    state.addressed[0].hid_configuration.endpoints[0].report_format =
        XHCI_HID_REPORT_BOOT_MOUSE;
    report_page[2] = 0U;
    event = transfer_event(expected, 0U);
    completed = 0U;
    expect(m52_complete_event(&state, &event, &completed) &&
           (runtime->pointer_reports == 1U) && (completed == 1U),
           "valid unchanged mouse report completes");
    expect((input_events == 0U) && (input_irqs == 0U),
           "unchanged mouse report queues no input event");

    expected = TEST_RING_PHYSICAL + (4ULL * XHCI_TRB_SIZE);
    runtime->expected_trb_physical = expected;
    runtime->transfer_outstanding = true;
    report_page[1] = 5U;
    event = transfer_event(expected, 0U);
    expect(m52_complete_event(&state, &event, &completed) &&
           (runtime->pointer_reports == 2U) && (completed == 2U) &&
           (input_events == 1U) && (input_irqs == 1U),
           "real mouse movement reaches input submission");

    if (failures != 0) { return 1; }
    puts("M61 HID decode failure transaction: PASS");
    puts("M61 HID valid no-change completion: PASS");
    puts("M61 HID real key/mouse transition submission: PASS");
    return 0;
}
