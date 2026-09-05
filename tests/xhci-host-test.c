#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <boring/usb_hid.h>
#include <boring/xhci.h>

static unsigned failures;

static void check(bool condition, const char *name) {
    if (!condition) {
        fprintf(stderr, "xhci-host-test: FAIL: %s\n", name);
        ++failures;
    }
}

static void put32(uint8_t *bytes, size_t offset, uint32_t value) {
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1U] = (uint8_t)(value >> 8U);
    bytes[offset + 2U] = (uint8_t)(value >> 16U);
    bytes[offset + 3U] = (uint8_t)(value >> 24U);
}

static uint32_t get32(const uint8_t *bytes, size_t offset) {
    return (uint32_t)bytes[offset] |
           ((uint32_t)bytes[offset + 1U] << 8U) |
           ((uint32_t)bytes[offset + 2U] << 16U) |
           ((uint32_t)bytes[offset + 3U] << 24U);
}

static void capabilities_test(void) {
    uint8_t mmio[XHCI_MMIO_WINDOW_SIZE] = {0U};
    struct xhci_capabilities cap;

    mmio[0] = 0x40U;
    put32(mmio, 4U, 32U | (4U << 8U) | (8U << 24U));
    put32(mmio, 8U, (1U << 21U) | (2U << 27U));
    put32(mmio, 0x10U, (0x20U << 16U) | (1U << 2U));
    put32(mmio, 0x14U, 0x1003U);
    put32(mmio, 0x18U, 0x2021U);
    check(xhci_parse_capabilities(mmio, sizeof(mmio), &cap), "valid caps");
    check((cap.capability_length == 0x40U) && (cap.max_slots == 32U) &&
          (cap.max_interrupters == 4U) && (cap.max_ports == 8U),
          "hcs1 fields");
    check((cap.scratchpad_count == 34U) && cap.context_64_bytes,
          "hcs2/hcc fields");
    check((cap.doorbell_offset == 0x1000U) &&
          (cap.runtime_offset == 0x2020U) &&
          (cap.extended_capability_offset == 0x80U), "offset masking");

    put32(mmio, 8U, 0U);
    check(xhci_parse_capabilities(mmio, sizeof(mmio), &cap) &&
          (cap.scratchpad_count == 0U), "scratchpad count zero");
    put32(mmio, 8U, 1U << 27U);
    check(xhci_parse_capabilities(mmio, sizeof(mmio), &cap) &&
          (cap.scratchpad_count == 1U), "scratchpad count low-only");
    put32(mmio, 8U, 1U << 21U);
    check(xhci_parse_capabilities(mmio, sizeof(mmio), &cap) &&
          (cap.scratchpad_count == 32U), "scratchpad count high-only");
    put32(mmio, 8U, (5U << 21U) | (17U << 27U));
    check(xhci_parse_capabilities(mmio, sizeof(mmio), &cap) &&
          (cap.scratchpad_count == 177U), "scratchpad count mixed");
    put32(mmio, 8U, (31U << 21U) | (31U << 27U));
    check(xhci_parse_capabilities(mmio, sizeof(mmio), &cap) &&
          (cap.scratchpad_count == 1023U), "scratchpad count maximum");

    mmio[0] = 0x10U;
    check(!xhci_parse_capabilities(mmio, sizeof(mmio), &cap), "short cap length");
    mmio[0] = 0x40U;
    put32(mmio, 4U, 32U | (4U << 8U) | (65U << 24U));
    check(!xhci_parse_capabilities(mmio, sizeof(mmio), &cap), "port bound");
    put32(mmio, 4U, 32U | (4U << 8U) | (8U << 24U));
    put32(mmio, 0x18U, 0xffffffe0U);
    check(!xhci_parse_capabilities(mmio, sizeof(mmio), &cap), "runtime bound");
    check(!xhci_parse_capabilities(NULL, sizeof(mmio), &cap), "null mmio");
}

static void keyboard_test(void) {
    struct usb_hid_keyboard_state state = {0U, {0U}};
    struct usb_hid_key_transition transitions[12];
    uint8_t modifiers = 0U;
    size_t count = 0U;
    const uint8_t a_down[8] = {2U, 0U, 4U, 0U, 0U, 0U, 0U, 0U};
    const uint8_t a_b[8] = {0U, 0U, 4U, 5U, 0U, 0U, 0U, 0U};
    const uint8_t b_only[8] = {0U, 0U, 5U, 0U, 0U, 0U, 0U, 0U};
    const uint8_t rollover[8] = {0U, 0U, 1U, 1U, 1U, 1U, 1U, 1U};

    check(usb_hid_keyboard_decode(&state, a_down, sizeof(a_down), transitions,
                                  12U, &count, &modifiers), "key first report");
    check((count == 1U) && (transitions[0].usage == 4U) &&
          transitions[0].down && (modifiers == 2U), "key A down");
    check(usb_hid_keyboard_decode(&state, a_b, sizeof(a_b), transitions,
                                  12U, &count, &modifiers) &&
          (count == 1U) && (transitions[0].usage == 5U) &&
          transitions[0].down, "key B added");
    check(usb_hid_keyboard_decode(&state, b_only, sizeof(b_only), transitions,
                                  12U, &count, &modifiers) &&
          (count == 1U) && (transitions[0].usage == 4U) &&
          !transitions[0].down, "key A release");
    check(usb_hid_keyboard_decode(&state, b_only, sizeof(b_only), transitions,
                                  12U, &count, &modifiers) && (count == 0U),
          "valid unchanged keyboard report consumed without transition");
    check(!usb_hid_keyboard_decode(&state, rollover, sizeof(rollover),
                                   transitions, 12U, &count, &modifiers),
          "rollover rejection");
    check(state.keys[0] == 5U, "invalid report leaves state");
    check(!usb_hid_keyboard_decode(&state, a_down, 7U, transitions, 12U,
                                   &count, &modifiers), "key exact length");
}

