#include <stdint.h>
#include <stdio.h>

#include <boring/xhci_mixed.h>

static int expect_classification(const char *name, const uint8_t *bytes,
                                 uint16_t length, uint8_t speed,
                                 uint16_t vendor_id, uint16_t product_id,
                                 enum xhci_hid_classification expected) {
    enum xhci_hid_classification actual =
        xhci_classify_hid_configuration(bytes, length, speed,
                                        vendor_id, product_id);
    if (actual != expected) {
        fprintf(stderr, "xhci-mixed-host-test: %s classification=%d expected=%d\n",
                name, (int)actual, (int)expected);
        return 1;
    }
    return 0;
}

int main(void) {
    static const uint8_t mass_storage[] = {
        9, 2, 32, 0, 1, 1, 0, 0x80, 50,
        9, 4, 0, 0, 2, 0x08, 0x06, 0x50, 0,
        7, 5, 0x01, 0x02, 64, 0, 0,
        7, 5, 0x82, 0x02, 64, 0, 0
    };
    static const uint8_t keyboard[] = {
        9, 2, 25, 0, 1, 1, 0, 0x80, 50,
        9, 4, 0, 0, 1, 0x03, 0x01, 0x01, 0,
        7, 5, 0x81, 0x03, 8, 0, 10
    };
    static const uint8_t pointer[] = {
        9, 2, 25, 0, 1, 1, 0, 0x80, 50,
        9, 4, 0, 0, 1, 0x03, 0x01, 0x02, 0,
        7, 5, 0x82, 0x03, 8, 0, 10
    };
    static const uint8_t generic_protocol0[] = {
        9, 2, 25, 0, 1, 1, 0, 0x80, 50,
        9, 4, 0, 0, 1, 0x03, 0x00, 0x00, 0,
        7, 5, 0x81, 0x03, 8, 0, 10
    };
    static const uint8_t valid_interrupt_out[] = {
        9, 2, 25, 0, 1, 1, 0, 0x80, 50,
        9, 4, 0, 0, 1, 0x03, 0x01, 0x01, 0,
        7, 5, 0x01, 0x03, 8, 0, 10
    };
    static const uint8_t malformed_hid[] = {
        9, 2, 25, 0, 1, 1, 0, 0x80, 50,
        9, 4, 0, 0, 1, 0x03, 0x01, 0x01, 0,
        7, 5, 0x81, 0x03, 0, 0, 10
    };
    static const uint8_t multiple_hid[] = {
        9, 2, 59, 0, 2, 1, 0, 0x80, 50,
        9, 4, 0, 0, 1, 3, 1, 2, 0,
        9, 0x21, 0x11, 0x01, 0, 1, 0x22, 52, 0,
        7, 5, 0x81, 3, 8, 0, 10,
        9, 4, 1, 0, 1, 3, 0, 0, 0,
        9, 0x21, 0x11, 0x01, 0, 1, 0x22, 16, 0,
        7, 5, 0x82, 3, 8, 0, 10
    };
    static const uint8_t mouse_with_vendor_interface[] = {
        9, 2, 50, 0, 2, 1, 0, 0x80, 50,
        9, 4, 0, 0, 1, 3, 1, 2, 0,
        9, 0x21, 0x11, 0x01, 0, 1, 0x22, 52, 0,
        7, 5, 0x81, 3, 8, 0, 10,
        9, 4, 1, 0, 1, 0xff, 0, 0, 0,
        7, 5, 0x02, 2, 64, 0, 0
    };
    unsigned configured = 0U;
    unsigned skipped = 0U;
    enum xhci_hid_rejection_reason rejection = XHCI_HID_REJECT_NONE;

    if (expect_classification("mass-storage", mass_storage,
                              (uint16_t)sizeof(mass_storage), 2U,
                              0x1234U, 0x5678U,
                              XHCI_HID_CLASS_NOT_HID) != 0) {
        return 1;
    }
    ++skipped;
    if (expect_classification("keyboard", keyboard,
                              (uint16_t)sizeof(keyboard), 2U,
                              0x1234U, 0x5678U,
                              XHCI_HID_CLASS_SUPPORTED) != 0) {
        return 1;
    }
    ++configured;
    if (expect_classification("pointer", pointer,
                              (uint16_t)sizeof(pointer), 2U,
                              0x1234U, 0x5678U,
                              XHCI_HID_CLASS_SUPPORTED) != 0) {
        return 1;
    }
    ++configured;
    if (expect_classification("generic-protocol0", generic_protocol0,
                              (uint16_t)sizeof(generic_protocol0), 2U,
                              0x1234U, 0x5678U,
                              XHCI_HID_CLASS_VALID_UNSUPPORTED) != 0) {
        return 1;
    }
    ++skipped;
    if (expect_classification("qemu-tablet", generic_protocol0,
                              (uint16_t)sizeof(generic_protocol0), 2U,
                              0x0627U, 0x0001U,
                              XHCI_HID_CLASS_SUPPORTED) != 0) {
        return 1;
    }
    ++configured;
    if (expect_classification("valid-interrupt-out", valid_interrupt_out,
                              (uint16_t)sizeof(valid_interrupt_out), 2U,
                              0x1234U, 0x5678U,
                              XHCI_HID_CLASS_VALID_UNSUPPORTED) != 0) {
        return 1;
    }
    ++skipped;
    if (expect_classification("multiple-hid", multiple_hid,
                              (uint16_t)sizeof(multiple_hid), 2U,
                              0x1234U, 0x5678U,
                              XHCI_HID_CLASS_SUPPORTED) != 0) {
        return 1;
    }
    ++configured;
    if (expect_classification(
            "mouse-with-secondary-vendor", mouse_with_vendor_interface,
            (uint16_t)sizeof(mouse_with_vendor_interface), 2U,
            0x1234U, 0x5678U, XHCI_HID_CLASS_SUPPORTED) != 0) {
        return 1;
    }
    ++configured;
    if (expect_classification("malformed-hid", malformed_hid,
                              (uint16_t)sizeof(malformed_hid), 2U,
                              0x1234U, 0x5678U,
                              XHCI_HID_CLASS_INVALID) != 0) {
        return 1;
    }
    if ((xhci_classify_hid_configuration_ex(
             malformed_hid, (uint16_t)sizeof(malformed_hid), 2U,
             0x1234U, 0x5678U, &rejection) != XHCI_HID_CLASS_INVALID) ||
        (rejection != XHCI_HID_REJECT_PACKET_SIZE)) {
        fprintf(stderr,
                "xhci-mixed-host-test: exact packet rejection missing\n");
        return 1;
    }
    rejection = XHCI_HID_REJECT_RUNTIME_CONFIGURATION;
    if ((xhci_classify_hid_configuration_ex(
             valid_interrupt_out, (uint16_t)sizeof(valid_interrupt_out), 2U,
             0x1234U, 0x5678U, &rejection) !=
         XHCI_HID_CLASS_VALID_UNSUPPORTED) ||
        (rejection != XHCI_HID_REJECT_NONE)) {
        fprintf(stderr,
                "xhci-mixed-host-test: valid OUT gained rejection reason\n");
        return 1;
    }
    if ((configured != 5U) || (skipped != 3U)) {
        fprintf(stderr, "xhci-mixed-host-test: dispatch count mismatch\n");
        return 1;
    }

    puts("xhci-mixed-host-test: mass-storage SKIPPED");
    puts("xhci-mixed-host-test: keyboard CONFIGURED");
    puts("xhci-mixed-host-test: pointer CONFIGURED");
    puts("xhci-mixed-host-test: generic protocol-0 HID VALID_UNSUPPORTED");
    puts("xhci-mixed-host-test: explicit QEMU tablet CONFIGURED");
    puts("xhci-mixed-host-test: valid Interrupt-OUT endpoint SKIPPED");
    puts("xhci-mixed-host-test: secondary HID/vendor interfaces SAFE");
    puts("xhci-mixed-host-test: malformed HID REJECTED");
    puts("xhci-mixed-host-test: exact rejection reason EXPOSED");
    puts("xhci-mixed-host-test: PASS");
    return 0;
}
