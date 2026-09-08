#ifndef BORING_USB_HID_H
#define BORING_USB_HID_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define USB_HID_BOOT_KEY_REPORT_SIZE 8U
#define USB_HID_BOOT_MOUSE_REPORT_MIN 3U
#define USB_HID_ABSOLUTE_TABLET_REPORT_SIZE 6U
#define USB_HID_BOOT_KEYS 6U
#define USB_HID_REPORT_DESCRIPTOR_MAX_BYTES 1024U
#define USB_HID_MOUSE_REPORT_MAX_BITS 512U

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

struct usb_hid_absolute_tablet_report {
    uint16_t x;
    uint16_t y;
    int8_t wheel;
    uint8_t buttons;
};

struct usb_hid_mouse_layout {
    uint16_t report_bits;
    uint16_t buttons_bit_offset;
    uint16_t x_bit_offset;
    uint16_t y_bit_offset;
    uint16_t wheel_bit_offset;
    int32_t x_logical_minimum;
    int32_t x_logical_maximum;
    int32_t y_logical_minimum;
    int32_t y_logical_maximum;
    int32_t wheel_logical_minimum;
    int32_t wheel_logical_maximum;
    uint8_t report_id;
    uint8_t button_count;
    uint8_t x_bits;
    uint8_t y_bits;
    uint8_t wheel_bits;
    bool has_report_id;
    bool has_wheel;
};

enum usb_hid_report_parse_result {
    USB_HID_REPORT_PARSE_SUPPORTED = 0,
    USB_HID_REPORT_PARSE_VALID_UNSUPPORTED,
    USB_HID_REPORT_PARSE_MALFORMED,
    USB_HID_REPORT_PARSE_BIT_OVERFLOW
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

/* Bounded six-byte absolute-pointer report used by the explicitly supported
 * direct HID-tablet transport. This is not a generic HID report parser. */
bool usb_hid_absolute_tablet_decode(
    const uint8_t *report, size_t length,
    struct usb_hid_absolute_tablet_report *decoded);

/* Bounded HID report-descriptor subset for conventional relative mice. */
enum usb_hid_report_parse_result usb_hid_parse_mouse_report_descriptor(
    const uint8_t *bytes, size_t length,
    struct usb_hid_mouse_layout *layout);
bool usb_hid_mouse_layout_decode(
    const struct usb_hid_mouse_layout *layout,
    const uint8_t *report, size_t length,
    struct usb_hid_mouse_report *decoded);

#endif
