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

void boring_m61_runtime_hid_arm(void);
void boring_m61_runtime_hid_post(uint8_t code);
#endif

#endif
