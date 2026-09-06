#include <stdbool.h>
#include <stdint.h>

#include <boring/xhci.h>
#include <boring/xhci_mixed.h>

enum xhci_hid_classification xhci_classify_hid_configuration_ex(
    const uint8_t *bytes, uint16_t received, uint8_t speed,
    uint16_t vendor_id, uint16_t product_id,
    enum xhci_hid_rejection_reason *reason) {
    struct xhci_hid_configuration configuration;
    struct xhci_hid_configuration supported;

    if (reason == NULL) {
        return XHCI_HID_CLASS_INVALID;
    }
    *reason = XHCI_HID_REJECT_NONE;
    if (!xhci_parse_hid_configuration_ex(
            bytes, received, speed, &configuration, reason)) {
        return XHCI_HID_CLASS_INVALID;
    }
    if (configuration.hid_interface_count == 0U) {
        return XHCI_HID_CLASS_NOT_HID;
    }
    if (!xhci_select_supported_hid_configuration(
            &configuration, vendor_id, product_id, &supported)) {
        *reason = XHCI_HID_REJECT_RUNTIME_CONFIGURATION;
        return XHCI_HID_CLASS_INVALID;
    }
    if (supported.endpoint_count == 0U) {
        return XHCI_HID_CLASS_VALID_UNSUPPORTED;
    }
    return XHCI_HID_CLASS_SUPPORTED;
}

enum xhci_hid_classification xhci_classify_hid_configuration(
    const uint8_t *bytes, uint16_t received, uint8_t speed,
    uint16_t vendor_id, uint16_t product_id) {
    enum xhci_hid_rejection_reason reason = XHCI_HID_REJECT_NONE;
    return xhci_classify_hid_configuration_ex(
        bytes, received, speed, vendor_id, product_id, &reason);
}
