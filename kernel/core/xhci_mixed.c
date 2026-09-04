#include <stdbool.h>
#include <stdint.h>

#include <boring/xhci.h>
#include <boring/xhci_mixed.h>

static uint16_t read_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

enum xhci_hid_classification xhci_classify_hid_configuration(
    const uint8_t *bytes, uint16_t received, uint8_t speed,
    uint16_t vendor_id, uint16_t product_id) {
    uint16_t total;
    uint32_t offset = 0U;
    bool claims_hid = false;
    struct xhci_hid_configuration configuration;
    struct xhci_hid_configuration supported;

    if ((bytes == NULL) || (received < 9U) ||
        (received > XHCI_DESCRIPTOR_BUFFER_BYTES) ||
        (bytes[0] < 9U) ||
        (bytes[1] != XHCI_USB_DESCRIPTOR_CONFIGURATION)) {
        return XHCI_HID_CLASS_INVALID;
    }
    total = read_le16(&bytes[2]);
    if ((total < 9U) || (total > received) ||
        (total > XHCI_DESCRIPTOR_BUFFER_BYTES)) {
        return XHCI_HID_CLASS_INVALID;
    }

    while (offset < total) {
        uint8_t length;
        uint8_t type;
        if ((uint32_t)total - offset < 2U) {
            return XHCI_HID_CLASS_INVALID;
        }
        length = bytes[offset];
        type = bytes[offset + 1U];
        if ((length < 2U) ||
            ((uint32_t)length > (uint32_t)total - offset)) {
            return XHCI_HID_CLASS_INVALID;
        }
        if (type == XHCI_USB_DESCRIPTOR_INTERFACE) {
            if (length < 9U) { return XHCI_HID_CLASS_INVALID; }
            if ((bytes[offset + 3U] == 0U) &&
                (bytes[offset + 5U] == XHCI_USB_CLASS_HID)) {
                claims_hid = true;
            }
        }
        offset += length;
    }
    if (offset != total) { return XHCI_HID_CLASS_INVALID; }
    if (!claims_hid) { return XHCI_HID_CLASS_NOT_HID; }

    if (!xhci_parse_hid_configuration(bytes, received, speed,
                                      &configuration) ||
        !xhci_select_supported_hid_configuration(
            &configuration, vendor_id, product_id, &supported)) {
        return XHCI_HID_CLASS_INVALID;
    }
    if (supported.endpoint_count == 0U) {
        return XHCI_HID_CLASS_NOT_HID;
    }
    return XHCI_HID_CLASS_SUPPORTED;
}
