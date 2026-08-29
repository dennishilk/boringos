#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/usb_hid.h>
#include <boring/xhci.h>

#if !__STDC_HOSTED__
#include <boring/vmm.h>

enum m60_hid_classification {
    M60_HID_SUPPORTED = 0,
    M60_HID_NOT_HID,
    M60_HID_INVALID
};

struct m60_non_hid_saved {
    struct xhci_addressed_device device;
    uint8_t index;
};

bool xhci_poll_hid_reports_legacy(struct xhci_state *state,
                                  uint32_t completion_goal);
bool xhci_service_hid_reports_legacy(struct xhci_state *state);

#define xhci_poll_hid_reports xhci_poll_hid_reports_legacy
#define xhci_service_hid_reports xhci_service_hid_reports_legacy
#endif

#include "usb_hid_impl.inc"

#if !__STDC_HOSTED__
#undef xhci_poll_hid_reports
#undef xhci_service_hid_reports

static uint16_t m60_read_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static enum m60_hid_classification m60_classify_hid_device(
    const struct xhci_addressed_device *device) {
    void *descriptor_virtual = NULL;
    const uint8_t *bytes;
    uint16_t total;
    uint32_t offset = 0U;
    bool claims_hid = false;
    struct xhci_hid_configuration parsed;

    if ((device == NULL) || !device->addressed || !device->descriptors_ready ||
        (device->descriptor_buffer_physical == 0ULL) ||
        (device->descriptors.configuration_length < 9U) ||
        (device->descriptors.configuration_length >
         XHCI_DESCRIPTOR_BUFFER_BYTES) ||
        !vmm_pmm_frame_to_hhdm(device->descriptor_buffer_physical,
                               &descriptor_virtual)) {
        return M60_HID_INVALID;
    }
    bytes = (const uint8_t *)descriptor_virtual;
    if ((bytes[0] < 9U) ||
        (bytes[1] != XHCI_USB_DESCRIPTOR_CONFIGURATION)) {
        return M60_HID_INVALID;
    }
    total = m60_read_le16(&bytes[2]);
    if ((total != device->descriptors.configuration_length) ||
        (total < 9U) || (total > XHCI_DESCRIPTOR_BUFFER_BYTES)) {
        return M60_HID_INVALID;
    }
    while (offset < total) {
        uint8_t length;
        uint8_t type;
        if ((uint32_t)total - offset < 2U) { return M60_HID_INVALID; }
        length = bytes[offset];
        type = bytes[offset + 1U];
        if ((length < 2U) ||
            ((uint32_t)length > (uint32_t)total - offset)) {
            return M60_HID_INVALID;
        }
        if (type == XHCI_USB_DESCRIPTOR_INTERFACE) {
            if (length < 9U) { return M60_HID_INVALID; }
            if ((bytes[offset + 3U] == 0U) &&
                (bytes[offset + 5U] == XHCI_USB_CLASS_HID)) {
                claims_hid = true;
            }
        }
        offset += length;
    }
    if (offset != total) { return M60_HID_INVALID; }
    if (!claims_hid) { return M60_HID_NOT_HID; }
    if (!xhci_parse_hid_configuration(bytes, total, device->speed, &parsed)) {
        return M60_HID_INVALID;
    }
    return M60_HID_SUPPORTED;
}

static const struct xhci_addressed_device *m60_find_caller_device(
    const struct xhci_state *state, uint8_t slot_id) {
    uint8_t index;
    if ((state == NULL) || (slot_id == 0U)) { return NULL; }
    for (index = 0U; index < state->addressed_count; ++index) {
        if (state->addressed[index].slot_id == slot_id) {
            return &state->addressed[index];
        }
    }
    return NULL;
}

static bool m60_prepare_hid_mixed_state(
    struct xhci_state *caller, struct xhci_state *active,
    struct m60_non_hid_saved saved[XHCI_MAX_ADDRESSED_DEVICES],
    uint8_t *saved_count) {
    uint8_t index;
    uint8_t count = 0U;
    if ((caller == NULL) || (active == NULL) || (saved == NULL) ||
        (saved_count == NULL) ||
        (caller->addressed_count != active->addressed_count) ||
        (active->addressed_count > XHCI_MAX_ADDRESSED_DEVICES)) {
        return false;
    }
    active->command_completions = caller->command_completions;
    active->port_events_consumed = caller->port_events_consumed;
    for (index = 0U; index < active->addressed_count; ++index) {
        enum m60_hid_classification classification =
            m60_classify_hid_device(&active->addressed[index]);
        if (classification == M60_HID_INVALID) { return false; }
        if (classification == M60_HID_NOT_HID) {
            const struct xhci_addressed_device *caller_device =
                m60_find_caller_device(caller, active->addressed[index].slot_id);
            if ((caller_device == NULL) || (count >= XHCI_MAX_ADDRESSED_DEVICES)) {
                return false;
            }
            saved[count].index = index;
            saved[count].device = *caller_device;
            active->addressed[index] = *caller_device;
            active->addressed[index].device_configured = true;
            active->addressed[index].hid_endpoint_ready = true;
            active->addressed[index].hid_configuration.endpoint_count = 0U;
            ++count;
        }
    }
    *saved_count = count;
    return true;
}

static void m60_restore_non_hid_state(
    struct xhci_state *caller, struct xhci_state *active,
    const struct m60_non_hid_saved saved[XHCI_MAX_ADDRESSED_DEVICES],
    uint8_t saved_count) {
    uint8_t index;
    for (index = 0U; index < saved_count; ++index) {
        active->addressed[saved[index].index] = saved[index].device;
    }
    *caller = *active;
}

static bool m60_hid_mixed_call(struct xhci_state *state,
                               uint32_t completion_goal, bool service) {
    const struct xhci_state *published;
    struct xhci_state *active;
    struct m60_non_hid_saved saved[XHCI_MAX_ADDRESSED_DEVICES];
    uint8_t saved_count = 0U;
    bool result;

    if ((state == NULL) || (!service && (completion_goal == 0U))) {
        return false;
    }
    published = xhci_get_state();
    if ((published == NULL) || !published->controller_running) { return false; }
    active = (struct xhci_state *)(uintptr_t)published;
    if (!m60_prepare_hid_mixed_state(state, active, saved, &saved_count)) {
        return false;
    }
    if (service) {
        result = xhci_service_hid_reports_legacy(state);
    } else {
        result = xhci_poll_hid_reports_legacy(state, completion_goal);
    }
    m60_restore_non_hid_state(state, active, saved, saved_count);
    return result;
}

bool xhci_poll_hid_reports(struct xhci_state *state, uint32_t completion_goal) {
    return m60_hid_mixed_call(state, completion_goal, false);
}

bool xhci_service_hid_reports(struct xhci_state *state) {
    return m60_hid_mixed_call(state, 1U, true);
}
#endif
