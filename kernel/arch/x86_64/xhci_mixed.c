#include <stdbool.h>
#include <stdint.h>

#include <boring/vmm.h>
#include <boring/xhci.h>
#include <boring/xhci_mixed.h>

bool xhci_configure_hid_devices_mixed(struct xhci_state *state) {
    const struct xhci_state *published;
    struct xhci_state *active;
    struct xhci_addressed_device saved[XHCI_MAX_ADDRESSED_DEVICES];
    uint8_t hid_original_index[XHCI_MAX_ADDRESSED_DEVICES];
    uint8_t original_count;
    uint8_t hid_count = 0U;
    uint8_t index;
    bool configured;

    if (state == NULL) { return false; }
    published = xhci_get_controller(state->controller_index);
    if ((published == NULL) || !published->controller_running ||
        (published->addressed_count == 0U) ||
        (published->addressed_count > XHCI_MAX_ADDRESSED_DEVICES)) {
        return false;
    }

    /* active_state is not const; xhci_get_state() exposes a read-only view. */
    active = xhci_get_controller(state->controller_index);
    if (active == NULL) { return false; }
    original_count = active->addressed_count;

    for (index = 0U; index < original_count; ++index) {
        void *descriptor_virtual = NULL;
        const struct xhci_addressed_device *device = &active->addressed[index];
        enum xhci_hid_classification classification;

        saved[index] = *device;
        if (!device->addressed || !device->descriptors_ready ||
            (device->descriptor_buffer_physical == 0ULL) ||
            (device->descriptors.configuration_length == 0U) ||
            !vmm_pmm_frame_to_hhdm(device->descriptor_buffer_physical,
                                   &descriptor_virtual)) {
            return false;
        }
        classification = xhci_classify_hid_configuration(
            (const uint8_t *)descriptor_virtual,
            device->descriptors.configuration_length, device->speed,
            device->descriptors.vendor_id, device->descriptors.product_id);
        if (classification == XHCI_HID_CLASS_INVALID) { return false; }
        if (classification == XHCI_HID_CLASS_SUPPORTED) {
            hid_original_index[hid_count] = index;
            ++hid_count;
        }
    }

    if (hid_count == 0U) {
        *state = *active;
        return true;
    }

    for (index = 0U; index < hid_count; ++index) {
        active->addressed[index] = saved[hid_original_index[index]];
    }
    active->addressed_count = hid_count;

    configured = xhci_configure_hid_devices(active);

    /* Preserve all HID-side runtime changes, including bounded partial failure. */
    for (index = 0U; index < hid_count; ++index) {
        saved[hid_original_index[index]] = active->addressed[index];
    }
    for (index = 0U; index < original_count; ++index) {
        active->addressed[index] = saved[index];
    }
    active->addressed_count = original_count;
    *state = *active;
    return configured;
}
