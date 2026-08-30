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

static bool m61_storage_pointer_owned(uint64_t pointer,
                                      uint64_t ring_physical,
                                      uint16_t ring_usable) {
    uint64_t span;
    uint64_t end;
    if ((ring_physical == 0ULL) || (ring_usable == 0U) ||
        ((pointer & 0x0fULL) != 0ULL)) {
        return false;
    }
    span = (uint64_t)ring_usable * XHCI_TRB_SIZE;
    if (ring_physical > UINT64_MAX - span) { return false; }
    end = ring_physical + span;
    return (pointer >= ring_physical) && (pointer < end);
}

bool xhci_classify_shared_transfer_event(
    const struct xhci_state *state, const struct xhci_trb *event,
    uint8_t storage_slot_id, uint8_t storage_endpoint_id,
    uint64_t storage_ring_physical, uint16_t storage_ring_usable,
    uint64_t storage_expected_trb_physical, uint32_t storage_requested_length,
    enum xhci_shared_transfer_owner *owner,
    uint32_t *storage_actual_length, bool *storage_short_packet) {
    uint8_t type;
    uint8_t endpoint;
    uint8_t slot;
    uint8_t completion;
    uint32_t residual;
    uint8_t device_index;
    uint32_t hid_matches = 0U;

    if ((state == NULL) || (event == NULL) || (owner == NULL) ||
        (storage_actual_length == NULL) || (storage_short_packet == NULL) ||
        (storage_slot_id == 0U) || (storage_endpoint_id <= 1U) ||
        (storage_endpoint_id > 31U) || (storage_requested_length == 0U) ||
        !m61_storage_pointer_owned(storage_expected_trb_physical,
                                   storage_ring_physical,
                                   storage_ring_usable)) {
        return false;
    }

    *owner = XHCI_SHARED_TRANSFER_REJECT;
    *storage_actual_length = 0U;
    *storage_short_packet = false;

    type = (uint8_t)((event->control >> M52_TRB_TYPE_SHIFT) &
                     M52_TRB_TYPE_MASK);
    if (type != XHCI_TRB_TYPE_TRANSFER_EVENT) { return true; }

    endpoint = (uint8_t)((event->control >> M52_EVENT_ENDPOINT_SHIFT) &
                         M52_EVENT_ENDPOINT_MASK);
    slot = (uint8_t)(event->control >> M52_EVENT_SLOT_SHIFT);
    completion = (uint8_t)(event->status >> M52_EVENT_COMPLETION_SHIFT);
    residual = event->status & M52_EVENT_RESIDUAL_MASK;

    if ((endpoint == storage_endpoint_id) && (slot == storage_slot_id) &&
        (event->parameter == storage_expected_trb_physical)) {
        if (!m61_storage_pointer_owned(event->parameter,
                                       storage_ring_physical,
                                       storage_ring_usable) ||
            (residual > storage_requested_length) ||
            ((completion != XHCI_COMPLETION_SUCCESS) &&
             (completion != XHCI_COMPLETION_SHORT_PACKET))) {
            return true;
        }
        *storage_actual_length = storage_requested_length - residual;
        *storage_short_packet =
            completion == XHCI_COMPLETION_SHORT_PACKET;
        *owner = XHCI_SHARED_TRANSFER_STORAGE;
        return true;
    }

    if (state->addressed_count > XHCI_MAX_ADDRESSED_DEVICES) { return false; }
    for (device_index = 0U; device_index < state->addressed_count;
         ++device_index) {
        const struct xhci_addressed_device *device =
            &state->addressed[device_index];
        uint8_t endpoint_index;
        if (!device->device_configured || !device->hid_endpoint_ready) {
            continue;
        }
        if (device->hid_configuration.endpoint_count > XHCI_MAX_HID_ENDPOINTS) {
            return false;
        }
        for (endpoint_index = 0U;
             endpoint_index < device->hid_configuration.endpoint_count;
             ++endpoint_index) {
            const struct xhci_hid_endpoint_runtime *runtime =
                &device->hid_runtime[endpoint_index];
            const struct xhci_hid_endpoint_descriptor *descriptor =
                &device->hid_configuration.endpoints[endpoint_index];
            uint16_t actual = 0U;
            bool short_packet = false;
            if (!runtime->transfer_outstanding) { continue; }
            if (!xhci_validate_interrupt_transfer_event(
                    event, device->hid_ring_physical[endpoint_index],
                    device->slot_id, descriptor->endpoint_id,
                    runtime->expected_trb_physical, descriptor->max_packet,
                    &actual, &short_packet)) {
                continue;
            }
            (void)actual;
            (void)short_packet;
            ++hid_matches;
            if (hid_matches > 1U) { return false; }
        }
    }
    if (hid_matches == 1U) { *owner = XHCI_SHARED_TRANSFER_HID; }
    return true;
}

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

