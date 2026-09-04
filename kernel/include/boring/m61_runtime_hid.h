#ifndef BORING_M61_RUNTIME_HID_H
#define BORING_M61_RUNTIME_HID_H

#include <stdbool.h>
#include <stdint.h>

#if defined(BORING_M61_PHYSICAL_BREADCRUMBS)
enum m61_post37_control_flow_code {
    M61_POST37_ARM_RETURNED = 0x20,
    M61_POST37_FRAMEBUFFER_PRESENT_RETURNED = 0x21,
    M61_POST37_DISPLAY_PRESENT_RETURNED = 0x22,
    M61_POST37_DISPLAY_EVENT_LOOP_REENTRY = 0x23,
    M61_POST37_EVENT_SYSCALL_ENTRY = 0x24,
    M61_POST37_INPUT_OWNER_TRUE = 0x25,
    M61_POST37_FAST_READY = 0x26,
    M61_POST37_READY_IPC_LISTENER = 0x27,
    M61_POST37_READY_IPC_ENDPOINT = 0x28,
    M61_POST37_READY_INPUT = 0x29,
    M61_POST37_READY_FD = 0x2a,
    M61_POST37_READY_IPC_HUP = 0x2b,
    M61_POST37_POLL_ERROR = 0x2c,
    M61_POST37_INPUT_OWNER_FALSE = 0x2d
};

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
bool boring_m61_runtime_hid_is_armed(void);
void boring_m61_post37_witness(uint8_t code);
void boring_m61_runtime_hid_post(uint8_t code);
void boring_m61_runtime_xhci_observe(uint8_t code);
#endif

#endif