static void mouse_test(void) {
    struct usb_hid_mouse_report decoded;
    const uint8_t report[4] = {5U, 0xfeU, 3U, 0xffU};
    check(usb_hid_mouse_decode(report, sizeof(report), &decoded), "mouse report");
    check((decoded.buttons == 5U) && (decoded.dx == -2) &&
          (decoded.dy == 3) && (decoded.wheel == -1), "mouse decode");
    check(!usb_hid_mouse_decode(report, 2U, &decoded), "mouse short reject");
}

static void addressing_model_test(void) {
    uint8_t context[256];
    struct xhci_trb event;
    uint16_t packet = 0U;
    uint8_t slot = 0U;
    size_t index;

    for (index = 0U; index < sizeof(context); ++index) { context[index] = 0xa5U; }
    check(xhci_build_address_input_context(context, sizeof(context), false,
                                           32U, 7U, 8U, 5U, 3U, 0x4000ULL),
          "32-byte address context");
    check(get32(context, 4U) == 3U, "input add flags");
    check(get32(context, 32U) == ((3U << 20U) | (1U << 27U)),
          "slot speed/context entries");
    check(get32(context, 36U) == (5U << 16U), "slot root port");
    check(get32(context, 68U) == ((3U << 1U) | (4U << 3U) | (64U << 16U)),
          "EP0 high-speed fields");
    check((get32(context, 72U) == 0x4001U) &&
          (get32(context, 76U) == 0U), "EP0 dequeue cycle");
    check(context[95] == 0U && context[96] == 0xa5U,
          "32-byte context canary");

    for (index = 0U; index < sizeof(context); ++index) { context[index] = 0x5aU; }
    check(xhci_build_address_input_context(context, sizeof(context), true,
                                           64U, 9U, 16U, 6U, 4U, 0x8000ULL),
          "64-byte address context");
    check(get32(context, 64U) == ((4U << 20U) | (1U << 27U)),
          "64-byte slot offset");
    check(get32(context, 132U) ==
          ((3U << 1U) | (4U << 3U) | (512U << 16U)),
          "EP0 superspeed fields");
    check(context[191] == 0U && context[192] == 0x5aU,
          "64-byte context canary");

    check(xhci_ep0_max_packet(1U, &packet) && packet == 8U,
          "full-speed EP0 bound");
    check(xhci_ep0_max_packet(2U, &packet) && packet == 8U,
          "low-speed EP0 bound");
    check(xhci_ep0_max_packet(3U, &packet) && packet == 64U,
          "high-speed EP0 bound");
    check(xhci_ep0_max_packet(5U, &packet) && packet == 512U,
          "super-speed EP0 bound");
    check(!xhci_ep0_max_packet(0U, &packet) &&
          !xhci_ep0_max_packet(6U, &packet), "invalid speed rejection");
    check(!xhci_build_address_input_context(context, 95U, false,
                                            32U, 7U, 8U, 5U, 3U, 0x4000ULL),
          "short context rejection");
    check(!xhci_build_address_input_context(context, sizeof(context), false,
                                            32U, 0U, 8U, 5U, 3U, 0x4000ULL) &&
          !xhci_build_address_input_context(context, sizeof(context), false,
                                            32U, 7U, 8U, 9U, 3U, 0x4000ULL) &&
          !xhci_build_address_input_context(context, sizeof(context), false,
                                            32U, 7U, 8U, 5U, 3U, 0x4001ULL),
          "slot/port/alignment rejection");

    event.parameter = 0x9000ULL;
    event.status = (uint32_t)XHCI_COMPLETION_SUCCESS << 24U;
    event.control = ((uint32_t)XHCI_TRB_TYPE_COMMAND_COMPLETION_EVENT << 10U) |
                    (7U << 24U) | 1U;
    check(xhci_validate_command_completion(&event, 0x9000ULL, 32U, &slot) &&
          slot == 7U, "command completion validation");
    event.parameter = 0x9010ULL;
    check(!xhci_validate_command_completion(&event, 0x9000ULL, 32U, &slot),
          "command pointer rejection");
    event.parameter = 0x9000ULL;
    event.status = 13U << 24U;
    check(!xhci_validate_command_completion(&event, 0x9000ULL, 32U, &slot),
          "completion code rejection");
    event.status = (uint32_t)XHCI_COMPLETION_SUCCESS << 24U;
    event.control = (32U << 10U) | (7U << 24U) | 1U;
    check(!xhci_validate_command_completion(&event, 0x9000ULL, 32U, &slot),
          "event type rejection");
}

