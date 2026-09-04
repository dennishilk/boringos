#ifndef BORING_XHCI_MIXED_H
#define BORING_XHCI_MIXED_H

#include <stdbool.h>
#include <stdint.h>

#include <boring/xhci.h>

enum xhci_hid_classification {
    XHCI_HID_CLASS_SUPPORTED = 0,
    XHCI_HID_CLASS_NOT_HID,
    XHCI_HID_CLASS_INVALID
};

/* Pure bounded descriptor classification for mixed-class USB topologies. */
enum xhci_hid_classification xhci_classify_hid_configuration(
    const uint8_t *bytes, uint16_t received, uint8_t speed,
    uint16_t vendor_id, uint16_t product_id);

/*
 * M60 mixed-class dispatch seam. Valid non-HID devices are skipped while the
 * existing strict HID configuration path remains authoritative for HID slots.
 */
bool xhci_configure_hid_devices_mixed(struct xhci_state *state);

#endif
