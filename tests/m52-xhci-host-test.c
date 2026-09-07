#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <boring/usb_hid.h>
#include <boring/xhci.h>

#define TRB_TYPE_SHIFT 10U
#define EVENT_ENDPOINT_SHIFT 16U
#define EVENT_SLOT_SHIFT 24U
#define COMPLETION_SHIFT 24U

static unsigned failures;

static void check(bool condition, const char *name) {
    if (!condition) {
        (void)fprintf(stderr, "FAIL: %s\n", name);
        ++failures;
    }
}

static void tablet_decoder_test(void) {
    const uint8_t report[USB_HID_ABSOLUTE_TABLET_REPORT_SIZE] = {
        0x01U, 0x34U, 0x12U, 0x78U, 0x56U, 0xffU
    };
    struct usb_hid_absolute_tablet_report decoded = {0};
    check(usb_hid_absolute_tablet_decode(report, sizeof(report), &decoded) &&
          (decoded.buttons == 1U) && (decoded.x == 0x1234U) &&
          (decoded.y == 0x5678U) && (decoded.wheel == -1),
          "bounded absolute tablet decode");
    check(!usb_hid_absolute_tablet_decode(report, 5U, &decoded),
          "tablet short report rejected");
    check(!usb_hid_absolute_tablet_decode(report, 7U, &decoded),
          "tablet long report rejected");
    check(!usb_hid_absolute_tablet_decode(NULL, sizeof(report), &decoded),
          "tablet null report rejected");
}

static void interrupt_trb_test(void) {
    struct xhci_trb trb = {0};
    struct xhci_trb before;
    uint64_t physical = 0ULL;
    uint16_t next = 0U;
    bool cycle = false;

    check(xhci_build_interrupt_in_trb(&trb, 0x20000ULL, 7U, true,
                                      0x40000ULL, 8U, &physical, &next,
                                      &cycle),
          "normal Interrupt-IN TRB construction");
    check((trb.parameter == 0x40000ULL) && (trb.status == 8U) &&
          (((trb.control >> TRB_TYPE_SHIFT) & 0x3fU) == XHCI_TRB_TYPE_NORMAL) &&
          ((trb.control & (1U << 0U)) != 0U) &&
          ((trb.control & (1U << 2U)) != 0U) &&
          ((trb.control & (1U << 5U)) != 0U) &&
          (physical == 0x20070ULL) && (next == 8U) && cycle,
          "normal TRB fields and producer advance");

    check(xhci_build_interrupt_in_trb(
              &trb, 0x20000ULL, XHCI_INTERRUPT_RING_USABLE - 1U, true,
              0x40000ULL, 8U, &physical, &next, &cycle) &&
          (next == 0U) && !cycle,
          "interrupt ring Link boundary cycle wrap");

    before = trb;
    check(!xhci_build_interrupt_in_trb(
              &trb, 0x20000ULL, XHCI_INTERRUPT_RING_USABLE, true,
              0x40000ULL, 8U, &physical, &next, &cycle) &&
          (memcmp(&trb, &before, sizeof(trb)) == 0),
          "interrupt ring overflow rejected without mutation");
    check(!xhci_build_interrupt_in_trb(&trb, 0x20001ULL, 0U, true,
                                       0x40000ULL, 8U, &physical, &next,
                                       &cycle),
          "interrupt ring alignment rejected");
    check(!xhci_build_interrupt_in_trb(&trb, 0x20000ULL, 0U, true,
                                       0x40001ULL, 8U, &physical, &next,
                                       &cycle),
          "report DMA alignment rejected");
    check(!xhci_build_interrupt_in_trb(&trb, 0x20000ULL, 0U, true,
                                       0x40000ULL, 0U, &physical, &next,
                                       &cycle),
          "zero report length rejected");
}