static void control_td_test(void) {
    struct xhci_control_td td;
    struct xhci_control_td canary;
    const uint64_t ring = 0x20000ULL;
    const uint64_t buffer = 0x30000ULL;

    check(xhci_build_get_descriptor_control_td(&td, ring, 0U, true, buffer,
                                               XHCI_USB_DESCRIPTOR_DEVICE,
                                               0U, 18U),
          "control TD build");
    check(td.setup.parameter == 0x0012000001000680ULL,
          "little-endian setup packet");
    check((td.setup.status == 8U) &&
          (((td.setup.control >> 10U) & 0x3fU) == XHCI_TRB_TYPE_SETUP_STAGE) &&
          ((td.setup.control & (1U << 6U)) != 0U) &&
          (((td.setup.control >> 16U) & 3U) == 3U),
          "setup stage fields");
    check((td.data.parameter == buffer) && (td.data.status == 18U) &&
          (((td.data.control >> 10U) & 0x3fU) == XHCI_TRB_TYPE_DATA_STAGE) &&
          ((td.data.control & (1U << 16U)) != 0U) &&
          ((td.data.control & (1U << 2U)) != 0U),
          "data stage fields");
    check((((td.status.control >> 10U) & 0x3fU) ==
           XHCI_TRB_TYPE_STATUS_STAGE) &&
          ((td.status.control & (1U << 16U)) == 0U) &&
          ((td.status.control & (1U << 5U)) != 0U),
          "status stage fields");
    check((td.setup_physical == ring) &&
          (td.data_physical == ring + XHCI_TRB_SIZE) &&
          (td.status_physical == ring + (2ULL * XHCI_TRB_SIZE)) &&
          (td.next_producer_index == 3U) && td.next_producer_cycle,
          "producer advancement");

    check(xhci_build_get_descriptor_control_td(
              &td, ring, XHCI_EP0_RING_USABLE - 3U, true, buffer,
              XHCI_USB_DESCRIPTOR_CONFIGURATION, 0U, 9U),
          "terminal bounded TD");
    check((td.setup_physical == ring +
           ((uint64_t)(XHCI_EP0_RING_USABLE - 3U) * XHCI_TRB_SIZE)) &&
          (td.status_physical == ring +
           ((uint64_t)(XHCI_EP0_RING_USABLE - 1U) * XHCI_TRB_SIZE)) &&
          (td.next_producer_index == 0U) && !td.next_producer_cycle,
          "link boundary cycle wrap");

    canary = td;
    check(!xhci_build_get_descriptor_control_td(
              &td, ring, XHCI_EP0_RING_USABLE - 2U, true, buffer,
              XHCI_USB_DESCRIPTOR_DEVICE, 0U, 8U) &&
          memcmp(&td, &canary, sizeof(td)) == 0,
          "no TD ring overflow and unchanged output");
    check(!xhci_build_get_descriptor_control_td(
              &td, ring + 1ULL, 0U, true, buffer,
              XHCI_USB_DESCRIPTOR_DEVICE, 0U, 8U) &&
          !xhci_build_get_descriptor_control_td(
              &td, ring, 0U, true, buffer + 16ULL,
              XHCI_USB_DESCRIPTOR_DEVICE, 0U, 8U),
          "physical alignment validation");
    check(!xhci_build_get_descriptor_control_td(
              &td, ring, 0U, true, buffer,
              XHCI_USB_DESCRIPTOR_DEVICE, 0U, 0U) &&
          !xhci_build_get_descriptor_control_td(
              &td, ring, 0U, true, buffer,
              XHCI_USB_DESCRIPTOR_DEVICE, 0U,
              (uint16_t)(XHCI_DESCRIPTOR_BUFFER_BYTES + 1U)),
          "transfer length validation");
}

