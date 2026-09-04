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

struct event_cursor {
    uint16_t index;
    bool cycle;
};

static int failures;

static void expect(bool condition, const char *name) {
    if (!condition) {
        fprintf(stderr, "M61 event coexistence host FAILED: %s\n", name);
        ++failures;
    }
}

static void commit_cursor(struct event_cursor *cursor) {
    ++cursor->index;
    if (cursor->index == XHCI_EVENT_RING_TRBS) {
        cursor->index = 0U;
        cursor->cycle = !cursor->cycle;
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

static struct xhci_trb port_status_event(uint8_t port, bool cycle) {
    struct xhci_trb event = {0};
    event.parameter = (uint64_t)port << 24U;
    event.status = (uint32_t)XHCI_COMPLETION_SUCCESS << 24U;
    event.control = ((uint32_t)XHCI_TRB_TYPE_PORT_STATUS_EVENT << 10U) |
                    (cycle ? 1U : 0U);
    return event;
}

static uint64_t semantic_events(const struct xhci_state *state) {
    uint64_t total = (uint64_t)state->command_completions +
                     (uint64_t)state->port_events_consumed;
    uint8_t index;
    for (index = 0U; index < state->addressed_count; ++index) {
        total += state->addressed[index].transfer_events;
    }
    return total;
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
    struct event_cursor cursor;
    uint32_t hid_events;
    uint32_t storage_events;
    uint16_t dequeue_index;
    uint16_t next_dequeue_index;
    bool dequeue_cycle;
    bool next_dequeue_cycle;

    state.addressed_count = 1U;
    state.capabilities.max_ports = 8U;
    hid->slot_id = TEST_HID_SLOT;
    hid->device_configured = true;
    hid->hid_endpoint_ready = true;
    hid->hid_configuration.endpoint_count = 1U;
    hid->hid_configuration.endpoints[0].endpoint_id = TEST_HID_ENDPOINT;
    hid->hid_configuration.endpoints[0].max_packet = TEST_HID_REQUESTED;
    hid->hid_ring_physical[0] = TEST_HID_RING;
    hid->hid_runtime[0].transfer_outstanding = true;
    hid->hid_runtime[0].expected_trb_physical = TEST_HID_TRB;

    /* Existing inverse ordering: HID first, then expected Storage. */
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

    /* Missing regression: Storage is at the shared ring head, HID runs first. */
    cursor.index = 255U;
    cursor.cycle = true;
    hid_events = 17U;
    storage_events = 23U;
    event = transfer_event(TEST_STORAGE_SLOT, TEST_STORAGE_ENDPOINT,
                           TEST_STORAGE_TRB, XHCI_COMPLETION_SUCCESS, 0U);
    owner = classify(&state, &event, &actual, &short_packet);
    expect(owner == XHCI_SHARED_TRANSFER_STORAGE,
           "Storage-first ring head is uniquely Storage-owned");

    /* HID service must yield without physical dequeue or logical accounting. */
    if (owner == XHCI_SHARED_TRANSFER_HID) {
        ++hid_events;
        commit_cursor(&cursor);
    }
    expect(cursor.index == 255U && cursor.cycle,
           "HID does not dequeue Storage-owned ring head");
    expect(hid_events == 17U,
           "HID transfer_events unchanged for Storage-owned ring head");
    expect(storage_events == 23U,
           "Storage transfer_events unchanged while HID yields");

    /* Storage consumes that exact event once, then physical dequeue commits. */
    owner = classify(&state, &event, &actual, &short_packet);
    if (owner == XHCI_SHARED_TRANSFER_STORAGE) {
        ++storage_events;
        commit_cursor(&cursor);
    }
    expect(storage_events == 24U,
           "Storage transfer_events increments exactly once");
    expect(hid_events == 17U,
           "Storage consume does not forge HID accounting");
    expect(cursor.index == 0U && !cursor.cycle,
           "successful Storage consume commits 255-to-0 wrap exactly once");

    /* Also model HID-first plus Storage-second across the same wrap boundary. */
    cursor.index = 254U;
    cursor.cycle = true;
    hid_events = 31U;
    storage_events = 47U;
    event = transfer_event(TEST_HID_SLOT, TEST_HID_ENDPOINT, TEST_HID_TRB,
                           XHCI_COMPLETION_SUCCESS, 0U);
    owner = classify(&state, &event, &actual, &short_packet);
    if (owner == XHCI_SHARED_TRANSFER_HID) {
        ++hid_events;
        commit_cursor(&cursor);
    }
    expect(hid_events == 32U && storage_events == 47U,
           "Storage wait consumes/rearms HID exactly once");
    expect(cursor.index == 255U && cursor.cycle,
           "HID-first consume advances to index 255");

    event = transfer_event(TEST_STORAGE_SLOT, TEST_STORAGE_ENDPOINT,
                           TEST_STORAGE_TRB, XHCI_COMPLETION_SUCCESS, 0U);
    owner = classify(&state, &event, &actual, &short_packet);
    if (owner == XHCI_SHARED_TRANSFER_STORAGE) {
        ++storage_events;
        commit_cursor(&cursor);
    }
    expect(storage_events == 48U && hid_events == 32U,
           "expected Storage completion consumed exactly once after HID");
    expect(cursor.index == 0U && !cursor.cycle,
           "HID-then-Storage ordering wraps 255-to-0 coherently");

    /* Exact physical hazard: a Port Status Event precedes a valid HID Event. */
    state.command_completions = 255U;
    state.event_dequeue_count = 255ULL;
    expect(xhci_event_dequeue_position(&state, &dequeue_index,
                                       &dequeue_cycle) &&
           dequeue_index == 255U && dequeue_cycle,
           "generic dequeue starts at event N before wrap");
    event = port_status_event(2U, true);

    /* The old HID-only gate returned here, so neither software nor ERDP could
     * reach the valid HID completion in event N+1. */
    expect(((event.control >> 10U) & 0x3fU) !=
               XHCI_TRB_TYPE_TRANSFER_EVENT &&
           xhci_event_dequeue_position(&state, &next_dequeue_index,
                                       &next_dequeue_cycle) &&
           next_dequeue_index == dequeue_index &&
           next_dequeue_cycle == dequeue_cycle,
           "old transfer-only behavior remains stuck on event N");

    expect(xhci_consume_port_status_event(&state, &event),
           "fixed path validates and accounts Port Status event N");
    expect(xhci_event_dequeue_advance(&state, dequeue_index, dequeue_cycle,
                                      &next_dequeue_index,
                                      &next_dequeue_cycle) &&
           next_dequeue_index == 0U && !next_dequeue_cycle,
           "Port Status event advances generic dequeue across wrap once");
    expect(state.event_dequeue_count == 256ULL &&
           semantic_events(&state) == 256ULL,
           "generic and semantic accounting agree after Port Status event");
    expect(!xhci_event_dequeue_advance(&state, dequeue_index, dequeue_cycle,
                                       &next_dequeue_index,
                                       &next_dequeue_cycle) &&
           state.event_dequeue_count == 256ULL,
           "stale event N cannot advance the shared dequeue twice");

    event = transfer_event(TEST_HID_SLOT, TEST_HID_ENDPOINT, TEST_HID_TRB,
                           XHCI_COMPLETION_SUCCESS, 0U);
    owner = classify(&state, &event, &actual, &short_packet);
    expect(owner == XHCI_SHARED_TRANSFER_HID,
           "valid HID Transfer Event N+1 is reached after Port Status event");
    ++hid->transfer_events;
    expect(xhci_event_dequeue_advance(&state, 0U, false,
                                      &next_dequeue_index,
                                      &next_dequeue_cycle) &&
           next_dequeue_index == 1U && !next_dequeue_cycle,
           "HID Transfer Event N+1 advances the same shared dequeue once");
    expect(state.event_dequeue_count == 257ULL &&
           semantic_events(&state) == 257ULL,
           "generic and semantic accounting agree after N then N+1");

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
    puts("M61 Storage-first HID-yield Event Ring accounting: PASS");
    puts("M61 HID-before-Storage shared Event Ring ordering: PASS");
    puts("M61 Port-Status-before-HID generic dequeue regression: PASS");
    puts("M61 Event Ring index 255-to-0 accounting wrap: PASS");
    puts("M61 unknown/malformed shared Transfer Events: REJECTED");
    return 0;
}
