#ifndef BORING_USB_TOPOLOGY_H
#define BORING_USB_TOPOLOGY_H

#include <stdbool.h>
#include <stdint.h>

#define BORING_USB_ROUTE_DEPTH_MAX 5U
#define BORING_USB_HUB_PORT_MAX 15U
#define BORING_USB_HUB_DESCRIPTOR_MAX_PORTS 15U
#define BORING_USB_HUB_DESCRIPTOR_MAX_BYTES 11U

#define BORING_USB_SPEED_FULL 1U
#define BORING_USB_SPEED_LOW 2U
#define BORING_USB_SPEED_HIGH 3U
#define BORING_USB_SPEED_SUPER 4U
#define BORING_USB_SPEED_SUPER_PLUS 5U

struct boring_usb_topology {
    uint32_t route_string;
    uint8_t root_port;
    uint8_t speed;
    uint8_t depth;
    uint8_t parent_hub_slot;
    uint8_t downstream_port;
    uint8_t tt_hub_slot;
    uint8_t tt_port;
    bool tt_multi;
};

struct boring_usb_hub_descriptor {
    uint16_t characteristics;
    uint8_t port_count;
    uint8_t power_good_2ms;
    uint8_t controller_current_ma;
    bool individual_port_power;
    bool no_power_switching;
    bool compound;
    bool multi_tt;
    uint8_t tt_think_time;
};

struct boring_usb_hub_port_status {
    uint16_t status;
    uint16_t change;
    uint8_t speed;
    bool connected;
    bool enabled;
    bool reset;
    bool powered;
};

bool boring_usb_topology_root(struct boring_usb_topology *topology,
                              uint8_t root_port, uint8_t speed);
bool boring_usb_topology_child(const struct boring_usb_topology *parent_hub,
                               uint8_t parent_hub_slot,
                               uint8_t downstream_port,
                               uint8_t child_speed,
                               struct boring_usb_topology *child);
bool boring_usb_topology_set_tt(struct boring_usb_topology *topology,
                                uint8_t tt_hub_slot, uint8_t tt_port);
bool boring_usb_topology_set_tt_mode(struct boring_usb_topology *topology,
                                     uint8_t tt_hub_slot, uint8_t tt_port,
                                     bool multi_tt);
bool boring_usb_topology_validate(const struct boring_usb_topology *topology);

bool boring_usb_parse_hub_descriptor(const uint8_t *bytes, uint16_t length,
                                     struct boring_usb_hub_descriptor *hub);
bool boring_usb_parse_hub_port_status(const uint8_t *bytes, uint16_t length,
                                      struct boring_usb_hub_port_status *port);

#endif
