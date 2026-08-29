#include <boring/usb_hid.h>

static bool usage_present(const uint8_t keys[USB_HID_BOOT_KEYS], uint8_t usage) {
    size_t index;
    for (index = 0U; index < USB_HID_BOOT_KEYS; ++index) {
        if (keys[index] == usage) { return true; }
    }
    return false;
}

static bool report_valid(const uint8_t *report) {
    size_t left;
    for (left = 2U; left < USB_HID_BOOT_KEY_REPORT_SIZE; ++left) {
        size_t right;
        const uint8_t usage = report[left];
        if ((usage >= 1U) && (usage <= 3U)) { return false; }
        if (usage == 0U) { continue; }
        for (right = left + 1U; right < USB_HID_BOOT_KEY_REPORT_SIZE; ++right) {
            if (report[right] == usage) { return false; }
        }
    }
    return true;
}

bool usb_hid_keyboard_decode(struct usb_hid_keyboard_state *state,
                             const uint8_t *report, size_t length,
                             struct usb_hid_key_transition *transitions,
                             size_t capacity, size_t *count_out,
                             uint8_t *modifiers_out) {
    uint8_t next[USB_HID_BOOT_KEYS];
    size_t count = 0U;
    size_t index;

    if ((state == NULL) || (report == NULL) || (transitions == NULL) ||
        (count_out == NULL) || (modifiers_out == NULL) ||
        (length != USB_HID_BOOT_KEY_REPORT_SIZE) || !report_valid(report)) {
        return false;
    }
    for (index = 0U; index < USB_HID_BOOT_KEYS; ++index) {
        next[index] = report[index + 2U];
    }
    for (index = 0U; index < USB_HID_BOOT_KEYS; ++index) {
        const uint8_t usage = state->keys[index];
        if ((usage != 0U) && !usage_present(next, usage)) {
            if (count >= capacity) { return false; }
            transitions[count].usage = usage;
            transitions[count].down = false;
            ++count;
        }
    }
    for (index = 0U; index < USB_HID_BOOT_KEYS; ++index) {
        const uint8_t usage = next[index];
        if ((usage != 0U) && !usage_present(state->keys, usage)) {
            if (count >= capacity) { return false; }
            transitions[count].usage = usage;
            transitions[count].down = true;
            ++count;
        }
    }
    state->modifiers = report[0];
    for (index = 0U; index < USB_HID_BOOT_KEYS; ++index) {
        state->keys[index] = next[index];
    }
    *count_out = count;
    *modifiers_out = report[0];
    return true;
}

bool usb_hid_mouse_decode(const uint8_t *report, size_t length,
                          struct usb_hid_mouse_report *decoded) {
    if ((report == NULL) || (decoded == NULL) ||
        (length < USB_HID_BOOT_MOUSE_REPORT_MIN) || (length > 4U)) {
        return false;
    }
    decoded->buttons = (uint8_t)(report[0] & 0x1fU);
    decoded->dx = (int16_t)(int8_t)report[1];
    decoded->dy = (int16_t)(int8_t)report[2];
    decoded->wheel = (length == 4U) ? (int8_t)report[3] : 0;
    return true;
}

bool usb_hid_absolute_tablet_decode(
    const uint8_t *report, size_t length,
    struct usb_hid_absolute_tablet_report *decoded) {
    if ((report == NULL) || (decoded == NULL) ||
        (length != USB_HID_ABSOLUTE_TABLET_REPORT_SIZE)) {
        return false;
    }
    decoded->buttons = (uint8_t)(report[0] & 0x1fU);
    decoded->x = (uint16_t)((uint16_t)report[1] |
                            ((uint16_t)report[2] << 8U));
    decoded->y = (uint16_t)((uint16_t)report[3] |
                            ((uint16_t)report[4] << 8U));
    decoded->wheel = (int8_t)report[5];
    return true;
}
