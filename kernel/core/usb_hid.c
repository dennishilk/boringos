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

static bool m60_poll_hid_reports_limit(struct xhci_state *state,
                                        uint32_t completion_goal,
                                        uint32_t wait_limit,
                                        bool rearm_after_completion) {
    const struct xhci_state *published;
    struct xhci_state *active;
    volatile void *mapping = NULL;
    volatile uint8_t *mmio;
    void *event_virtual = NULL;
    volatile struct xhci_trb *event_ring;
    uint64_t consumed;
    uint16_t event_index;
    bool event_cycle;
    uint32_t completed = 0U;
    uint32_t attempt;
    uint8_t device_index;
    bool success = false;

    if ((state == NULL) || (completion_goal == 0U) || (wait_limit == 0U)) {
        return false;
    }
    published = xhci_get_state();
    if ((published == NULL) || !published->controller_running ||
        (published->addressed_count == 0U) ||
        !vmm_map_mmio_region(published->mmio_physical,
                             M52_MMIO_WINDOW_SIZE, &mapping) ||
        !vmm_pmm_frame_to_hhdm(published->event_ring_physical,
                               &event_virtual)) {
        if (mapping != NULL) {
            (void)vmm_unmap_mmio_region(mapping, M52_MMIO_WINDOW_SIZE);
        }
        return false;
    }

    active = (struct xhci_state *)(uintptr_t)published;
    mmio = (volatile uint8_t *)mapping;
    event_ring = (volatile struct xhci_trb *)event_virtual;

    for (device_index = 0U; device_index < active->addressed_count;
         ++device_index) {
        struct xhci_addressed_device *device = &active->addressed[device_index];
        enum m60_hid_classification classification =
            m60_classify_hid_device(device);
        uint8_t endpoint_index;

        if (classification == M60_HID_INVALID) { goto out; }
        if (classification == M60_HID_NOT_HID) { continue; }
        if (!device->device_configured || !device->hid_endpoint_ready) {
            goto out;
        }
        for (endpoint_index = 0U;
             endpoint_index < device->hid_configuration.endpoint_count;
             ++endpoint_index) {
            if (!device->hid_runtime[endpoint_index].transfer_outstanding &&
                !m52_submit_endpoint(mmio, device, endpoint_index)) {
                goto out;
            }
        }
    }

    consumed = m52_consumed_events(active);
    event_index = (uint16_t)(consumed % XHCI_EVENT_RING_TRBS);
    event_cycle = (((consumed / XHCI_EVENT_RING_TRBS) & 1ULL) == 0ULL);
    for (attempt = 0U;
         (attempt < wait_limit) && (completed < completion_goal);
         ++attempt) {
        struct xhci_trb event;
        uint32_t control = event_ring[event_index].control;
        uint8_t type;
        const uint32_t interrupter = active->capabilities.runtime_offset +
                                     M52_RUNTIME_INTERRUPTER0;

        if (((control & M52_TRB_CYCLE) != 0U) != event_cycle) {
            x86_64_pause();
            continue;
        }
        event.parameter = event_ring[event_index].parameter;
        event.status = event_ring[event_index].status;
        event.control = control;
        ++event_index;
        if (event_index == XHCI_EVENT_RING_TRBS) {
            event_index = 0U;
            event_cycle = !event_cycle;
        }
        m52_barrier();
        if (interrupter > M52_MMIO_WINDOW_SIZE - 0x20U) { goto out; }
        m52_mmio_write64(mmio, interrupter + 0x18U,
            (active->event_ring_physical +
             ((uint64_t)event_index * XHCI_TRB_SIZE)) | (1ULL << 3U));
        type = (uint8_t)((event.control >> M52_TRB_TYPE_SHIFT) &
                         M52_TRB_TYPE_MASK);
        if (type == M52_TRB_TYPE_PORT_STATUS_EVENT) {
            goto out;
        }
        if ((type != XHCI_TRB_TYPE_TRANSFER_EVENT) ||
            !m52_complete_event(active, &event, &completed)) {
            goto out;
        }
        if ((completed < completion_goal) || rearm_after_completion) {
            for (device_index = 0U; device_index < active->addressed_count;
                 ++device_index) {
                struct xhci_addressed_device *device =
                    &active->addressed[device_index];
                enum m60_hid_classification classification =
                    m60_classify_hid_device(device);
                uint8_t endpoint_index;

                if (classification == M60_HID_INVALID) { goto out; }
                if (classification == M60_HID_NOT_HID) { continue; }
                if (!device->device_configured || !device->hid_endpoint_ready) {
                    goto out;
                }
                for (endpoint_index = 0U;
                     endpoint_index < device->hid_configuration.endpoint_count;
                     ++endpoint_index) {
                    if (!device->hid_runtime[endpoint_index].transfer_outstanding &&
                        !m52_submit_endpoint(mmio, device, endpoint_index)) {
                        goto out;
                    }
                }
            }
        }
    }

    success = completed >= completion_goal;
out:
    *state = *active;
    if (mapping != NULL) {
        (void)vmm_unmap_mmio_region(mapping, M52_MMIO_WINDOW_SIZE);
    }
    return success;
}

bool xhci_poll_hid_reports(struct xhci_state *state, uint32_t completion_goal) {
    return m60_poll_hid_reports_limit(state, completion_goal,
                                      M52_EVENT_WAIT_LIMIT, false);
}

bool xhci_service_hid_reports(struct xhci_state *state) {
    return m60_poll_hid_reports_limit(state, 1U,
                                      M54_SERVICE_EVENT_WAIT_LIMIT, true);
}
#endif