static void transfer_event_test(void) {
    const uint64_t ring = 0x40000ULL;
    const uint64_t data = ring + XHCI_TRB_SIZE;
    const uint64_t status = ring + (2ULL * XHCI_TRB_SIZE);
    struct xhci_trb event;
    uint16_t actual = 0U;
    bool short_packet = false;

    event.parameter = status;
    event.status = (uint32_t)XHCI_COMPLETION_SUCCESS << 24U;
    event.control = ((uint32_t)XHCI_TRB_TYPE_TRANSFER_EVENT << 10U) |
                    (1U << 16U) | (7U << 24U) | 1U;
    check(xhci_validate_control_transfer_event(
              &event, ring, 7U, data, status, 18U, false,
              &actual, &short_packet) &&
          (actual == 18U) && !short_packet,
          "status transfer event");

    event.parameter = data;
    event.status = ((uint32_t)XHCI_COMPLETION_SHORT_PACKET << 24U) | 3U;
    check(xhci_validate_control_transfer_event(
              &event, ring, 7U, data, status, 18U, false,
              &actual, &short_packet) &&
          (actual == 15U) && short_packet,
          "short packet residual calculation");
    check(!xhci_validate_control_transfer_event(
              &event, ring, 7U, data, status, 18U, true,
              &actual, &short_packet),
          "short packet still requires status event");

    event.parameter = status;
    event.status = (uint32_t)XHCI_COMPLETION_SUCCESS << 24U;
    check(xhci_validate_control_transfer_event(
              &event, ring, 7U, data, status, 18U, true,
              &actual, &short_packet),
          "post-short status event");

    event.control = (31U << 10U) | (1U << 16U) | (7U << 24U) | 1U;
    check(!xhci_validate_control_transfer_event(
              &event, ring, 7U, data, status, 18U, false,
              &actual, &short_packet), "transfer event type validation");
    event.control = ((uint32_t)XHCI_TRB_TYPE_TRANSFER_EVENT << 10U) |
                    (1U << 16U) | (8U << 24U) | 1U;
    check(!xhci_validate_control_transfer_event(
              &event, ring, 7U, data, status, 18U, false,
              &actual, &short_packet), "transfer slot validation");
    event.control = ((uint32_t)XHCI_TRB_TYPE_TRANSFER_EVENT << 10U) |
                    (2U << 16U) | (7U << 24U) | 1U;
    check(!xhci_validate_control_transfer_event(
              &event, ring, 7U, data, status, 18U, false,
              &actual, &short_packet), "transfer endpoint validation");
    event.control = ((uint32_t)XHCI_TRB_TYPE_TRANSFER_EVENT << 10U) |
                    (1U << 16U) | (7U << 24U) | 1U;
    event.parameter = ring + ((uint64_t)XHCI_EP0_RING_USABLE * XHCI_TRB_SIZE);
    check(!xhci_validate_control_transfer_event(
              &event, ring, 7U, data, status, 18U, false,
              &actual, &short_packet), "transfer pointer ownership");
    event.parameter = status;
    event.status = (2U << 24U);
    check(!xhci_validate_control_transfer_event(
              &event, ring, 7U, data, status, 18U, false,
              &actual, &short_packet), "transfer completion validation");
    event.parameter = data;
    event.status = ((uint32_t)XHCI_COMPLETION_SHORT_PACKET << 24U) | 19U;
    check(!xhci_validate_control_transfer_event(
              &event, ring, 7U, data, status, 18U, false,
              &actual, &short_packet), "residual bound validation");
}

static void evaluate_context_test(void) {
    uint8_t context[256];
    size_t index;

    for (index = 0U; index < sizeof(context); ++index) { context[index] = 0xa5U; }
    check(xhci_build_evaluate_ep0_context(context, sizeof(context), false,
                                          32U, 7U, 64U, 0x8000ULL),
          "32-byte Evaluate Context");
    check(get32(context, 4U) == 2U, "Evaluate only EP0 add flag");
    check(get32(context, 68U) ==
          ((3U << 1U) | (4U << 3U) | (64U << 16U)),
          "Evaluate EP0 packet field");
    check(context[95] == 0U && context[96] == 0xa5U,
          "32-byte Evaluate canary");

    for (index = 0U; index < sizeof(context); ++index) { context[index] = 0x5aU; }
    check(xhci_build_evaluate_ep0_context(context, sizeof(context), true,
                                          64U, 9U, 512U, 0xc000ULL),
          "64-byte Evaluate Context");
    check(get32(context, 132U) ==
          ((3U << 1U) | (4U << 3U) | (512U << 16U)),
          "64-byte Evaluate EP0 field");
    check(context[191] == 0U && context[192] == 0x5aU,
          "64-byte Evaluate canary");
    check(!xhci_build_evaluate_ep0_context(context, 95U, false,
                                           32U, 7U, 64U, 0x8000ULL) &&
          !xhci_build_evaluate_ep0_context(context, sizeof(context), false,
                                           32U, 0U, 64U, 0x8000ULL) &&
          !xhci_build_evaluate_ep0_context(context, sizeof(context), false,
                                           32U, 7U, 7U, 0x8000ULL) &&
          !xhci_build_evaluate_ep0_context(context, sizeof(context), false,
                                           32U, 7U, 64U, 0x8001ULL),
          "Evaluate bounds and alignment");
}

