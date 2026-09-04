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
    static const uint8_t malformed_hid[] = {
        9, 2, 25, 0, 1, 1, 0, 0x80, 50,
        9, 4, 0, 0, 1, 0x03, 0x01, 0x01, 0,
        7, 5, 0x01, 0x03, 8, 0, 10
    };
    unsigned configured = 0U;
    unsigned skipped = 0U;

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
                              XHCI_HID_CLASS_NOT_HID) != 0) {
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
    if (expect_classification("malformed-hid", malformed_hid,
                              (uint16_t)sizeof(malformed_hid), 2U,
                              0x1234U, 0x5678U,
                              XHCI_HID_CLASS_INVALID) != 0) {
        return 1;
    }
    if ((configured != 3U) || (skipped != 2U)) {
        fprintf(stderr, "xhci-mixed-host-test: dispatch count mismatch\n");
        return 1;
    }

    puts("xhci-mixed-host-test: mass-storage SKIPPED");
    puts("xhci-mixed-host-test: keyboard CONFIGURED");
    puts("xhci-mixed-host-test: pointer CONFIGURED");
    puts("xhci-mixed-host-test: generic protocol-0 HID SKIPPED");
    puts("xhci-mixed-host-test: explicit QEMU tablet CONFIGURED");
    puts("xhci-mixed-host-test: malformed HID REJECTED");
    puts("xhci-mixed-host-test: PASS");
    return 0;
}
