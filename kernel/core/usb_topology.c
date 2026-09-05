#include <stddef.h>
#include <stdint.h>

#include <boring/usb_topology.h>

#define USB_HUB_DESCRIPTOR_TYPE 0x29U
#define USB_HUB_CHAR_POWER_MASK 0x0003U
#define USB_HUB_CHAR_COMPOUND (1U << 2)
#define USB_HUB_CHAR_TTT_SHIFT 5U
#define USB_HUB_CHAR_TTT_MASK 0x0003U
#define USB_HUB_CHAR_MTT (1U << 7)

#define USB_PORT_CONNECTION (1U << 0)
#define USB_PORT_ENABLE (1U << 1)
#define USB_PORT_RESET (1U << 4)
#define USB_PORT_POWER (1U << 8)
#define USB_PORT_LOW_SPEED (1U << 9)
#define USB_PORT_HIGH_SPEED (1U << 10)
#define USB_PORT_CHANGE_VALID_MASK 0x001fU

static uint16_t little16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static bool speed_valid(uint8_t speed) {
    return (speed >= BORING_USB_SPEED_FULL) &&
           (speed <= BORING_USB_SPEED_SUPER_PLUS);
}

bool boring_usb_topology_validate(const struct boring_usb_topology *topology) {
    uint8_t index;
    if ((topology == NULL) || (topology->root_port == 0U) ||
        !speed_valid(topology->speed) ||
        (topology->depth > BORING_USB_ROUTE_DEPTH_MAX)) {
        return false;
    }
    if (topology->depth == 0U) {
        return (topology->route_string == 0U) &&
               (topology->parent_hub_slot == 0U) &&
               (topology->downstream_port == 0U) &&
               (topology->tt_hub_slot == 0U) &&
               (topology->tt_port == 0U);
    }
    if ((topology->parent_hub_slot == 0U) ||
        (topology->downstream_port == 0U) ||
        (topology->downstream_port > BORING_USB_HUB_PORT_MAX)) {
        return false;
    }
    for (index = 0U; index < topology->depth; ++index) {
        const uint8_t port = (uint8_t)((topology->route_string >>
                                        ((uint32_t)index * 4U)) & 0x0fU);
        if ((port == 0U) || (port > BORING_USB_HUB_PORT_MAX)) {
            return false;
        }
    }
    if ((topology->route_string >> ((uint32_t)topology->depth * 4U)) != 0U) {
        return false;
    }
    if ((topology->tt_hub_slot == 0U) != (topology->tt_port == 0U)) {
        return false;
    }
    if ((topology->tt_port != 0U) &&
        (topology->tt_port > BORING_USB_HUB_PORT_MAX)) {
        return false;
    }
    return true;
}

bool boring_usb_topology_root(struct boring_usb_topology *topology,
                              uint8_t root_port, uint8_t speed) {
    struct boring_usb_topology value = {0U};
    if ((topology == NULL) || (root_port == 0U) || !speed_valid(speed)) {
        return false;
    }
    value.root_port = root_port;
    value.speed = speed;
    *topology = value;
    return true;
}

bool boring_usb_topology_child(const struct boring_usb_topology *parent_hub,
                               uint8_t parent_hub_slot,
                               uint8_t downstream_port,
                               uint8_t child_speed,
                               struct boring_usb_topology *child) {
    struct boring_usb_topology value;
    uint32_t shift;
    if ((parent_hub == NULL) || (child == NULL) ||
        !boring_usb_topology_validate(parent_hub) ||
        (parent_hub_slot == 0U) || (downstream_port == 0U) ||
        (downstream_port > BORING_USB_HUB_PORT_MAX) ||
        !speed_valid(child_speed) ||
        (parent_hub->depth >= BORING_USB_ROUTE_DEPTH_MAX)) {
        return false;
    }
    value = *parent_hub;
    shift = (uint32_t)parent_hub->depth * 4U;
    value.route_string |= (uint32_t)downstream_port << shift;
    value.speed = child_speed;
    value.depth = (uint8_t)(parent_hub->depth + 1U);
    value.parent_hub_slot = parent_hub_slot;
    value.downstream_port = downstream_port;
    value.tt_hub_slot = 0U;
    value.tt_port = 0U;
    if (!boring_usb_topology_validate(&value)) {
        return false;
    }
    *child = value;
    return true;
}