static void packet_size_test(void) {
    uint16_t packet = 0U;
    check(xhci_descriptor_ep0_max_packet(1U, 8U, &packet) && packet == 8U,
          "full 8");
    check(xhci_descriptor_ep0_max_packet(1U, 16U, &packet) && packet == 16U,
          "full 16");
    check(xhci_descriptor_ep0_max_packet(1U, 32U, &packet) && packet == 32U,
          "full 32");
    check(xhci_descriptor_ep0_max_packet(1U, 64U, &packet) && packet == 64U,
          "full 64");
    check(xhci_descriptor_ep0_max_packet(2U, 8U, &packet) && packet == 8U,
          "low 8");
    check(xhci_descriptor_ep0_max_packet(3U, 64U, &packet) && packet == 64U,
          "high 64");
    check(xhci_descriptor_ep0_max_packet(4U, 9U, &packet) && packet == 512U,
          "SuperSpeed exponent");
    check(xhci_descriptor_ep0_max_packet(5U, 9U, &packet) && packet == 512U,
          "SuperSpeedPlus exponent");
    check(!xhci_descriptor_ep0_max_packet(2U, 16U, &packet) &&
          !xhci_descriptor_ep0_max_packet(3U, 8U, &packet) &&
          !xhci_descriptor_ep0_max_packet(4U, 64U, &packet) &&
          !xhci_descriptor_ep0_max_packet(1U, 9U, &packet) &&
          !xhci_descriptor_ep0_max_packet(6U, 8U, &packet),
          "invalid speed packet combinations");
}

static void descriptor_validation_test(void) {
    uint8_t device[18] = {
        18U, 1U, 0x00U, 0x02U, 0U, 0U, 0U, 64U,
        0x34U, 0x12U, 0x78U, 0x56U, 0x00U, 0x01U, 1U, 2U, 3U, 1U
    };
    uint8_t configuration[18] = {
        9U, 2U, 18U, 0U, 1U, 1U, 0U, 0x80U, 50U,
        9U, 99U, 0U, 0U, 0U, 0U, 0U, 0U, 0U
    };
    struct xhci_usb_descriptor_facts facts = {0U};
    struct xhci_usb_descriptor_facts before;
    uint16_t packet = 0U;
    uint16_t total = 0U;

    check(xhci_validate_device_descriptor_prefix(device, 8U, 3U, &packet) &&
          packet == 64U, "device prefix validation");
    check(xhci_validate_device_descriptor(device, sizeof(device), 3U, &facts),
          "device descriptor validation");
    check((facts.usb_version == 0x0200U) &&
          (facts.vendor_id == 0x1234U) && (facts.product_id == 0x5678U) &&
          (facts.device_class == 0U) && (facts.configuration_count == 1U) &&
          (facts.ep0_max_packet == 64U), "real device facts parsing");
    check(xhci_configuration_total_length(configuration, 9U, &total) &&
          total == 18U, "configuration header validation");
    check(xhci_validate_configuration_descriptor(configuration,
                                                 sizeof(configuration), &facts),
          "configuration structural validation");
    check((facts.configuration_length == 18U) &&
          (facts.interface_count == 1U), "configuration facts parsing");

    before = facts;
    device[0] = 17U;
    check(!xhci_validate_device_descriptor(device, sizeof(device), 3U, &facts) &&
          memcmp(&facts, &before, sizeof(facts)) == 0,
          "malformed device leaves state unchanged");
    device[0] = 18U;
    device[17] = 0U;
    check(!xhci_validate_device_descriptor(device, sizeof(device), 3U, &facts) &&
          memcmp(&facts, &before, sizeof(facts)) == 0,
          "device needs configuration");
    device[17] = 1U;
    device[7] = 8U;
    check(!xhci_validate_device_descriptor(device, sizeof(device), 3U, &facts) &&
          memcmp(&facts, &before, sizeof(facts)) == 0,
          "device invalid speed MPS leaves state unchanged");
    device[7] = 64U;

    configuration[2] = 8U;
    configuration[3] = 0U;
    check(!xhci_configuration_total_length(configuration, 9U, &total),
          "wTotalLength below nine");
    configuration[2] = 1U;
    configuration[3] = 16U;
    check(!xhci_configuration_total_length(configuration, 9U, &total),
          "wTotalLength above PMM page");
    configuration[2] = 19U;
    configuration[3] = 0U;
    check(!xhci_validate_configuration_descriptor(configuration,
                                                  sizeof(configuration), &facts) &&
          memcmp(&facts, &before, sizeof(facts)) == 0,
          "wTotalLength above received bytes");
    configuration[2] = 18U;
    configuration[9] = 0U;
    check(!xhci_validate_configuration_descriptor(configuration,
                                                  sizeof(configuration), &facts) &&
          memcmp(&facts, &before, sizeof(facts)) == 0,
          "zero-length nested descriptor");
    configuration[9] = 10U;
    check(!xhci_validate_configuration_descriptor(configuration,
                                                  sizeof(configuration), &facts) &&
          memcmp(&facts, &before, sizeof(facts)) == 0,
          "overflowing nested descriptor");
    configuration[9] = 9U;
    configuration[1] = 3U;
    check(!xhci_validate_configuration_descriptor(configuration,
                                                  sizeof(configuration), &facts) &&
          memcmp(&facts, &before, sizeof(facts)) == 0,
          "wrong configuration descriptor type");
}

