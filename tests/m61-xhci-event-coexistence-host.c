#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <boring/xhci.h>

#define TEST_STORAGE_SLOT 1U
#define TEST_STORAGE_ENDPOINT 4U
#define TEST_STORAGE_RING 0x8000ULL
#define TEST_STORAGE_TRB 0x8020ULL
#define TEST_STORAGE_REQUESTED 512U
#define TEST_STORAGE_RING_USABLE 252U
#define TEST_HID_SLOT 2U
#define TEST_HID_ENDPOINT 3U
#define TEST_HID_RING 0x4000ULL
#define TEST_HID_TRB 0x4010ULL
#define TEST_HID_REQUESTED 8U

static int failures;

static void expect(bool condition, const char *name) {
    if (!condition) {
        fprintf(stderr, "M61 event coexistence host FAILED: %s\n", name);
        ++failures;
    }
}

static struct xhci_trb transfer_event(uint8_t slot, uint8_t endpoint,
                                      uint64_t trb, uint8_t completion,
                                      uint32_t residual) {
    struct xhci_trb event = {0};
    event.parameter = trb;
    event.status = ((uint32_t)completion << 24U) |
                   (residual & 0x00ffffffU);
    event.control = ((uint32_t)XHCI_TRB_TYPE_TRANSFER_EVENT << 10U) |
                    ((uint32_t)endpoint << 16U) |
                    ((uint32_t)slot << 24U);
    return event;
}

static enum xhci_shared_transfer_owner classify(
    const struct xhci_state *state, const struct xhci_trb *event,
    uint32_t *actual, bool *short_packet) {
    enum xhci_shared_transfer_owner owner = XHCI_SHARED_TRANSFER_REJECT;
    expect(xhci_classify_shared_transfer_event(
               state, event, TEST_STORAGE_SLOT, TEST_STORAGE_ENDPOINT,
               TEST_STORAGE_RING, TEST_STORAGE_RING_USABLE,
               TEST_STORAGE_TRB, TEST_STORAGE_REQUESTED,
               &owner, actual, short_packet),
           "classifier arguments");
    return owner;
}

int main(void) {
    struct xhci_state state = {0};
    struct xhci_addressed_device *hid = &state.addressed[0];
    struct xhci_trb event;
    enum xhci_shared_transfer_owner owner;
    uint32_t actual = 0U;
    bool short_packet = false;

    state.addressed_count = 1U;
    hid->slot_id = TEST_HID_SLOT;
    hid->device_configured = true;
    hid->hid_endpoint_ready = true;
    hid->hid_configuration.endpoint_count = 1U;
    hid->hid_configuration.endpoints[0].endpoint_id = TEST_HID_ENDPOINT;
    hid->hid_configuration.endpoints[0].max_packet = TEST_HID_REQUESTED;
    hid->hid_ring_physical[0] = TEST_HID_RING;
    hid->hid_runtime[0].transfer_outstanding = true;
    hid->hid_runtime[0].expected_trb_physical = TEST_HID_TRB;

    event = transfer_event(TEST_HID_SLOT, TEST_HID_ENDPOINT, TEST_HID_TRB,
                           XHCI_COMPLETION_SUCCESS, 0U);
    owner = classify(&state, &event, &actual, &short_packet);
    expect(owner == XHCI_SHARED_TRANSFER_HID,
           "valid HID completion before Storage classified as HID");
    expect(actual == 0U && !short_packet,
           "HID classification does not forge Storage completion");

    event = transfer_event(TEST_STORAGE_SLOT, TEST_STORAGE_ENDPOINT,
                           TEST_STORAGE_TRB, XHCI_COMPLETION_SUCCESS, 0U);
    owner = classify(&state, &event, &actual, &short_packet);
    expect(owner == XHCI_SHARED_TRANSFER_STORAGE,
           "expected Storage completion accepted after HID");
    expect(actual == TEST_STORAGE_REQUESTED && !short_packet,
           "Storage completion length preserved");

    event = transfer_event(TEST_HID_SLOT, TEST_HID_ENDPOINT,
                           TEST_HID_TRB + XHCI_TRB_SIZE,
                           XHCI_COMPLETION_SUCCESS, 0U);
    owner = classify(&state, &event, &actual, &short_packet);
    expect(owner == XHCI_SHARED_TRANSFER_REJECT,
           "wrong HID TRB pointer rejected");

    event = transfer_event(TEST_HID_SLOT, TEST_HID_ENDPOINT, TEST_HID_TRB,
                           XHCI_COMPLETION_SHORT_PACKET,
                           TEST_HID_REQUESTED + 1U);
    owner = classify(&state, &event, &actual, &short_packet);
    expect(owner == XHCI_SHARED_TRANSFER_REJECT,
           "malformed HID residual rejected");

    event = transfer_event(7U, 9U, 0xa000ULL,
                           XHCI_COMPLETION_SUCCESS, 0U);
    owner = classify(&state, &event, &actual, &short_packet);
    expect(owner == XHCI_SHARED_TRANSFER_REJECT,
           "unknown Transfer Event rejected");

    event = transfer_event(TEST_STORAGE_SLOT, TEST_STORAGE_ENDPOINT,
                           TEST_STORAGE_TRB, XHCI_COMPLETION_SHORT_PACKET,
                           TEST_STORAGE_REQUESTED + 1U);
    owner = classify(&state, &event, &actual, &short_packet);
    expect(owner == XHCI_SHARED_TRANSFER_REJECT,
           "malformed Storage residual rejected");

    if (failures != 0) { return 1; }
    puts("M61 HID-before-Storage shared Event Ring ordering: PASS");
    puts("M61 unknown/malformed shared Transfer Events: REJECTED");
    return 0;
}
