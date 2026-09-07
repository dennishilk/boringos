#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <boring/usb_topology.h>

static unsigned failures;

static void check(bool condition, const char *name) {
    if (!condition) {
        fprintf(stderr, "usb-topology-host-test: FAIL: %s\n", name);
        ++failures;
    }
}

static void route_test(void) {
    struct boring_usb_topology root;
    struct boring_usb_topology one;
    struct boring_usb_topology two;
    struct boring_usb_topology value;
    unsigned depth;

    check(boring_usb_topology_root(&root, 7U, BORING_USB_SPEED_HIGH),
          "root topology");
    check((root.route_string == 0U) && (root.depth == 0U) &&
          (root.root_port == 7U) && boring_usb_topology_validate(&root),
          "direct route zero");
    check(boring_usb_topology_child(&root, 4U, 3U,
                                    BORING_USB_SPEED_FULL, &one),
          "single hub child");
    check((one.route_string == 0x3U) && (one.depth == 1U) &&
          (one.root_port == 7U) && (one.parent_hub_slot == 4U) &&
          (one.downstream_port == 3U), "single hub route fields");
    check(boring_usb_topology_child(&one, 9U, 5U,
                                    BORING_USB_SPEED_HIGH, &two),
          "second hub child");
    check((two.route_string == 0x53U) && (two.depth == 2U) &&
          (two.root_port == 7U) && (two.parent_hub_slot == 9U) &&
          (two.downstream_port == 5U), "multi-level route fields");

    check(!boring_usb_topology_child(&root, 4U, 0U,
                                     BORING_USB_SPEED_FULL, &value) &&
          !boring_usb_topology_child(&root, 4U, 16U,
                                     BORING_USB_SPEED_FULL, &value),
          "invalid route nibble rejected");

    value = root;
    for (depth = 0U; depth < BORING_USB_ROUTE_DEPTH_MAX; ++depth) {
        struct boring_usb_topology next;
        check(boring_usb_topology_child(&value, (uint8_t)(depth + 1U), 1U,
                                        BORING_USB_SPEED_HIGH, &next),
              "bounded route depth accepted");
        value = next;
    }
    check(value.depth == BORING_USB_ROUTE_DEPTH_MAX,
          "route reaches exact depth bound");
    check(!boring_usb_topology_child(&value, 7U, 1U,
                                     BORING_USB_SPEED_HIGH, &one),
          "route depth overflow rejected");

    value = two;
    value.route_string |= (uint32_t)1U << 12U;
    check(!boring_usb_topology_validate(&value),
          "bits above active route rejected");
    check(!boring_usb_topology_root(&root, 0U, BORING_USB_SPEED_HIGH) &&
          !boring_usb_topology_root(&root, 1U, 0U),
          "invalid root topology rejected");
}

static void tt_test(void) {
    struct boring_usb_topology hub;
    struct boring_usb_topology child;
    struct boring_usb_topology unchanged;

    check(boring_usb_topology_root(&hub, 2U, BORING_USB_SPEED_HIGH),
          "TT parent root");
    check(boring_usb_topology_child(&hub, 6U, 4U,
                                    BORING_USB_SPEED_FULL, &child),
          "TT full-speed child");
    check(boring_usb_topology_set_tt(&child, 6U, 4U),
          "TT assignment");
    check((child.tt_hub_slot == 6U) && (child.tt_port == 4U) &&
          !child.tt_multi && boring_usb_topology_validate(&child),
          "TT fields retained");
    check(boring_usb_topology_set_tt_mode(&child, 6U, 4U, true) &&
          child.tt_multi && boring_usb_topology_validate(&child),
          "multi-TT assignment");

    unchanged = child;
    check(!boring_usb_topology_set_tt(&child, 0U, 4U) &&
          (child.tt_hub_slot == unchanged.tt_hub_slot),
          "zero TT slot rejected without mutation");
    check(!boring_usb_topology_set_tt(&child, 6U, 16U),
          "TT port bound");

    check(boring_usb_topology_child(&hub, 6U, 5U,
                                    BORING_USB_SPEED_HIGH, &child),
          "high-speed child");
    check(!boring_usb_topology_set_tt(&child, 6U, 5U),
          "TT rejected for high-speed child");
    check(!boring_usb_topology_set_tt(&hub, 6U, 1U),
          "TT rejected for root device");
}

