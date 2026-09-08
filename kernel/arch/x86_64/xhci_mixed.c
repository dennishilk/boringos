#include <stdbool.h>
#include <stdint.h>

#include <boring/vmm.h>
#include <boring/xhci.h>
#include <boring/xhci_mixed.h>
#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
#include <boring/m61_runtime_hid.h>
#include <boring/serial.h>
#endif

#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
#define M66_DIAGNOSTIC_DESCRIPTOR_LIMIT 64U

static uint64_t m66_descriptor_diagnostics_seen;

static uint16_t m66_read_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static void m66_serial_u64(const char *label, uint64_t value) {
    serial_write_string(label);
    serial_write_u64(value);
}

static void m66_serial_hex(const char *label, uint64_t value) {
    serial_write_string(label);
    serial_write_hex_u64(value);
}

static const char *m66_transfer_type(uint8_t attributes) {
    switch (attributes & 0x03U) {
        case 0U: return "control";
        case 1U: return "isochronous";
        case 2U: return "bulk";
        case 3U: return "interrupt";
        default: return "unknown";
    }
}

static void m66_print_descriptor_diagnostics(
    const struct xhci_addressed_device *device, const uint8_t *bytes,
    uint16_t received, uint8_t device_index) {
    uint8_t identity;
    uint64_t bit;
    uint16_t total;
    uint32_t offset = 0U;
    uint8_t descriptor_count = 0U;
    uint8_t current_interface = 0xffU;

    if ((device == NULL) || (bytes == NULL) ||
        (device->topology.depth == 0U)) {
        return;
    }
    identity = (uint8_t)(device->controller_index *
                         XHCI_MAX_ADDRESSED_DEVICES + device_index);
    if (identity >= 64U) { return; }
    bit = 1ULL << identity;
    if ((m66_descriptor_diagnostics_seen & bit) != 0ULL) { return; }
    m66_descriptor_diagnostics_seen |= bit;
    serial_write_string("M66 HID DEVICE");
    m66_serial_u64(" controller=", device->controller_index);
    m66_serial_u64(" root_port=", device->topology.root_port);
    m66_serial_hex(" route=", device->topology.route_string);
    m66_serial_u64(" depth=", device->topology.depth);
    m66_serial_u64(" downstream_port=", device->topology.downstream_port);
    m66_serial_u64(" speed=", device->speed);
    m66_serial_hex(" vid=", device->descriptors.vendor_id);
    m66_serial_hex(" pid=", device->descriptors.product_id);
    m66_serial_hex(" device_class=", device->descriptors.device_class);
    m66_serial_u64(" config_total=",
                   device->descriptors.configuration_length);
    m66_serial_u64(" interfaces=", device->descriptors.interface_count);
    serial_write_string("\n");

    if ((received < 9U) || (bytes[1] != XHCI_USB_DESCRIPTOR_CONFIGURATION)) {
        serial_write_string("M66 HID DIAGNOSTIC STOP configuration header\n");
        return;
    }
    total = m66_read_le16(&bytes[2]);
    if ((total < 9U) || (total > received) ||
        (total > XHCI_DESCRIPTOR_BUFFER_BYTES)) {
        serial_write_string("M66 HID DIAGNOSTIC STOP configuration bounds\n");
        return;
    }
    while (offset < total) {
        uint8_t length;
        uint8_t type;
        if (descriptor_count == M66_DIAGNOSTIC_DESCRIPTOR_LIMIT) {
            serial_write_string("M66 HID DIAGNOSTIC STOP descriptor limit\n");
            return;
        }
        ++descriptor_count;
        if ((uint32_t)total - offset < 2U) {
            serial_write_string("M66 HID DIAGNOSTIC STOP descriptor tail\n");
            return;
        }
        length = bytes[offset];
        type = bytes[offset + 1U];
        if ((length < 2U) ||
            ((uint32_t)length > (uint32_t)total - offset)) {
            serial_write_string("M66 HID DIAGNOSTIC STOP descriptor bounds\n");
            return;
        }
        if ((type == XHCI_USB_DESCRIPTOR_INTERFACE) && (length >= 9U)) {
            current_interface = bytes[offset + 2U];
            serial_write_string("M66 HID INTERFACE");
            m66_serial_u64(" number=", current_interface);
            m66_serial_u64(" alternate=", bytes[offset + 3U]);
            m66_serial_hex(" class=", bytes[offset + 5U]);
            m66_serial_hex(" subclass=", bytes[offset + 6U]);
            m66_serial_hex(" protocol=", bytes[offset + 7U]);
            m66_serial_u64(" endpoints=", bytes[offset + 4U]);
            serial_write_string("\n");
        } else if ((type == XHCI_USB_DESCRIPTOR_HID) && (length >= 6U)) {
            const uint8_t subordinate_count = bytes[offset + 5U];
            uint8_t report_type = 0U;
            uint16_t report_length = 0U;
            uint8_t subordinate;
            for (subordinate = 0U; subordinate < subordinate_count;
                 ++subordinate) {
                const uint32_t entry = offset + 6U +
                                       (uint32_t)subordinate * 3U;
                if ((entry + 3U > offset + length) ||
                    (entry + 3U > total)) {
                    break;
                }
                if (bytes[entry] == XHCI_USB_DESCRIPTOR_REPORT) {
                    report_type = bytes[entry];
                    report_length = m66_read_le16(&bytes[entry + 1U]);
                    break;
                }
            }
            serial_write_string("M66 HID DESCRIPTOR");
            m66_serial_u64(" interface=", current_interface);
            m66_serial_hex(" version=", m66_read_le16(&bytes[offset + 2U]));
            m66_serial_u64(" country=", bytes[offset + 4U]);
            m66_serial_u64(" subordinate=", subordinate_count);
            m66_serial_hex(" report_type=", report_type);
            m66_serial_u64(" report_length=", report_length);
            serial_write_string("\n");
        } else if ((type == XHCI_USB_DESCRIPTOR_ENDPOINT) && (length >= 7U)) {
            const uint8_t address = bytes[offset + 2U];
            serial_write_string("M66 HID ENDPOINT");
            m66_serial_u64(" interface=", current_interface);
            m66_serial_hex(" address=", address);
            serial_write_string(" direction=");
            serial_write_string((address & 0x80U) != 0U ? "IN" : "OUT");
            serial_write_string(" transfer=");
            serial_write_string(m66_transfer_type(bytes[offset + 3U]));
            m66_serial_u64(" max_packet=",
                           m66_read_le16(&bytes[offset + 4U]));
            m66_serial_u64(" interval=", bytes[offset + 6U]);
            serial_write_string("\n");
        }
        offset += length;
    }
}