static void m51_configuration_test(void) {
    uint8_t configuration[37] = {
        9U, 2U, 37U, 0U, 1U, 1U, 0U, 0x80U, 50U,
        9U, 4U, 0U, 0U, 1U, 3U, 1U, 1U, 0U,
        9U, 0x21U, 0x11U, 0x01U, 0U, 1U, 0x22U, 63U, 0U,
        3U, 0x30U, 0xaaU,
        7U, 5U, 0x81U, 3U, 8U, 0U, 10U
    };
    struct xhci_hid_configuration parsed = {0};
    struct xhci_hid_configuration good;
    struct xhci_hid_configuration supported;
    struct xhci_hid_configuration generic;
    struct xhci_control_td td;
    struct xhci_control_td td_before;
    struct xhci_trb event;
    struct xhci_trb command;
    uint8_t context[2304];
    uint64_t rings[XHCI_MAX_HID_ENDPOINTS] = {0x50000ULL, 0ULL, 0ULL, 0ULL};
    uint8_t value = 0U;
    size_t index;

    check(xhci_parse_hid_configuration(configuration, sizeof(configuration),
                                       3U, &parsed), "M51 HID descriptor walk");
    check((parsed.configuration_value == 1U) && (parsed.endpoint_count == 1U) &&
          (parsed.endpoints[0].interface_number == 0U) &&
          (parsed.endpoints[0].interface_subclass ==
           XHCI_USB_HID_SUBCLASS_BOOT) &&
          (parsed.endpoints[0].protocol == 1U) &&
          (parsed.endpoints[0].report_format ==
           XHCI_HID_REPORT_BOOT_KEYBOARD) &&
          (parsed.endpoints[0].endpoint_address == 0x81U) &&
          (parsed.endpoints[0].endpoint_id == 3U) &&
          (parsed.endpoints[0].max_packet == 8U) &&
          (parsed.endpoints[0].interval == 10U) &&
          (parsed.endpoints[0].xhci_interval == 9U),
          "M51 dynamic HID endpoint facts");
    good = parsed;
    check(xhci_select_supported_hid_configuration(
              &good, 0x1234U, 0x5678U, &supported) &&
          (supported.endpoint_count == 1U) &&
          (supported.endpoints[0].report_format ==
           XHCI_HID_REPORT_BOOT_KEYBOARD),
          "M61 Boot HID interface selected explicitly");
    generic = good;
    generic.endpoints[0].interface_subclass = 0U;
    generic.endpoints[0].protocol = 0U;
    generic.endpoints[0].report_format = XHCI_HID_REPORT_UNSUPPORTED;
    check(xhci_select_supported_hid_configuration(
              &generic, 0x1234U, 0x5678U, &supported) &&
          (supported.endpoint_count == 0U),
          "M61 generic protocol-0 HID is not a tablet");
    check(xhci_select_supported_hid_configuration(
              &generic, 0x0627U, 0x0001U, &supported) &&
          (supported.endpoint_count == 1U) &&
          (supported.endpoints[0].report_format ==
           XHCI_HID_REPORT_QEMU_ABSOLUTE_TABLET),
          "M61 explicit QEMU absolute-tablet contract preserved");
    check(xhci_usb_endpoint_id(0x81U, &value) && value == 3U &&
          xhci_usb_endpoint_id(0x02U, &value) && value == 4U &&
          !xhci_usb_endpoint_id(0x80U, &value) &&
          !xhci_usb_endpoint_id(0x91U, &value),
          "M51 USB endpoint to xHCI DCI mapping");

    configuration[30] = 0U;
    check(!xhci_parse_hid_configuration(configuration, sizeof(configuration), 3U, &parsed) &&
          memcmp(&parsed, &good, sizeof(parsed)) == 0,
          "M51 zero-length descriptor leaves state");
    configuration[30] = 8U;
    check(!xhci_parse_hid_configuration(configuration, sizeof(configuration), 3U, &parsed) &&
          memcmp(&parsed, &good, sizeof(parsed)) == 0,
          "M51 descriptor overrun leaves state");
    configuration[30] = 3U;
    configuration[32] = 0x01U;
    check(!xhci_parse_hid_configuration(configuration, sizeof(configuration), 3U, &parsed),
          "M51 HID OUT endpoint rejected");
    configuration[32] = 0x81U;
    configuration[33] = 2U;
    check(!xhci_parse_hid_configuration(configuration, sizeof(configuration), 3U, &parsed),
          "M51 non-interrupt endpoint rejected");
    configuration[33] = 3U;
    configuration[34] = 0U;
    configuration[35] = 0U;
    check(!xhci_parse_hid_configuration(configuration, sizeof(configuration), 3U, &parsed),
          "M51 zero max packet rejected");
    configuration[34] = 8U;
    configuration[36] = 17U;
    check(!xhci_parse_hid_configuration(configuration, sizeof(configuration), 3U, &parsed),
          "M51 invalid HS interval rejected");
    configuration[36] = 10U;
    check(!xhci_parse_hid_configuration(configuration, 36U, 3U, &parsed),
          "M51 truncated configuration rejected");
    check(!xhci_parse_hid_configuration(configuration, sizeof(configuration), 4U, &parsed),
          "M51 unsupported SuperSpeed companion path rejected");
    {
        uint8_t duplicate[44];
        memcpy(duplicate, configuration, sizeof(configuration));
        duplicate[2] = 44U;
        duplicate[3] = 0U;
        duplicate[13] = 2U;
        memcpy(&duplicate[37], &configuration[30], 7U);
        check(!xhci_parse_hid_configuration(duplicate, sizeof(duplicate), 3U, &parsed),
              "M51 duplicate endpoint rejected");
    }

    check(xhci_build_set_configuration_control_td(&td, 0x20000ULL, 7U, true, 5U),
          "M51 SET_CONFIGURATION TD");
    check((td.setup.parameter == 0x0000000000050900ULL) &&
          (((td.setup.control >> 10U) & 0x3fU) == XHCI_TRB_TYPE_SETUP_STAGE) &&
          (((td.setup.control >> 16U) & 3U) == 0U) &&
          (td.data_physical == 0ULL) &&
          (((td.status.control >> 10U) & 0x3fU) == XHCI_TRB_TYPE_STATUS_STAGE) &&
          ((td.status.control & (1U << 16U)) != 0U) &&
          ((td.status.control & (1U << 5U)) != 0U) &&
          (td.next_producer_index == 9U) && td.next_producer_cycle,
          "M51 no-data setup/status semantics");
    check(xhci_build_set_configuration_control_td(
              &td, 0x20000ULL, XHCI_EP0_RING_USABLE - 2U, true, 1U) &&
          (td.next_producer_index == 0U) && !td.next_producer_cycle,
          "M51 no-data Link TRB cycle wrap");
    td_before = td;
    check(!xhci_build_set_configuration_control_td(
              &td, 0x20000ULL, XHCI_EP0_RING_USABLE - 1U, true, 1U) &&
          memcmp(&td, &td_before, sizeof(td)) == 0,
          "M51 no-data ring overflow rejected");

    check(xhci_build_hid_set_protocol_control_td(
              &td, 0x20000ULL, 9U, true, 5U),
          "M61 HID SET_PROTOCOL(BOOT) TD");
    check((td.setup.parameter == 0x0000000500000b21ULL) &&
          (((td.setup.control >> 10U) & 0x3fU) ==
           XHCI_TRB_TYPE_SETUP_STAGE) &&
          (((td.setup.control >> 16U) & 3U) == 0U) &&
          (td.data_physical == 0ULL) &&
          (((td.status.control >> 10U) & 0x3fU) ==
           XHCI_TRB_TYPE_STATUS_STAGE) &&
          ((td.status.control & (1U << 16U)) != 0U) &&
          ((td.status.control & (1U << 5U)) != 0U) &&
          (td.next_producer_index == 11U) && td.next_producer_cycle,
          "M61 HID SET_PROTOCOL request is 21/0B/0000/interface/0000");

    event.parameter = 0x20010ULL;
    event.status = (uint32_t)XHCI_COMPLETION_SUCCESS << 24U;
    event.control = ((uint32_t)XHCI_TRB_TYPE_TRANSFER_EVENT << 10U) |
                    (1U << 16U) | (7U << 24U) | 1U;
    check(xhci_validate_no_data_control_event(&event, 0x20000ULL, 7U, 0x20010ULL),
          "M51 no-data completion validation");
    event.control = ((uint32_t)XHCI_TRB_TYPE_TRANSFER_EVENT << 10U) |
                    (3U << 16U) | (7U << 24U) | 1U;
    check(!xhci_validate_no_data_control_event(&event, 0x20000ULL, 7U, 0x20010ULL),
          "M51 wrong endpoint completion rejected");

    for (index = 0U; index < sizeof(context); ++index) { context[index] = 0xa5U; }
    check(xhci_build_configure_hid_context(context, sizeof(context), false,
              32U, 7U, 8U, 5U, 3U, &good, rings),
          "M51 32-byte Configure Endpoint input context");
    check(get32(context, 4U) == ((1U << 0U) | (1U << 3U)), "M51 add-context flags");
    check(get32(context, 32U) == ((3U << 20U) | (3U << 27U)),
          "M51 slot Context Entries");
    check(get32(context, 36U) == (5U << 16U), "M51 slot root port retained");
    check(get32(context, 128U) == (9U << 16U) &&
          get32(context, 132U) == ((3U << 1U) | (7U << 3U) | (8U << 16U)) &&
          get32(context, 136U) == 0x50001U &&
          get32(context, 144U) == (8U | (8U << 16U)),
          "M51 Interrupt-IN endpoint context fields");
    check(context[159] == 0U && context[160] == 0xa5U, "M51 32-byte context canary");

    for (index = 0U; index < sizeof(context); ++index) { context[index] = 0x5aU; }
    check(xhci_build_configure_hid_context(context, sizeof(context), true,
              32U, 7U, 8U, 5U, 3U, &good, rings),
          "M51 64-byte Configure Endpoint input context");
    check(get32(context, 256U) == (9U << 16U) &&
          get32(context, 260U) == ((3U << 1U) | (7U << 3U) | (8U << 16U)) &&
          context[319] == 0U && context[320] == 0x5aU,
          "M51 64-byte context offsets and canary");
    rings[0] = 0x50001ULL;
    check(!xhci_build_configure_hid_context(context, sizeof(context), false,
              32U, 7U, 8U, 5U, 3U, &good, rings),
          "M51 transfer-ring alignment rejected");
    rings[0] = 0x50000ULL;

    check(xhci_build_configure_endpoint_command(&command, 0x60000ULL, 32U, 7U) &&
          (command.parameter == 0x60000ULL) &&
          (((command.control >> 10U) & 0x3fU) == XHCI_TRB_TYPE_CONFIGURE_ENDPOINT) &&
          ((command.control >> 24U) == 7U),
          "M51 Configure Endpoint command construction");
    event.parameter = 0x70000ULL;
    event.status = (uint32_t)XHCI_COMPLETION_SUCCESS << 24U;
    event.control = ((uint32_t)XHCI_TRB_TYPE_COMMAND_COMPLETION_EVENT << 10U) |
                    (7U << 24U) | 1U;
    check(xhci_validate_command_completion(&event, 0x70000ULL, 32U, &value) && value == 7U,
          "M51 Configure Endpoint command completion");
}


