#include <stdbool.h>
#include <stdint.h>

#include <boring/usb_mass_storage.h>
#include <boring/xhci.h>

#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
#include <boring/io.h>
uint8_t boring_m61_usb_mass_storage_failure_reason(void);
#endif

bool usb_mass_storage_init_legacy(struct xhci_state *state);

#define USB_MASS_STORAGE_RUNTIME 1
#define usb_mass_storage_init usb_mass_storage_init_legacy
#include "usb_mass_storage_impl.inc"
#undef usb_mass_storage_init
#undef USB_MASS_STORAGE_RUNTIME

static bool m60_same_xhci_topology(const struct xhci_state *caller,
                                   const struct xhci_state *active) {
    return (caller != NULL) && (active != NULL) &&
           caller->controller_running && active->controller_running &&
           (caller->controller_index == active->controller_index) &&
           (caller->device.bdf.bus == active->device.bdf.bus) &&
           (caller->device.bdf.device == active->device.bdf.device) &&
           (caller->device.bdf.function == active->device.bdf.function) &&
           (caller->mmio_physical == active->mmio_physical) &&
           (caller->event_ring_physical == active->event_ring_physical);
}

bool usb_mass_storage_init(struct xhci_state *state) {
    uint8_t index;
    if (state == NULL) { return false; }
    for (index = 0U; index < xhci_controller_count(); ++index) {
        struct xhci_state *active = xhci_get_controller(index);
        if ((active == NULL) || !active->controller_running ||
            (active->addressed_count == 0U)) {
            continue;
        }
        if (usb_mass_storage_init_legacy(active)) {
            *state = *active;
            return true;
        }
    }
    return false;
}