bool boring_usb_topology_set_tt(struct boring_usb_topology *topology,
                                uint8_t tt_hub_slot, uint8_t tt_port) {
    struct boring_usb_topology value;
    if ((topology == NULL) || !boring_usb_topology_validate(topology) ||
        (topology->depth == 0U) || (tt_hub_slot == 0U) ||
        (tt_port == 0U) || (tt_port > BORING_USB_HUB_PORT_MAX) ||
        ((topology->speed != BORING_USB_SPEED_LOW) &&
         (topology->speed != BORING_USB_SPEED_FULL))) {
        return false;
    }
    value = *topology;
    value.tt_hub_slot = tt_hub_slot;
    value.tt_port = tt_port;
    if (!boring_usb_topology_validate(&value)) {
        return false;
    }
    *topology = value;
    return true;
}

bool boring_usb_parse_hub_descriptor(const uint8_t *bytes, uint16_t length,
                                     struct boring_usb_hub_descriptor *hub) {
    struct boring_usb_hub_descriptor value = {0U};
    uint16_t characteristics;
    uint8_t descriptor_length;
    uint8_t ports;
    uint16_t required;
    uint16_t bitmap_bytes;
    if ((bytes == NULL) || (hub == NULL) || (length < 7U)) {
        return false;
    }
    descriptor_length = bytes[0];
    ports = bytes[2];
    if ((bytes[1] != USB_HUB_DESCRIPTOR_TYPE) ||
        (ports == 0U) || (ports > BORING_USB_HUB_DESCRIPTOR_MAX_PORTS)) {
        return false;
    }
    bitmap_bytes = (uint16_t)(((uint16_t)ports + 1U + 7U) / 8U);
    required = (uint16_t)(7U + (2U * bitmap_bytes));
    if ((descriptor_length != required) || (length < descriptor_length) ||
        (descriptor_length > BORING_USB_HUB_DESCRIPTOR_MAX_BYTES)) {
        return false;
    }
    characteristics = little16(&bytes[3]);
    if ((characteristics & USB_HUB_CHAR_POWER_MASK) == 3U) {
        return false;
    }
    value.characteristics = characteristics;
    value.port_count = ports;
    value.power_good_2ms = bytes[5];
    value.controller_current_ma = bytes[6];
    value.individual_port_power =
        (characteristics & USB_HUB_CHAR_POWER_MASK) == 1U;
    value.no_power_switching =
        (characteristics & USB_HUB_CHAR_POWER_MASK) == 2U;
    value.compound = (characteristics & USB_HUB_CHAR_COMPOUND) != 0U;
    value.tt_think_time = (uint8_t)((characteristics >> USB_HUB_CHAR_TTT_SHIFT) &
                                    USB_HUB_CHAR_TTT_MASK);
    value.multi_tt = (characteristics & USB_HUB_CHAR_MTT) != 0U;
    *hub = value;
    return true;
}

bool boring_usb_parse_hub_port_status(const uint8_t *bytes, uint16_t length,
                                      struct boring_usb_hub_port_status *port) {
    struct boring_usb_hub_port_status value = {0U};
    uint16_t status;
    uint16_t change;
    bool low;
    bool high;
    if ((bytes == NULL) || (port == NULL) || (length != 4U)) {
        return false;
    }
    status = little16(bytes);
    change = little16(&bytes[2]);
    if ((change & (uint16_t)~USB_PORT_CHANGE_VALID_MASK) != 0U) {
        return false;
    }
    low = (status & USB_PORT_LOW_SPEED) != 0U;
    high = (status & USB_PORT_HIGH_SPEED) != 0U;
    if (low && high) {
        return false;
    }
    value.status = status;
    value.change = change;
    value.connected = (status & USB_PORT_CONNECTION) != 0U;
    value.enabled = (status & USB_PORT_ENABLE) != 0U;
    value.reset = (status & USB_PORT_RESET) != 0U;
    value.powered = (status & USB_PORT_POWER) != 0U;
    if (value.connected) {
        value.speed = low ? BORING_USB_SPEED_LOW :
                      (high ? BORING_USB_SPEED_HIGH : BORING_USB_SPEED_FULL);
    }
    *port = value;
    return true;
}
