#ifndef BORING_XHCI_H
#define BORING_XHCI_H

#include <stdbool.h>
#include <stdint.h>

#include <boring/pci.h>

#define XHCI_MMIO_WINDOW_SIZE 65536U
#define XHCI_MAX_PORTS 64U
#define XHCI_MAX_ADDRESSED_DEVICES 8U
#define XHCI_TRB_SIZE 16U
#define XHCI_COMMAND_RING_USABLE 252U
#define XHCI_EVENT_RING_TRBS 256U

#define XHCI_TRB_TYPE_COMMAND_COMPLETION_EVENT 33U
#define XHCI_COMPLETION_SUCCESS 1U

struct xhci_trb {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
};

struct xhci_addressed_device {
    uint64_t input_context_physical;
    uint64_t device_context_physical;
    uint64_t ep0_ring_physical;
    uint8_t root_port_id;
    uint8_t slot_id;
    uint8_t speed;
    bool addressed;
};

struct xhci_capabilities {
    uint8_t capability_length;
    uint8_t max_slots;
    uint8_t max_ports;
    uint16_t max_interrupters;
    uint16_t scratchpad_count;
    uint32_t doorbell_offset;
    uint32_t runtime_offset;
    uint16_t extended_capability_offset;
    bool context_64_bytes;
};

struct xhci_state {
    struct pci_device device;
    struct xhci_capabilities capabilities;
    uint64_t mmio_physical;
    uint64_t dcbaa_physical;
    uint64_t command_ring_physical;
    uint64_t event_ring_physical;
    uint64_t erst_physical;
    uint64_t connected_ports;
    struct xhci_addressed_device addressed[XHCI_MAX_ADDRESSED_DEVICES];
    uint32_t command_completions;
    uint32_t port_events_consumed;
    uint8_t addressed_count;
    bool addressing_truncated;
    bool legacy_handoff_complete;
    bool controller_running;
};

/* Pure bounded capability parser used by the hardware path and host fixtures. */
bool xhci_parse_capabilities(const volatile void *mmio, uint32_t length,
                             struct xhci_capabilities *capabilities);

/* Pure M49 model helpers shared by host fixtures and the hardware path. */
bool xhci_ep0_max_packet(uint8_t speed, uint16_t *max_packet);
bool xhci_build_address_input_context(void *buffer, uint32_t length,
                                      bool context_64_bytes,
                                      uint8_t max_slots, uint8_t slot_id,
                                      uint8_t max_ports, uint8_t root_port_id,
                                      uint8_t speed,
                                      uint64_t ep0_ring_physical);
bool xhci_validate_command_completion(const struct xhci_trb *event,
                                      uint64_t expected_command_physical,
                                      uint8_t max_slots,
                                      uint8_t *slot_id);

/* Initializes one segment-zero xHCI controller through PCI BAR0. The current
 * M48 foundation establishes command/event transport and observes ports; it
 * does not claim USB device addressing or HID endpoint support.
 */
bool xhci_init(struct xhci_state *state);
bool xhci_address_connected(struct xhci_state *state);
const struct xhci_state *xhci_get_state(void);

_Static_assert(sizeof(struct xhci_trb) == XHCI_TRB_SIZE,
               "xHCI TRB must remain 16 bytes");

#endif
