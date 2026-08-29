#ifndef BORING_XHCI_H
#define BORING_XHCI_H

#include <stdbool.h>
#include <stdint.h>

#include <boring/pci.h>

#define XHCI_MMIO_WINDOW_SIZE 65536U
#define XHCI_MAX_PORTS 64U

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
    bool legacy_handoff_complete;
    bool controller_running;
};

/* Pure bounded capability parser used by the hardware path and host fixtures. */
bool xhci_parse_capabilities(const volatile void *mmio, uint32_t length,
                             struct xhci_capabilities *capabilities);

/* Initializes one segment-zero xHCI controller through PCI BAR0. The current
 * M48 foundation establishes command/event transport and observes ports; it
 * does not claim USB device addressing or HID endpoint support.
 */
bool xhci_init(struct xhci_state *state);
const struct xhci_state *xhci_get_state(void);

#endif
