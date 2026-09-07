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
