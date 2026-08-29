#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <boring/usb_hid.h>
#include <boring/xhci.h>

static unsigned failures;

static void check(bool condition, const char *name) {
    if (!condition) {
        fprintf(stderr, "xhci-host-test: FAIL: %s\n", name);
        ++failures;
    }
}

static void put32(uint8_t *bytes, size_t offset, uint32_t value) {
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1U] = (uint8_t)(value >> 8U);
    bytes[offset + 2U] = (uint8_t)(value >> 16U);
    bytes[offset + 3U] = (uint8_t)(value >> 24U);
}

static void capabilities_test(void) {
    uint8_t mmio[XHCI_MMIO_WINDOW_SIZE] = {0U};
    struct xhci_capabilities cap;

    mmio[0] = 0x40U;
    put32(mmio, 4U, 32U | (4U << 8U) | (8U << 24U));
    put32(mmio, 8U, (1U << 27U) | (2U << 21U));
    put32(mmio, 0x10U, (0x20U << 16U) | (1U << 2U));
    put32(mmio, 0x14U, 0x1003U);
    put32(mmio, 0x18U, 0x2021U);
    check(xhci_parse_capabilities(mmio, sizeof(mmio), &cap), "valid caps");
    check((cap.capability_length == 0x40U) && (cap.max_slots == 32U) &&
          (cap.max_interrupters == 4U) && (cap.max_ports == 8U),
          "hcs1 fields");
    check((cap.scratchpad_count == 34U) && cap.context_64_bytes,
          "hcs2/hcc fields");
    check((cap.doorbell_offset == 0x1000U) &&
          (cap.runtime_offset == 0x2020U) &&
          (cap.extended_capability_offset == 0x80U), "offset masking");

    mmio[0] = 0x10U;
    check(!xhci_parse_capabilities(mmio, sizeof(mmio), &cap), "short cap length");
    mmio[0] = 0x40U;
    put32(mmio, 4U, 32U | (4U << 8U) | (65U << 24U));
    check(!xhci_parse_capabilities(mmio, sizeof(mmio), &cap), "port bound");
    put32(mmio, 4U, 32U | (4U << 8U) | (8U << 24U));
    put32(mmio, 0x18U, 0xffffffe0U);
    check(!xhci_parse_capabilities(mmio, sizeof(mmio), &cap), "runtime bound");
    check(!xhci_parse_capabilities(NULL, sizeof(mmio), &cap), "null mmio");
}

static void keyboard_test(void) {
    struct usb_hid_keyboard_state state = {0U, {0U}};
    struct usb_hid_key_transition transitions[12];
    uint8_t modifiers = 0U;
    size_t count = 0U;
    const uint8_t a_down[8] = {2U, 0U, 4U, 0U, 0U, 0U, 0U, 0U};
    const uint8_t a_b[8] = {0U, 0U, 4U, 5U, 0U, 0U, 0U, 0U};
    const uint8_t b_only[8] = {0U, 0U, 5U, 0U, 0U, 0U, 0U, 0U};
    const uint8_t rollover[8] = {0U, 0U, 1U, 1U, 1U, 1U, 1U, 1U};

    check(usb_hid_keyboard_decode(&state, a_down, sizeof(a_down), transitions,
                                  12U, &count, &modifiers), "key first report");
    check((count == 1U) && (transitions[0].usage == 4U) &&
          transitions[0].down && (modifiers == 2U), "key A down");
    check(usb_hid_keyboard_decode(&state, a_b, sizeof(a_b), transitions,
                                  12U, &count, &modifiers) &&
          (count == 1U) && (transitions[0].usage == 5U) &&
          transitions[0].down, "key B added");
    check(usb_hid_keyboard_decode(&state, b_only, sizeof(b_only), transitions,
                                  12U, &count, &modifiers) &&
          (count == 1U) && (transitions[0].usage == 4U) &&
          !transitions[0].down, "key A release");
    check(!usb_hid_keyboard_decode(&state, rollover, sizeof(rollover),
                                   transitions, 12U, &count, &modifiers),
          "rollover rejection");
    check(state.keys[0] == 5U, "invalid report leaves state");
    check(!usb_hid_keyboard_decode(&state, a_down, 7U, transitions, 12U,
                                   &count, &modifiers), "key exact length");
}

static void mouse_test(void) {
    struct usb_hid_mouse_report decoded;
    const uint8_t report[4] = {5U, 0xfeU, 3U, 0xffU};
    check(usb_hid_mouse_decode(report, sizeof(report), &decoded), "mouse report");
    check((decoded.buttons == 5U) && (decoded.dx == -2) &&
          (decoded.dy == 3) && (decoded.wheel == -1), "mouse decode");
    check(!usb_hid_mouse_decode(report, 2U, &decoded), "mouse short reject");
}

int main(void) {
    capabilities_test();
    keyboard_test();
    mouse_test();
    if (failures != 0U) { return 1; }
    puts("xhci-host-test: PASS");
    return 0;
}
