#ifndef BORING_USB_HID_H
#define BORING_USB_HID_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define USB_HID_BOOT_KEY_REPORT_SIZE 8U
#define USB_HID_BOOT_MOUSE_REPORT_MIN 3U
#define USB_HID_BOOT_KEYS 6U

struct usb_hid_key_transition {
    uint8_t usage;
    bool down;
};

struct usb_hid_keyboard_state {
    uint8_t modifiers;
    uint8_t keys[USB_HID_BOOT_KEYS];
};

struct usb_hid_mouse_report {
    int16_t dx;
    int16_t dy;
    int8_t wheel;
    uint8_t buttons;
};

/* Decode fixed USB HID boot-protocol reports only. Rollover/error usages are
 * rejected, duplicate usages are rejected and caller-owned output is bounded.
 */
bool usb_hid_keyboard_decode(struct usb_hid_keyboard_state *state,
                             const uint8_t *report, size_t length,
                             struct usb_hid_key_transition *transitions,
                             size_t capacity, size_t *count_out,
                             uint8_t *modifiers_out);
bool usb_hid_mouse_decode(const uint8_t *report, size_t length,
                          struct usb_hid_mouse_report *decoded);

#endif