static bool m60_rearm_hid_endpoints(struct xhci_state *active,
                                     volatile uint8_t *mmio) {
    static bool traced_failure;
    uint8_t device_index;
    if ((active == NULL) || (mmio == NULL)) { return false; }
    for (device_index = 0U; device_index < active->addressed_count;
         ++device_index) {
        struct xhci_addressed_device *device = &active->addressed[device_index];
        enum m60_hid_classification classification =
            m60_classify_hid_device(device);
        uint8_t endpoint_index;

        if (classification == M60_HID_INVALID) {
            if (!traced_failure) {
                traced_failure = true;
                serial_write_string(
                    "m60-rearm-diag: reason=classification dev/addressed/descriptors/configured/ready/descbuf/configlen/endpoints=");
                serial_write_u64((uint64_t)device_index);
                serial_write_string("/");
                serial_write_u64(device->addressed ? 1ULL : 0ULL);
                serial_write_string("/");
                serial_write_u64(device->descriptors_ready ? 1ULL : 0ULL);
                serial_write_string("/");
                serial_write_u64(device->device_configured ? 1ULL : 0ULL);
                serial_write_string("/");
                serial_write_u64(device->hid_endpoint_ready ? 1ULL : 0ULL);
                serial_write_string("/");
                serial_write_u64(device->descriptor_buffer_physical);
                serial_write_string("/");
                serial_write_u64(
                    (uint64_t)device->descriptors.configuration_length);
                serial_write_string("/");
                serial_write_u64(
                    (uint64_t)device->hid_configuration.endpoint_count);
                serial_write_string("\n");
            }
            return false;
        }
        if (classification == M60_HID_NOT_HID) { continue; }
        if (!device->device_configured || !device->hid_endpoint_ready ||
            (device->hid_configuration.endpoint_count > XHCI_MAX_HID_ENDPOINTS)) {
            if (!traced_failure) {
                traced_failure = true;
                serial_write_string(
                    "m60-rearm-diag: reason=configuration dev/configured/ready/endpoints=");
                serial_write_u64((uint64_t)device_index);
                serial_write_string("/");
                serial_write_u64(device->device_configured ? 1ULL : 0ULL);
                serial_write_string("/");
                serial_write_u64(device->hid_endpoint_ready ? 1ULL : 0ULL);
                serial_write_string("/");
                serial_write_u64(
                    (uint64_t)device->hid_configuration.endpoint_count);
                serial_write_string("\n");
            }
            return false;
        }
        for (endpoint_index = 0U;
             endpoint_index < device->hid_configuration.endpoint_count;
             ++endpoint_index) {
            if (!device->hid_runtime[endpoint_index].transfer_outstanding &&
                !m52_submit_endpoint(mmio, device, endpoint_index)) {
                if (!traced_failure) {
                    const struct xhci_hid_endpoint_runtime *runtime =
                        &device->hid_runtime[endpoint_index];
                    const struct xhci_hid_endpoint_descriptor *descriptor =
                        &device->hid_configuration.endpoints[endpoint_index];
                    traced_failure = true;
                    serial_write_string(
                        "m60-rearm-diag: reason=submit dev/slot/index/ep/proto/max=");
                    serial_write_u64((uint64_t)device_index);
                    serial_write_string("/");
                    serial_write_u64((uint64_t)device->slot_id);
                    serial_write_string("/");
                    serial_write_u64((uint64_t)endpoint_index);
                    serial_write_string("/");
                    serial_write_u64((uint64_t)descriptor->endpoint_id);
                    serial_write_string("/");
                    serial_write_u64((uint64_t)descriptor->protocol);
                    serial_write_string("/");
                    serial_write_u64((uint64_t)descriptor->max_packet);
                    serial_write_string("\n");
                    serial_write_string(
                        "m60-rearm-diag: ring/report/out/expected/producer/cycle/submitted/completed=");
                    serial_write_u64(
                        device->hid_ring_physical[endpoint_index]);
                    serial_write_string("/");
                    serial_write_u64(runtime->report_buffer_physical);
                    serial_write_string("/");
                    serial_write_u64(runtime->transfer_outstanding ?
                                     1ULL : 0ULL);
                    serial_write_string("/");
                    serial_write_u64(runtime->expected_trb_physical);
                    serial_write_string("/");
                    serial_write_u64((uint64_t)runtime->producer_index);
                    serial_write_string("/");
                    serial_write_u64(runtime->producer_cycle ? 1ULL : 0ULL);
                    serial_write_string("/");
                    serial_write_u64((uint64_t)runtime->submitted_transfers);
                    serial_write_string("/");
                    serial_write_u64((uint64_t)runtime->completed_transfers);
                    serial_write_string("\n");
                }
                return false;
            }
        }
    }
    return true;
}

static bool m60_consume_hid_event_mapped(struct xhci_state *active,
                                          const struct xhci_trb *event,
                                          uint32_t *completed) {
    uint32_t before;
    if ((active == NULL) || (event == NULL) || (completed == NULL)) {
        return false;
    }
    before = *completed;
    return m52_complete_event(active, event, completed) &&
           (*completed == before + 1U);
}