static void m66_print_rejection(enum xhci_hid_rejection_reason reason) {
    serial_write_string("M66 HID REJECTION reason=");
    serial_write_string(xhci_hid_rejection_reason_name(reason));
    serial_write_string("\n");
}
#endif

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
        enum xhci_hid_rejection_reason rejection = XHCI_HID_REJECT_NONE;

        saved[index] = *device;
        if (!device->addressed || !device->descriptors_ready ||
            (device->descriptor_buffer_physical == 0ULL) ||
            (device->descriptors.configuration_length == 0U) ||
            !vmm_pmm_frame_to_hhdm(device->descriptor_buffer_physical,
                                   &descriptor_virtual)) {
            return false;
        }
#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
        m66_print_descriptor_diagnostics(
            device, (const uint8_t *)descriptor_virtual,
            device->descriptors.configuration_length, index);
#endif
        classification = xhci_classify_hid_configuration_ex(
            (const uint8_t *)descriptor_virtual,
            device->descriptors.configuration_length, device->speed,
            device->descriptors.vendor_id, device->descriptors.product_id,
            &rejection);
        if (classification == XHCI_HID_CLASS_INVALID) {
#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
            if (device->topology.depth != 0U) {
                m66_print_rejection(rejection);
                boring_m66_physical_usb_mouse_witness(
                    (uint8_t)M66_POST_DOWNSTREAM_HID_UNSUPPORTED);
            }
#endif
            return false;
        }
        if ((classification == XHCI_HID_CLASS_SUPPORTED) ||
            (classification == XHCI_HID_CLASS_VALID_UNSUPPORTED)) {
#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
            if (device->topology.depth != 0U) {
                boring_m66_physical_usb_mouse_witness(
                    (uint8_t)M66_POST_CONFIGURATION_VALID);
            }
#endif
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

#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
    {
        bool downstream_hid = false;
        bool downstream_mouse = false;
        for (index = 0U; index < hid_count; ++index) {
            const struct xhci_addressed_device *device = &active->addressed[index];
            uint8_t endpoint_index;
            if (device->topology.depth == 0U) { continue; }
            downstream_hid = true;
            for (endpoint_index = 0U;
                 endpoint_index < device->hid_configuration.endpoint_count;
                 ++endpoint_index) {
                const enum xhci_hid_report_format format =
                    device->hid_configuration.endpoints[endpoint_index].report_format;
                if ((format == XHCI_HID_REPORT_BOOT_MOUSE) ||
                    (format == XHCI_HID_REPORT_GENERIC_MOUSE)) {
                    downstream_mouse = true;
                    boring_m66_physical_usb_mouse_witness(
                        (uint8_t)M66_POST_DOWNSTREAM_HID_SUPPORTED);
                }
            }
        }
        if (downstream_hid && (!configured || !downstream_mouse)) {
            for (index = 0U; index < hid_count; ++index) {
                const struct xhci_addressed_device *device =
                    &active->addressed[index];
                if ((device->topology.depth != 0U) &&
                    (device->hid_rejection_reason != XHCI_HID_REJECT_NONE)) {
                    m66_print_rejection(device->hid_rejection_reason);
                    break;
                }
            }
            boring_m66_physical_usb_mouse_witness(
                (uint8_t)M66_POST_DOWNSTREAM_HID_UNSUPPORTED);
            configured = false;
        }
    }
#endif

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
