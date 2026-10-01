#ifndef BORING_E1000_H
#define BORING_E1000_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct e1000_info {
    bool initialized;
    bool ethernet_seen;
    bool link_up;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t mac[6];
};

enum e1000_result {
    E1000_RESULT_OK = 0,
    E1000_RESULT_NO_DEVICE,
    E1000_RESULT_UNSUPPORTED_DEVICE,
    E1000_RESULT_PCI_COMMAND,
    E1000_RESULT_BAR,
    E1000_RESULT_MMIO,
    E1000_RESULT_RESET,
    E1000_RESULT_MAC,
    E1000_RESULT_DMA,
    E1000_RESULT_SETUP
};

enum e1000_result e1000_init(void);
bool e1000_send(const void *frame, size_t length);
bool e1000_receive(void *frame, size_t capacity, size_t *length);
bool e1000_get_info(struct e1000_info *info);

#endif
