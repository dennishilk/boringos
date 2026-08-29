#include <stdbool.h>
#include <stdint.h>

#include <boring/usb_mass_storage.h>
#include <boring/xhci.h>

bool usb_mass_storage_init_legacy(struct xhci_state *state);

#define usb_mass_storage_init usb_mass_storage_init_legacy
#include "usb_mass_storage_impl.inc"
#undef usb_mass_storage_init

static bool m60_same_xhci_topology(const struct xhci_state *caller,
                                   const struct xhci_state *active) {
    uint8_t index;

    if ((caller == NULL) || (active == NULL) ||
        !caller->controller_running || !active->controller_running ||
        (caller->mmio_physical != active->mmio_physical) ||
        (caller->command_ring_physical != active->command_ring_physical) ||
        (caller->event_ring_physical != active->event_ring_physical) ||
        (caller->addressed_count != active->addressed_count)) {
        return false;
    }
    for (index = 0U; index < active->addressed_count; ++index) {
        if ((caller->addressed[index].slot_id != active->addressed[index].slot_id) ||
            (caller->addressed[index].root_port_id !=
             active->addressed[index].root_port_id) ||
            (caller->addressed[index].speed != active->addressed[index].speed) ||
            (caller->addressed[index].descriptor_buffer_physical !=
             active->addressed[index].descriptor_buffer_physical)) {
            return false;
        }
    }
    return true;
}

bool usb_mass_storage_init(struct xhci_state *state) {
    const struct xhci_state *published;
    struct xhci_state *active;
    bool result;

    if (state == NULL) { return false; }
    published = xhci_get_state();
    if ((published == NULL) ||
        !m60_same_xhci_topology(state, published)) {
        return false;
    }
    active = (struct xhci_state *)(uintptr_t)published;
    result = usb_mass_storage_init_legacy(active);
    *state = *active;
    return result;
}
