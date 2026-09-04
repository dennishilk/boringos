#ifndef BORING_M61_RUNTIME_HID_H
#define BORING_M61_RUNTIME_HID_H

#include <stdint.h>

#if defined(BORING_M61_PHYSICAL_BREADCRUMBS)
enum m61_runtime_hid_post_code {
    M61_RUNTIME_HID_POST_A_SERVICE_LOOP = 0xc9,
    M61_RUNTIME_HID_POST_B_TRANSFER_EVENT = 0xca,
    M61_RUNTIME_HID_POST_C_EVENT_VALIDATED = 0xcb,
    M61_RUNTIME_HID_POST_D_INPUT_QUEUED = 0xcc,
    M61_RUNTIME_HID_POST_E_INPUT_READ = 0xcd
};

enum m61_runtime_xhci_observation_code {
    M61_RUNTIME_XHCI_POST_HID_ENDPOINTS_ARMED = 0x5e,
    M61_RUNTIME_XHCI_POST_ANY_CYCLE_READY_EVENT = 0x5f,
    M61_RUNTIME_XHCI_POST_PORT_STATUS_AT_HEAD = 0xce,
    M61_RUNTIME_XHCI_POST_OTHER_EVENT_AT_HEAD = 0xcf
};

void boring_m61_runtime_hid_arm(void);
void boring_m61_runtime_hid_post(uint8_t code);
void boring_m61_runtime_xhci_observe(uint8_t code);
#endif

#endif