static void hub_descriptor_test(void) {
    struct boring_usb_hub_descriptor hub;
    uint8_t descriptor[10] = {
        10U, 0x29U, 8U, 0xa1U, 0x00U, 25U, 50U, 0x00U, 0x00U, 0xffU
    };

    check(boring_usb_parse_hub_descriptor(descriptor,
                                          (uint16_t)sizeof(descriptor), &hub),
          "valid eight-port hub descriptor");
    check((hub.port_count == 8U) && hub.individual_port_power &&
          !hub.no_power_switching && !hub.compound && hub.multi_tt &&
          (hub.tt_think_time == 1U) && (hub.power_good_2ms == 25U) &&
          (hub.controller_current_ma == 50U), "hub descriptor fields");

    descriptor[1] = 0x2aU;
    check(!boring_usb_parse_hub_descriptor(descriptor,
                                           (uint16_t)sizeof(descriptor), &hub),
          "wrong hub descriptor type rejected");
    descriptor[1] = 0x29U;
    descriptor[2] = 0U;
    check(!boring_usb_parse_hub_descriptor(descriptor,
                                           (uint16_t)sizeof(descriptor), &hub),
          "zero hub ports rejected");
    descriptor[2] = 16U;
    check(!boring_usb_parse_hub_descriptor(descriptor,
                                           (uint16_t)sizeof(descriptor), &hub),
          "hub port bound enforced");
    descriptor[2] = 8U;
    descriptor[0] = 11U;
    check(!boring_usb_parse_hub_descriptor(descriptor,
                                           (uint16_t)sizeof(descriptor), &hub),
          "hub descriptor length mismatch rejected");
    descriptor[0] = 10U;
    descriptor[3] = 3U;
    descriptor[4] = 0U;
    check(!boring_usb_parse_hub_descriptor(descriptor,
                                           (uint16_t)sizeof(descriptor), &hub),
          "reserved power switching mode rejected");
}

static void port_status_test(void) {
    struct boring_usb_hub_port_status port;
    uint8_t status[4] = {0x03U, 0x05U, 0x11U, 0x00U};

    check(boring_usb_parse_hub_port_status(status, (uint16_t)sizeof(status),
                                           &port),
          "valid high-speed hub port status");
    check(port.connected && port.enabled && port.powered && !port.reset &&
          (port.speed == BORING_USB_SPEED_HIGH) &&
          (port.change == 0x0011U), "high-speed status fields");

    status[1] = 0x03U;
    check(boring_usb_parse_hub_port_status(status, (uint16_t)sizeof(status),
                                           &port) &&
          (port.speed == BORING_USB_SPEED_LOW), "low-speed status");
    status[1] = 0x07U;
    check(!boring_usb_parse_hub_port_status(status, (uint16_t)sizeof(status),
                                            &port),
          "contradictory speed rejected");
    status[1] = 0x01U;
    status[2] = 0x20U;
    status[3] = 0x00U;
    check(!boring_usb_parse_hub_port_status(status, (uint16_t)sizeof(status),
                                            &port),
          "unknown change bit rejected");
    check(!boring_usb_parse_hub_port_status(status, 3U, &port),
          "short port status rejected");
}