static void multi_controller_discovery_test(void) {
    struct boring_pci_inventory inventory = {0};
    struct pci_device devices[XHCI_MAX_CONTROLLERS];
    uint8_t count = 99U;
    bool truncated = true;
    uint8_t index;

    inventory.complete = true;
    check(xhci_collect_controller_devices(&inventory, devices, &count,
                                          &truncated) &&
          (count == 0U) && !truncated,
          "M64 discover zero controllers");

    inventory.stored = 4U;
    inventory.total = 4U;
    inventory.entries[0].bdf = (struct pci_bdf){58U, 0U, 3U};
    inventory.entries[0].vendor_id = 0x3333U;
    inventory.entries[0].device_id = 3U;
    inventory.entries[0].class_code = 0x0cU;
    inventory.entries[0].subclass = 0x03U;
    inventory.entries[0].prog_if = 0x30U;
    inventory.entries[1].bdf = (struct pci_bdf){1U, 0U, 0U};
    inventory.entries[1].class_code = 0x01U;
    inventory.entries[2].bdf = (struct pci_bdf){2U, 0U, 0U};
    inventory.entries[2].vendor_id = 0x1111U;
    inventory.entries[2].device_id = 1U;
    inventory.entries[2].class_code = 0x0cU;
    inventory.entries[2].subclass = 0x03U;
    inventory.entries[2].prog_if = 0x30U;
    inventory.entries[3].bdf = (struct pci_bdf){41U, 0U, 0U};
    inventory.entries[3].vendor_id = 0x2222U;
    inventory.entries[3].device_id = 2U;
    inventory.entries[3].class_code = 0x0cU;
    inventory.entries[3].subclass = 0x03U;
    inventory.entries[3].prog_if = 0x30U;

    check(xhci_collect_controller_devices(&inventory, devices, &count,
                                          &truncated) &&
          (count == 3U) && !truncated,
          "M64 discover three controllers");
    check((devices[0].bdf.bus == 2U) &&
          (devices[1].bdf.bus == 41U) &&
          (devices[2].bdf.bus == 58U),
          "M64 deterministic BDF ordering");
    check((devices[0].vendor_id == 0x1111U) &&
          (devices[1].vendor_id == 0x2222U) &&
          (devices[2].vendor_id == 0x3333U),
          "M64 independent discovery records");

    inventory.stored = XHCI_MAX_CONTROLLERS + 1U;
    inventory.total = inventory.stored;
    for (index = 0U; index < inventory.stored; ++index) {
        inventory.entries[index].bdf =
            (struct pci_bdf){index, 0U, 0U};
        inventory.entries[index].class_code = 0x0cU;
        inventory.entries[index].subclass = 0x03U;
        inventory.entries[index].prog_if = 0x30U;
    }
    check(xhci_collect_controller_devices(&inventory, devices, &count,
                                          &truncated) &&
          (count == XHCI_MAX_CONTROLLERS) && truncated,
          "M64 bounded controller registry");
}

int main(void) {
    multi_controller_discovery_test();
    capabilities_test();
    keyboard_test();
    mouse_test();
    addressing_model_test();
    control_td_test();
    transfer_event_test();
    evaluate_context_test();
    packet_size_test();
    descriptor_validation_test();
    m51_configuration_test();
    if (failures != 0U) { return 1; }
    puts("xhci-host-test: PASS");
    return 0;
}