static struct xhci_trb transfer_event(uint64_t pointer, uint8_t slot,
                                      uint8_t endpoint, uint8_t completion,
                                      uint32_t residual) {
    struct xhci_trb event = {0};
    event.parameter = pointer;
    event.status = residual | ((uint32_t)completion << COMPLETION_SHIFT);
    event.control = ((uint32_t)XHCI_TRB_TYPE_TRANSFER_EVENT << TRB_TYPE_SHIFT) |
                    ((uint32_t)endpoint << EVENT_ENDPOINT_SHIFT) |
                    ((uint32_t)slot << EVENT_SLOT_SHIFT) | 1U;
    return event;
}

static void interrupt_event_test(void) {
    struct xhci_trb event = transfer_event(0x20030ULL, 7U, 3U,
                                           XHCI_COMPLETION_SUCCESS, 0U);
    uint16_t actual = 0U;
    bool short_packet = true;

    check(xhci_validate_interrupt_transfer_event(
              &event, 0x20000ULL, 7U, 3U, 0x20030ULL, 8U,
              &actual, &short_packet) &&
          (actual == 8U) && !short_packet,
          "successful Interrupt-IN Transfer Event");

    event = transfer_event(0x20030ULL, 7U, 3U,
                           XHCI_COMPLETION_SHORT_PACKET, 2U);
    check(xhci_validate_interrupt_transfer_event(
              &event, 0x20000ULL, 7U, 3U, 0x20030ULL, 8U,
              &actual, &short_packet) &&
          (actual == 6U) && short_packet,
          "short Interrupt-IN report residual");

    event = transfer_event(0x20030ULL, 8U, 3U,
                           XHCI_COMPLETION_SUCCESS, 0U);
    check(!xhci_validate_interrupt_transfer_event(
              &event, 0x20000ULL, 7U, 3U, 0x20030ULL, 8U,
              &actual, &short_packet),
          "wrong slot rejected");
    event = transfer_event(0x20030ULL, 7U, 5U,
                           XHCI_COMPLETION_SUCCESS, 0U);
    check(!xhci_validate_interrupt_transfer_event(
              &event, 0x20000ULL, 7U, 3U, 0x20030ULL, 8U,
              &actual, &short_packet),
          "wrong endpoint rejected");
    event = transfer_event(0x20040ULL, 7U, 3U,
                           XHCI_COMPLETION_SUCCESS, 0U);
    check(!xhci_validate_interrupt_transfer_event(
              &event, 0x20000ULL, 7U, 3U, 0x20030ULL, 8U,
              &actual, &short_packet),
          "stale TRB pointer rejected");
    event = transfer_event(0x20030ULL, 7U, 3U,
                           XHCI_COMPLETION_SHORT_PACKET, 9U);
    check(!xhci_validate_interrupt_transfer_event(
              &event, 0x20000ULL, 7U, 3U, 0x20030ULL, 8U,
              &actual, &short_packet),
          "oversized residual rejected");
    event = transfer_event(0x20030ULL, 7U, 3U, 2U, 0U);
    check(!xhci_validate_interrupt_transfer_event(
              &event, 0x20000ULL, 7U, 3U, 0x20030ULL, 8U,
              &actual, &short_packet),
          "unsupported completion code rejected");
}

static void keyboard_transition_test(void) {
    struct usb_hid_keyboard_state state = {0};
    struct usb_hid_key_transition transitions[12];
    const uint8_t down[8] = {0U, 0U, 4U, 0U, 0U, 0U, 0U, 0U};
    const uint8_t up[8] = {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    size_t count = 0U;
    uint8_t modifiers = 0U;
    check(usb_hid_keyboard_decode(&state, down, sizeof(down), transitions,
                                  12U, &count, &modifiers) &&
          (count == 1U) && (transitions[0].usage == 4U) &&
          transitions[0].down,
          "keyboard real-report press transition model");
    check(usb_hid_keyboard_decode(&state, up, sizeof(up), transitions,
                                  12U, &count, &modifiers) &&
          (count == 1U) && (transitions[0].usage == 4U) &&
          !transitions[0].down,
          "keyboard real-report release transition model");
}

int main(void) {
    tablet_decoder_test();
    interrupt_trb_test();
    interrupt_event_test();
    keyboard_transition_test();
    if (failures != 0U) { return 1; }
    puts("m52-xhci-host-test: PASS");
    return 0;
}
