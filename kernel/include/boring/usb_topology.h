#ifndef BORING_USB_TOPOLOGY_H
#define BORING_USB_TOPOLOGY_H

#include <stdbool.h>
#include <stdint.h>

#define BORING_USB_ROUTE_DEPTH_MAX 5U
#define BORING_USB_HUB_PORT_MAX 15U
#define BORING_USB_HUB_DESCRIPTOR_MAX_PORTS 15U
#define BORING_USB_HUB_DESCRIPTOR_MAX_BYTES 11U

#define BORING_USB_HUB_PORT_FEATURE_RESET 4U
#define BORING_USB_HUB_PORT_FEATURE_POWER 8U
#define BORING_USB_HUB_PORT_FEATURE_C_RESET 20U
#define BORING_USB_HUB_PORT_CHANGE_RESET (1U << 4)
#define BORING_USB_HUB_RESET_POLL_INTERVAL_MS 10U
#define BORING_USB_HUB_RESET_TIMEOUT_MS 800U
#define BORING_USB_HUB_RESET_POLL_LIMIT \
    (BORING_USB_HUB_RESET_TIMEOUT_MS / BORING_USB_HUB_RESET_POLL_INTERVAL_MS)

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

enum boring_usb_hub_reset_observation {
    BORING_USB_HUB_RESET_INVALID = 0,
    BORING_USB_HUB_RESET_DISCONNECTED,
    BORING_USB_HUB_RESET_WAIT_RESET,
    BORING_USB_HUB_RESET_WAIT_ENABLE,
    BORING_USB_HUB_RESET_INVALID_SPEED,
    BORING_USB_HUB_RESET_READY
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
enum boring_usb_hub_reset_observation boring_usb_hub_port_reset_observe(
    const struct boring_usb_hub_port_status *port, bool *clear_reset_change);

#endif