bool xhci_consume_hid_transfer_event(struct xhci_state *state,
                                     const struct xhci_trb *event,
                                     bool rearm_after_completion) {
    const struct xhci_state *published;
    struct xhci_state *active;
    volatile void *mapping = NULL;
    volatile uint8_t *mmio;
    uint32_t completed = 0U;
    bool success = false;

    if ((state == NULL) || (event == NULL)) { return false; }
    published = xhci_get_state();
    if ((published == NULL) || !published->controller_running ||
        !state->controller_running ||
        (state->mmio_physical != published->mmio_physical) ||
        (state->event_ring_physical != published->event_ring_physical) ||
        (state->addressed_count != published->addressed_count) ||
        !vmm_map_mmio_region(published->mmio_physical,
                             M52_MMIO_WINDOW_SIZE, &mapping)) {
        if (mapping != NULL) {
            (void)vmm_unmap_mmio_region(mapping, M52_MMIO_WINDOW_SIZE);
        }
        return false;
    }

    active = (struct xhci_state *)(uintptr_t)published;
    mmio = (volatile uint8_t *)mapping;
    success = m60_consume_hid_event_mapped(active, event, &completed) &&
              (completed == 1U);
    if (success && rearm_after_completion) {
        (void)m60_rearm_hid_endpoints(active, mmio);
    }
    *state = *active;
    (void)vmm_unmap_mmio_region(mapping, M52_MMIO_WINDOW_SIZE);
    return success;
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
    bool success = false;
    const char *failure_stage = "wait";

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
    consumed = m52_consumed_events(active);
    event_index = (uint16_t)(consumed % XHCI_EVENT_RING_TRBS);
    event_cycle = (((consumed / XHCI_EVENT_RING_TRBS) & 1ULL) == 0ULL);

    if (!m60_rearm_hid_endpoints(active, mmio)) {
        failure_stage = "rearm";
        goto out;
    }

    for (attempt = 0U;
         (attempt < wait_limit) && (completed < completion_goal);
         ++attempt) {
        struct xhci_trb event;
        uint32_t control = event_ring[event_index].control;
        uint8_t type;
        uint16_t next_index;
        bool next_cycle;
        bool should_rearm;
        const uint32_t interrupter = active->capabilities.runtime_offset +
                                     M52_RUNTIME_INTERRUPTER0;

        if (((control & M52_TRB_CYCLE) != 0U) != event_cycle) {
            x86_64_pause();
            continue;
        }
        event.parameter = event_ring[event_index].parameter;
        event.status = event_ring[event_index].status;
        event.control = control;
        type = (uint8_t)((event.control >> M52_TRB_TYPE_SHIFT) &
                         M52_TRB_TYPE_MASK);
        if (type != XHCI_TRB_TYPE_TRANSFER_EVENT) {
            failure_stage = "event-type";
            goto out;
        }
        if (!m60_consume_hid_event_mapped(active, &event, &completed)) {
            failure_stage = "consume";
            goto out;
        }
        next_index = (uint16_t)(event_index + 1U);
        next_cycle = event_cycle;
        if (next_index == XHCI_EVENT_RING_TRBS) {
            next_index = 0U;
            next_cycle = !next_cycle;
        }
        m52_barrier();
        if (interrupter > M52_MMIO_WINDOW_SIZE - 0x20U) {
            failure_stage = "interrupter";
            goto out;
        }
        m52_mmio_write64(mmio, interrupter + 0x18U,
            (active->event_ring_physical +
             ((uint64_t)next_index * XHCI_TRB_SIZE)) | (1ULL << 3U));
        event_index = next_index;
        event_cycle = next_cycle;
        should_rearm = (completed < completion_goal) || rearm_after_completion;
        if (should_rearm) {
            (void)m60_rearm_hid_endpoints(active, mmio);
        }
    }

    success = completed >= completion_goal;
out:
    if (!success && (event_virtual != NULL)) {
        static bool traced_poll_failure;
        const uint32_t control = event_ring[event_index].control;
        const bool entry_cycle = (control & M52_TRB_CYCLE) != 0U;
        if (!traced_poll_failure && (entry_cycle == event_cycle)) {
            traced_poll_failure = true;
            serial_write_string("m60-poll-diag: stage=");
            serial_write_string(failure_stage);
            serial_write_string(" consumed/index/cycle/control/param/status=");
            serial_write_u64(m52_consumed_events(active));
            serial_write_string("/");
            serial_write_u64((uint64_t)event_index);
            serial_write_string("/");
            serial_write_u64(event_cycle ? 1ULL : 0ULL);
            serial_write_string("/");
            serial_write_u64((uint64_t)control);
            serial_write_string("/");
            serial_write_u64(event_ring[event_index].parameter);
            serial_write_string("/");
            serial_write_u64((uint64_t)event_ring[event_index].status);
            serial_write_string("\n");
        }
    }
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