static void hub_reset_state_test(void) {
    struct boring_usb_hub_port_status port;
    enum boring_usb_hub_reset_observation observation =
        BORING_USB_HUB_RESET_INVALID;
    bool clear_reset_change = false;
    uint8_t resetting[4] = {0x11U, 0x05U, 0x00U, 0x00U};
    uint8_t resetting_enabled[4] = {0x13U, 0x05U, 0x01U, 0x00U};
    uint8_t complete_disabled[4] = {0x01U, 0x05U, 0x00U, 0x00U};
    uint8_t complete[4] = {0x03U, 0x05U, 0x10U, 0x00U};
    uint8_t complete_mixed_change[4] = {0x03U, 0x05U, 0x11U, 0x00U};
    uint8_t disconnected[4] = {0x00U, 0x05U, 0x00U, 0x00U};
    unsigned poll;

    check(BORING_USB_HUB_RESET_POLL_LIMIT == 80U,
          "bounded hub reset poll limit");
    check(boring_usb_parse_hub_port_status(
              complete, (uint16_t)sizeof(complete), &port) &&
          (boring_usb_hub_port_reset_observe(
               &port, &clear_reset_change) == BORING_USB_HUB_RESET_READY) &&
          clear_reset_change,
          "hub reset completes immediately");

    for (poll = 0U; poll < 3U; ++poll) {
        clear_reset_change = true;
        check(boring_usb_parse_hub_port_status(
                  resetting, (uint16_t)sizeof(resetting), &port) &&
              (boring_usb_hub_port_reset_observe(
                   &port, &clear_reset_change) ==
               BORING_USB_HUB_RESET_WAIT_RESET) &&
              !clear_reset_change,
              "hub reset remains active across polls");
    }
    check(boring_usb_parse_hub_port_status(
              complete, (uint16_t)sizeof(complete), &port) &&
          (boring_usb_hub_port_reset_observe(
               &port, &clear_reset_change) == BORING_USB_HUB_RESET_READY),
          "hub reset completes after several polls");

    check(boring_usb_parse_hub_port_status(
              resetting_enabled, (uint16_t)sizeof(resetting_enabled), &port) &&
          (boring_usb_hub_port_reset_observe(
               &port, &clear_reset_change) ==
           BORING_USB_HUB_RESET_WAIT_RESET),
          "enable does not outrun reset completion");
    check(boring_usb_parse_hub_port_status(
              complete_disabled, (uint16_t)sizeof(complete_disabled), &port) &&
          (boring_usb_hub_port_reset_observe(
               &port, &clear_reset_change) ==
           BORING_USB_HUB_RESET_WAIT_ENABLE),
          "hub reset waits for enable after reset clears");

    check(boring_usb_parse_hub_port_status(
              disconnected, (uint16_t)sizeof(disconnected), &port) &&
          (boring_usb_hub_port_reset_observe(
               &port, &clear_reset_change) ==
           BORING_USB_HUB_RESET_DISCONNECTED),
          "disconnect during hub reset fails");
    check(!boring_usb_parse_hub_port_status(resetting, 3U, &port),
          "malformed reset GET_STATUS fails");

    port.status = 0U;
    port.change = 0U;
    port.speed = 0U;
    port.connected = true;
    port.enabled = true;
    port.reset = false;
    port.powered = true;
    check(boring_usb_hub_port_reset_observe(
              &port, &clear_reset_change) ==
          BORING_USB_HUB_RESET_INVALID_SPEED,
          "invalid reset-complete speed fails");

    for (poll = 0U; poll < BORING_USB_HUB_RESET_POLL_LIMIT; ++poll) {
        check(boring_usb_parse_hub_port_status(
                  resetting, (uint16_t)sizeof(resetting), &port),
              "timeout reset status parses");
        observation = boring_usb_hub_port_reset_observe(
            &port, &clear_reset_change);
        if (observation != BORING_USB_HUB_RESET_WAIT_RESET) { break; }
    }
    check((poll == BORING_USB_HUB_RESET_POLL_LIMIT) &&
          (observation == BORING_USB_HUB_RESET_WAIT_RESET),
          "hub reset timeout stays bounded");

    check(boring_usb_parse_hub_port_status(
              complete_mixed_change,
              (uint16_t)sizeof(complete_mixed_change), &port) &&
          (boring_usb_hub_port_reset_observe(
               &port, &clear_reset_change) == BORING_USB_HUB_RESET_READY) &&
          clear_reset_change,
          "C_PORT_RESET completion change requests reset clear");

    resetting[2] = 0x01U;
    check(boring_usb_parse_hub_port_status(
              resetting, (uint16_t)sizeof(resetting), &port) &&
          (boring_usb_hub_port_reset_observe(
               &port, &clear_reset_change) ==
           BORING_USB_HUB_RESET_WAIT_RESET) &&
          !clear_reset_change,
          "unrelated change bit cannot fake reset completion");
}

int main(void) {
    route_test();
    tt_test();
    hub_descriptor_test();
    port_status_test();
    hub_reset_state_test();
    if (failures != 0U) {
        fprintf(stderr, "usb-topology-host-test: %u failure(s)\n", failures);
        return 1;
    }
    puts("usb-topology-host-test: PASS");
    return 0;
}
