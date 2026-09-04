#include <stddef.h>
#include <stdint.h>

#include <boring/xhci.h>

#define XHCI_CAP_MIN_LENGTH 0x20U
#define XHCI_OPERATION_MIN_SIZE 0x40U
#define XHCI_PORT_BASE 0x400U
#define XHCI_PORT_STRIDE 0x10U
#define XHCI_CONTEXT_32_SIZE 32U
#define XHCI_CONTEXT_64_SIZE 64U
#define XHCI_INPUT_CONTEXT_COUNT 3U
#define XHCI_TRB_TYPE_SHIFT 10U
#define XHCI_TRB_TYPE_MASK 0x3fU
#define XHCI_EVENT_ENDPOINT_SHIFT 16U
#define XHCI_EVENT_ENDPOINT_MASK 0x1fU
#define XHCI_EVENT_SLOT_SHIFT 24U
#define XHCI_EVENT_COMPLETION_SHIFT 24U
#define XHCI_EVENT_RESIDUAL_MASK 0x00ffffffU
#define XHCI_TRB_CYCLE (1U << 0)
#define XHCI_TRB_ISP (1U << 2)
#define XHCI_TRB_IOC (1U << 5)
#define XHCI_TRB_IDT (1U << 6)
#define XHCI_TRB_DIRECTION_IN (1U << 16)
#define XHCI_SETUP_TRT_IN_DATA (3U << 16)
#define XHCI_QEMU_HID_VENDOR_ID 0x0627U
#define XHCI_QEMU_HID_PRODUCT_ID 0x0001U

static uint8_t read8(const volatile uint8_t *base, uint32_t offset) {
    return base[offset];
}

static uint32_t read32(const volatile uint8_t *base, uint32_t offset) {
    return *(const volatile uint32_t *)(const volatile void *)(base + offset);
}

static uint16_t little16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

bool xhci_event_dequeue_position(const struct xhci_state *state,
                                 uint16_t *index, bool *cycle) {
    if ((state == NULL) || (index == NULL) || (cycle == NULL)) {
        return false;
    }
    *index = (uint16_t)(state->event_dequeue_count % XHCI_EVENT_RING_TRBS);
    *cycle = (((state->event_dequeue_count / XHCI_EVENT_RING_TRBS) & 1ULL) ==
              0ULL);
    return true;
}

bool xhci_event_dequeue_advance(struct xhci_state *state,
                                uint16_t index, bool cycle,
                                uint16_t *next_index, bool *next_cycle) {
    uint16_t current_index;
    bool current_cycle;
    if ((state == NULL) || (next_index == NULL) || (next_cycle == NULL) ||
        (state->event_dequeue_count == UINT64_MAX) ||
        !xhci_event_dequeue_position(state, &current_index, &current_cycle) ||
        (index != current_index) || (cycle != current_cycle)) {
        return false;
    }
    ++state->event_dequeue_count;
    return xhci_event_dequeue_position(state, next_index, next_cycle);
}

bool xhci_consume_port_status_event(struct xhci_state *state,
                                    const struct xhci_trb *event) {
    uint8_t type;
    uint8_t port_id;
    uint8_t completion;
    if ((state == NULL) || (event == NULL)) { return false; }
    type = (uint8_t)((event->control >> XHCI_TRB_TYPE_SHIFT) &
                     XHCI_TRB_TYPE_MASK);
    port_id = (uint8_t)(event->parameter >> 24U);
    completion = (uint8_t)(event->status >> 24U);
    if ((type != XHCI_TRB_TYPE_PORT_STATUS_EVENT) || (port_id == 0U) ||
        (port_id > state->capabilities.max_ports) ||
        (completion != XHCI_COMPLETION_SUCCESS) ||
        (state->port_events_consumed == UINT32_MAX)) {
        return false;
    }
    ++state->port_events_consumed;
    return true;
}

bool xhci_parse_capabilities(const volatile void *mmio, uint32_t length,
                             struct xhci_capabilities *capabilities) {
    const volatile uint8_t *base = (const volatile uint8_t *)mmio;
    uint32_t hcs1;
    uint32_t hcs2;
    uint32_t hcc1;
    uint32_t port_end;
    struct xhci_capabilities parsed;

    if ((base == NULL) || (capabilities == NULL) ||
        (length < XHCI_CAP_MIN_LENGTH)) {
        return false;
    }
    parsed.capability_length = read8(base, 0U);
    hcs1 = read32(base, 4U);
    hcs2 = read32(base, 8U);
    hcc1 = read32(base, 0x10U);
    parsed.max_slots = (uint8_t)(hcs1 & 0xffU);
    parsed.max_interrupters = (uint16_t)((hcs1 >> 8U) & 0x7ffU);
    parsed.max_ports = (uint8_t)((hcs1 >> 24U) & 0xffU);
    parsed.scratchpad_count = (uint16_t)((((hcs2 >> 21U) & 0x1fU) << 5U) |
                                         ((hcs2 >> 27U) & 0x1fU));
    parsed.doorbell_offset = read32(base, 0x14U) & ~3U;
    parsed.runtime_offset = read32(base, 0x18U) & ~0x1fU;
    parsed.extended_capability_offset =
        (uint16_t)(((hcc1 >> 16U) & 0xffffU) * 4U);
    parsed.context_64_bytes = (hcc1 & (1U << 2)) != 0U;

    port_end = XHCI_PORT_BASE +
               ((uint32_t)parsed.max_ports * XHCI_PORT_STRIDE);
    if ((parsed.capability_length < XHCI_CAP_MIN_LENGTH) ||
        ((uint32_t)parsed.capability_length + XHCI_OPERATION_MIN_SIZE > length) ||
        (parsed.max_slots == 0U) || (parsed.max_ports == 0U) ||
        (parsed.max_ports > XHCI_MAX_PORTS) ||
        (parsed.max_interrupters == 0U) || (port_end > length) ||
        (parsed.doorbell_offset >= length) ||
        (parsed.runtime_offset > length - 0x40U) ||
        ((parsed.extended_capability_offset != 0U) &&
         ((uint32_t)parsed.extended_capability_offset > length - 4U))) {
        return false;
    }
    *capabilities = parsed;
    return true;
}

bool xhci_ep0_max_packet(uint8_t speed, uint16_t *max_packet) {
    uint16_t value;
    if (max_packet == NULL) { return false; }
    switch (speed) {
        case 1U: value = 8U; break;
        case 2U: value = 8U; break;
        case 3U: value = 64U; break;
        case 4U: value = 512U; break;
        case 5U: value = 512U; break;
        default: return false;
    }
    *max_packet = value;
    return true;
}

static void zero_bytes(uint8_t *bytes, uint32_t length) {
    uint32_t index;
    for (index = 0U; index < length; ++index) { bytes[index] = 0U; }
}

static void context_write32(uint8_t *context, uint32_t offset,
                            uint32_t value) {
    context[offset] = (uint8_t)value;
    context[offset + 1U] = (uint8_t)(value >> 8U);
    context[offset + 2U] = (uint8_t)(value >> 16U);
    context[offset + 3U] = (uint8_t)(value >> 24U);
}

bool xhci_build_address_input_context(void *buffer, uint32_t length,
                                      bool context_64_bytes,
                                      uint8_t max_slots, uint8_t slot_id,
                                      uint8_t max_ports, uint8_t root_port_id,
                                      uint8_t speed,
                                      uint64_t ep0_ring_physical) {
    uint8_t *bytes = (uint8_t *)buffer;
    const uint32_t context_size = context_64_bytes ?
                                  XHCI_CONTEXT_64_SIZE : XHCI_CONTEXT_32_SIZE;
    const uint32_t required = context_size * XHCI_INPUT_CONTEXT_COUNT;
    uint16_t max_packet;
    uint8_t *slot;
    uint8_t *ep0;

    if ((bytes == NULL) || (length < required) || (max_slots == 0U) ||
        (slot_id == 0U) || (slot_id > max_slots) || (max_ports == 0U) ||
        (root_port_id == 0U) || (root_port_id > max_ports) ||
        ((ep0_ring_physical & 0x3fULL) != 0ULL) ||
        !xhci_ep0_max_packet(speed, &max_packet)) {
        return false;
    }
    zero_bytes(bytes, required);
    context_write32(bytes, 4U, 3U);
    slot = bytes + context_size;
    ep0 = bytes + (context_size * 2U);
    context_write32(slot, 0U, ((uint32_t)speed << 20U) | (1U << 27U));
    context_write32(slot, 4U, (uint32_t)root_port_id << 16U);
    context_write32(ep0, 4U, (3U << 1U) | (4U << 3U) |
                                  ((uint32_t)max_packet << 16U));
    context_write32(ep0, 8U,
                    (uint32_t)(ep0_ring_physical | 1ULL));
    context_write32(ep0, 12U,
                    (uint32_t)((ep0_ring_physical | 1ULL) >> 32U));
    context_write32(ep0, 16U, 8U);
    return true;
}

bool xhci_validate_command_completion(const struct xhci_trb *event,
                                      uint64_t expected_command_physical,
                                      uint8_t max_slots,
                                      uint8_t *slot_id) {
    const uint8_t type = (event == NULL) ? 0U :
        (uint8_t)((event->control >> XHCI_TRB_TYPE_SHIFT) & XHCI_TRB_TYPE_MASK);
    const uint8_t completion = (event == NULL) ? 0U :
        (uint8_t)(event->status >> XHCI_EVENT_COMPLETION_SHIFT);
    const uint8_t slot = (event == NULL) ? 0U :
        (uint8_t)(event->control >> XHCI_EVENT_SLOT_SHIFT);
    if ((event == NULL) || (slot_id == NULL) ||
        ((expected_command_physical & 0x0fULL) != 0ULL) ||
        (event->parameter != expected_command_physical) ||
        (type != XHCI_TRB_TYPE_COMMAND_COMPLETION_EVENT) ||
        (completion != XHCI_COMPLETION_SUCCESS) ||
        (max_slots == 0U) || (slot == 0U) || (slot > max_slots)) {
        return false;
    }
    *slot_id = slot;
    return true;
}

static bool packet_size_value_valid(uint16_t max_packet) {
    return (max_packet == 8U) || (max_packet == 16U) ||
           (max_packet == 32U) || (max_packet == 64U) ||
           (max_packet == 512U);
}

bool xhci_descriptor_ep0_max_packet(uint8_t speed, uint8_t descriptor_value,
                                    uint16_t *max_packet) {
    uint16_t value;
    if (max_packet == NULL) { return false; }
    switch (speed) {
        case 1U:
            if ((descriptor_value != 8U) && (descriptor_value != 16U) &&
                (descriptor_value != 32U) && (descriptor_value != 64U)) {
                return false;
            }
            value = descriptor_value;
            break;
        case 2U:
            if (descriptor_value != 8U) { return false; }
            value = 8U;
            break;
        case 3U:
            if (descriptor_value != 64U) { return false; }
            value = 64U;
            break;
        case 4U:
        case 5U:
            if (descriptor_value != 9U) { return false; }
            value = 512U;
            break;
        default:
            return false;
    }
    *max_packet = value;
    return true;
}

bool xhci_build_get_descriptor_control_td(struct xhci_control_td *td,
                                          uint64_t ep0_ring_physical,
                                          uint16_t producer_index,
                                          bool producer_cycle,
                                          uint64_t buffer_physical,
                                          uint8_t descriptor_type,
                                          uint8_t descriptor_index,
                                          uint16_t length) {
    struct xhci_control_td built;
    uint8_t setup[8];
    uint64_t setup_parameter = 0ULL;
    uint8_t index;
    uint32_t cycle;

    if ((td == NULL) || (ep0_ring_physical == 0ULL) ||
        ((ep0_ring_physical & 0x3fULL) != 0ULL) ||
        (buffer_physical == 0ULL) ||
        ((buffer_physical & (uint64_t)(XHCI_DESCRIPTOR_BUFFER_BYTES - 1U)) !=
         0ULL) ||
        (length == 0U) || (length > XHCI_DESCRIPTOR_BUFFER_BYTES) ||
        (descriptor_type == 0U) ||
        (producer_index > XHCI_EP0_RING_USABLE - 3U)) {
        return false;
    }

    setup[0] = 0x80U;
    setup[1] = 0x06U;
    setup[2] = descriptor_index;
    setup[3] = descriptor_type;
    setup[4] = 0U;
    setup[5] = 0U;
    setup[6] = (uint8_t)length;
    setup[7] = (uint8_t)(length >> 8U);
    for (index = 0U; index < 8U; ++index) {
        setup_parameter |= (uint64_t)setup[index] << ((uint64_t)index * 8ULL);
    }

    cycle = producer_cycle ? XHCI_TRB_CYCLE : 0U;
    built.setup.parameter = setup_parameter;
    built.setup.status = 8U;
    built.setup.control =
        ((uint32_t)XHCI_TRB_TYPE_SETUP_STAGE << XHCI_TRB_TYPE_SHIFT) |
        XHCI_TRB_IDT | XHCI_SETUP_TRT_IN_DATA | cycle;
    built.data.parameter = buffer_physical;
    built.data.status = (uint32_t)length;
    built.data.control =
        ((uint32_t)XHCI_TRB_TYPE_DATA_STAGE << XHCI_TRB_TYPE_SHIFT) |
        XHCI_TRB_DIRECTION_IN | XHCI_TRB_ISP | cycle;
    built.status.parameter = 0ULL;
    built.status.status = 0U;
    built.status.control =
        ((uint32_t)XHCI_TRB_TYPE_STATUS_STAGE << XHCI_TRB_TYPE_SHIFT) |
        XHCI_TRB_IOC | cycle;
    built.setup_physical = ep0_ring_physical +
                           ((uint64_t)producer_index * XHCI_TRB_SIZE);
    built.data_physical = built.setup_physical + XHCI_TRB_SIZE;
    built.status_physical = built.data_physical + XHCI_TRB_SIZE;
    if (producer_index == XHCI_EP0_RING_USABLE - 3U) {
        built.next_producer_index = 0U;
        built.next_producer_cycle = !producer_cycle;
    } else {
        built.next_producer_index = (uint16_t)(producer_index + 3U);
        built.next_producer_cycle = producer_cycle;
    }
    *td = built;
    return true;
}

static bool trb_pointer_owned(uint64_t pointer, uint64_t ring_physical) {
    const uint64_t end = ring_physical +
                         ((uint64_t)XHCI_EP0_RING_USABLE * XHCI_TRB_SIZE);
    return ((pointer & 0x0fULL) == 0ULL) && (pointer >= ring_physical) &&
           (pointer < end);
}

bool xhci_validate_control_transfer_event(
    const struct xhci_trb *event, uint64_t ep0_ring_physical,
    uint8_t expected_slot_id, uint64_t expected_data_trb_physical,
    uint64_t expected_status_trb_physical, uint16_t requested_length,
    bool expect_status_only, uint16_t *actual_length, bool *short_packet) {
    uint8_t type;
    uint8_t endpoint;
    uint8_t slot;
    uint8_t completion;
    uint32_t residual;

    if ((event == NULL) || (actual_length == NULL) || (short_packet == NULL) ||
        (ep0_ring_physical == 0ULL) ||
        ((ep0_ring_physical & 0x3fULL) != 0ULL) ||
        (expected_slot_id == 0U) || (requested_length == 0U) ||
        (requested_length > XHCI_DESCRIPTOR_BUFFER_BYTES) ||
        !trb_pointer_owned(expected_data_trb_physical, ep0_ring_physical) ||
        !trb_pointer_owned(expected_status_trb_physical, ep0_ring_physical)) {
        return false;
    }
    type = (uint8_t)((event->control >> XHCI_TRB_TYPE_SHIFT) &
                     XHCI_TRB_TYPE_MASK);
    endpoint = (uint8_t)((event->control >> XHCI_EVENT_ENDPOINT_SHIFT) &
                         XHCI_EVENT_ENDPOINT_MASK);
    slot = (uint8_t)(event->control >> XHCI_EVENT_SLOT_SHIFT);
    completion = (uint8_t)(event->status >> XHCI_EVENT_COMPLETION_SHIFT);
    residual = event->status & XHCI_EVENT_RESIDUAL_MASK;
    if ((type != XHCI_TRB_TYPE_TRANSFER_EVENT) || (endpoint != 1U) ||
        (slot != expected_slot_id) || (residual > requested_length) ||
        !trb_pointer_owned(event->parameter, ep0_ring_physical)) {
        return false;
    }
    if (expect_status_only) {
        if ((event->parameter != expected_status_trb_physical) ||
            (completion != XHCI_COMPLETION_SUCCESS) || (residual != 0U)) {
            return false;
        }
        *actual_length = requested_length;
        *short_packet = false;
        return true;
    }
    if ((event->parameter == expected_status_trb_physical) &&
        (completion == XHCI_COMPLETION_SUCCESS) && (residual == 0U)) {
        *actual_length = requested_length;
        *short_packet = false;
        return true;
    }
    if ((event->parameter == expected_data_trb_physical) &&
        (completion == XHCI_COMPLETION_SHORT_PACKET)) {
        *actual_length = (uint16_t)((uint32_t)requested_length - residual);
        *short_packet = true;
        return true;
    }
    return false;
}

bool xhci_build_evaluate_ep0_context(void *buffer, uint32_t length,
                                     bool context_64_bytes,
                                     uint8_t max_slots, uint8_t slot_id,
                                     uint16_t max_packet,
                                     uint64_t ep0_ring_physical) {
    uint8_t *bytes = (uint8_t *)buffer;
    const uint32_t context_size = context_64_bytes ?
                                  XHCI_CONTEXT_64_SIZE : XHCI_CONTEXT_32_SIZE;
    const uint32_t required = context_size * XHCI_INPUT_CONTEXT_COUNT;
    uint8_t *ep0;
    if ((bytes == NULL) || (length < required) || (max_slots == 0U) ||
        (slot_id == 0U) || (slot_id > max_slots) ||
        !packet_size_value_valid(max_packet) ||
        (ep0_ring_physical == 0ULL) ||
        ((ep0_ring_physical & 0x3fULL) != 0ULL)) {
        return false;
    }
    zero_bytes(bytes, required);
    context_write32(bytes, 4U, 2U);
    ep0 = bytes + (context_size * 2U);
    context_write32(ep0, 4U, (3U << 1U) | (4U << 3U) |
                                  ((uint32_t)max_packet << 16U));
    context_write32(ep0, 8U, (uint32_t)(ep0_ring_physical | 1ULL));
    context_write32(ep0, 12U,
                    (uint32_t)((ep0_ring_physical | 1ULL) >> 32U));
    context_write32(ep0, 16U, 8U);
    return true;
}

bool xhci_validate_device_descriptor_prefix(const uint8_t *bytes,
                                            uint16_t received,
                                            uint8_t speed,
                                            uint16_t *max_packet) {
    uint16_t parsed;
    if ((bytes == NULL) || (max_packet == NULL) || (received != 8U) ||
        (bytes[0] != 18U) || (bytes[1] != XHCI_USB_DESCRIPTOR_DEVICE) ||
        !xhci_descriptor_ep0_max_packet(speed, bytes[7], &parsed)) {
        return false;
    }
    *max_packet = parsed;
    return true;
}

bool xhci_validate_device_descriptor(const uint8_t *bytes, uint16_t received,
                                     uint8_t speed,
                                     struct xhci_usb_descriptor_facts *facts) {
    struct xhci_usb_descriptor_facts parsed;
    uint16_t max_packet;
    if ((bytes == NULL) || (facts == NULL) || (received != 18U) ||
        (bytes[0] != 18U) || (bytes[1] != XHCI_USB_DESCRIPTOR_DEVICE) ||
        (bytes[17] == 0U) ||
        !xhci_descriptor_ep0_max_packet(speed, bytes[7], &max_packet)) {
        return false;
    }
    parsed = *facts;
    parsed.usb_version = little16(&bytes[2]);
    parsed.device_class = bytes[4];
    parsed.b_max_packet_size0 = bytes[7];
    parsed.ep0_max_packet = max_packet;
    parsed.vendor_id = little16(&bytes[8]);
    parsed.product_id = little16(&bytes[10]);
    parsed.configuration_count = bytes[17];
    *facts = parsed;
    return true;
}

bool xhci_configuration_total_length(const uint8_t *bytes, uint16_t received,
                                     uint16_t *total_length) {
    uint16_t total;
    if ((bytes == NULL) || (total_length == NULL) || (received != 9U) ||
        (bytes[0] < 9U) ||
        (bytes[1] != XHCI_USB_DESCRIPTOR_CONFIGURATION)) {
        return false;
    }
    total = little16(&bytes[2]);
    if ((total < 9U) || (total > XHCI_DESCRIPTOR_BUFFER_BYTES)) {
        return false;
    }
    *total_length = total;
    return true;
}

bool xhci_validate_configuration_descriptor(
    const uint8_t *bytes, uint16_t received,
    struct xhci_usb_descriptor_facts *facts) {
    struct xhci_usb_descriptor_facts parsed;
    uint16_t total;
    uint32_t offset = 0U;
    if ((bytes == NULL) || (facts == NULL) || (received < 9U) ||
        (received > XHCI_DESCRIPTOR_BUFFER_BYTES) || (bytes[0] < 9U) ||
        (bytes[1] != XHCI_USB_DESCRIPTOR_CONFIGURATION)) {
        return false;
    }
    total = little16(&bytes[2]);
    if ((total < 9U) || (total > XHCI_DESCRIPTOR_BUFFER_BYTES) ||
        (total > received)) {
        return false;
    }
    while (offset < total) {
        uint8_t descriptor_length;
        if (total - offset < 2U) { return false; }
        descriptor_length = bytes[offset];
        if ((descriptor_length < 2U) ||
            ((uint32_t)descriptor_length > (uint32_t)total - offset)) {
            return false;
        }
        offset += descriptor_length;
    }
    if (offset != total) { return false; }
    parsed = *facts;
    parsed.configuration_length = total;
    parsed.interface_count = bytes[4];
    *facts = parsed;
    return true;
}


static bool hid_interrupt_packet_size(uint8_t speed, uint16_t raw,
                                      uint16_t *max_packet) {
    uint16_t packet;
    if ((max_packet == NULL) || ((raw & 0xf800U) != 0U)) { return false; }
    packet = (uint16_t)(raw & 0x07ffU);
    if (packet == 0U) { return false; }
    switch (speed) {
        case 1U:
            if (packet > 64U) { return false; }
            break;
        case 2U:
            if (packet > 8U) { return false; }
            break;
        case 3U:
            if (packet > 1024U) { return false; }
            break;
        default:
            return false;
    }
    *max_packet = packet;
    return true;
}

static bool hid_interrupt_interval(uint8_t speed, uint8_t descriptor_interval,
                                   uint8_t *xhci_interval) {
    uint8_t encoded;
    if ((xhci_interval == NULL) || (descriptor_interval == 0U)) { return false; }
    if ((speed == 1U) || (speed == 2U)) {
        uint16_t units = (uint16_t)descriptor_interval * 8U;
        encoded = 0U;
        while (units > 1U) {
            units >>= 1U;
            ++encoded;
        }
        if ((encoded < 3U) || (encoded > 10U)) { return false; }
    } else if (speed == 3U) {
        if (descriptor_interval > 16U) { return false; }
        encoded = (uint8_t)(descriptor_interval - 1U);
    } else {
        /* SuperSpeed companion descriptors are deliberately outside M51. */
        return false;
    }
    *xhci_interval = encoded;
    return true;
}

bool xhci_usb_endpoint_id(uint8_t endpoint_address, uint8_t *endpoint_id) {
    const uint8_t number = (uint8_t)(endpoint_address & 0x0fU);
    uint8_t id;
    if ((endpoint_id == NULL) || (number == 0U) ||
        ((endpoint_address & 0x70U) != 0U)) {
        return false;
    }
    id = (uint8_t)((number * 2U) +
                   (((endpoint_address & 0x80U) != 0U) ? 1U : 0U));
    if ((id < 2U) || (id > 31U)) { return false; }
    *endpoint_id = id;
    return true;
}

bool xhci_parse_hid_configuration(
    const uint8_t *bytes, uint16_t received, uint8_t speed,
    struct xhci_hid_configuration *configuration) {
    struct xhci_hid_configuration parsed = {0};
    uint16_t total;
    uint32_t offset = 0U;
    bool current_hid = false;
    uint8_t current_interface = 0U;
    uint8_t current_alternate = 0U;
    uint8_t current_subclass = 0U;
    uint8_t current_protocol = 0U;

    if ((bytes == NULL) || (configuration == NULL) || (received < 9U) ||
        (received > XHCI_DESCRIPTOR_BUFFER_BYTES) ||
        (bytes[0] < 9U) || (bytes[1] != XHCI_USB_DESCRIPTOR_CONFIGURATION)) {
        return false;
    }
    total = little16(&bytes[2]);
    if ((total < 9U) || (total > received) ||
        (total > XHCI_DESCRIPTOR_BUFFER_BYTES) || (bytes[5] == 0U)) {
        return false;
    }
    parsed.configuration_value = bytes[5];
    while (offset < total) {
        uint8_t descriptor_length;
        uint8_t descriptor_type;
        if ((uint32_t)total - offset < 2U) { return false; }
        descriptor_length = bytes[offset];
        descriptor_type = bytes[offset + 1U];
        if ((descriptor_length < 2U) ||
            ((uint32_t)descriptor_length > (uint32_t)total - offset)) {
            return false;
        }
        if (descriptor_type == XHCI_USB_DESCRIPTOR_INTERFACE) {
            if (descriptor_length < 9U) { return false; }
            current_interface = bytes[offset + 2U];
            current_alternate = bytes[offset + 3U];
            current_subclass = bytes[offset + 6U];
            current_protocol = bytes[offset + 7U];
            current_hid = (current_alternate == 0U) &&
                          (bytes[offset + 5U] == XHCI_USB_CLASS_HID);
        } else if ((descriptor_type == XHCI_USB_DESCRIPTOR_ENDPOINT) &&
                   current_hid) {
            struct xhci_hid_endpoint_descriptor endpoint = {0};
            uint16_t raw_packet;
            uint8_t endpoint_id;
            uint8_t previous;
            if ((descriptor_length < 7U) ||
                (parsed.endpoint_count == XHCI_MAX_HID_ENDPOINTS)) {
                return false;
            }
            endpoint.endpoint_address = bytes[offset + 2U];
            if ((endpoint.endpoint_address & 0x80U) == 0U) { return false; }
            if ((bytes[offset + 3U] & 0x03U) !=
                XHCI_USB_ENDPOINT_TRANSFER_INTERRUPT) {
                return false;
            }
            if (!xhci_usb_endpoint_id(endpoint.endpoint_address, &endpoint_id)) {
                return false;
            }
            for (previous = 0U; previous < parsed.endpoint_count; ++previous) {
                if (parsed.endpoints[previous].endpoint_id == endpoint_id) {
                    return false;
                }
            }
            raw_packet = little16(&bytes[offset + 4U]);
            if (!hid_interrupt_packet_size(speed, raw_packet,
                                           &endpoint.max_packet) ||
                !hid_interrupt_interval(speed, bytes[offset + 6U],
                                        &endpoint.xhci_interval)) {
                return false;
            }
            endpoint.interface_number = current_interface;
            endpoint.alternate_setting = current_alternate;
            endpoint.interface_subclass = current_subclass;
            endpoint.protocol = current_protocol;
            if ((current_subclass == XHCI_USB_HID_SUBCLASS_BOOT) &&
                (current_protocol == XHCI_USB_HID_PROTOCOL_KEYBOARD)) {
                endpoint.report_format = XHCI_HID_REPORT_BOOT_KEYBOARD;
            } else if ((current_subclass == XHCI_USB_HID_SUBCLASS_BOOT) &&
                       (current_protocol == XHCI_USB_HID_PROTOCOL_MOUSE)) {
                endpoint.report_format = XHCI_HID_REPORT_BOOT_MOUSE;
            }
            endpoint.endpoint_id = endpoint_id;
            endpoint.interval = bytes[offset + 6U];
            parsed.endpoints[parsed.endpoint_count] = endpoint;
            ++parsed.endpoint_count;
        }
        offset += descriptor_length;
    }
    if ((offset != total) || (parsed.endpoint_count == 0U)) { return false; }
    *configuration = parsed;
    return true;
}

bool xhci_select_supported_hid_configuration(
    const struct xhci_hid_configuration *parsed,
    uint16_t vendor_id, uint16_t product_id,
    struct xhci_hid_configuration *supported) {
    struct xhci_hid_configuration selected = {0};
    uint8_t index;

    if ((parsed == NULL) || (supported == NULL) ||
        (parsed->configuration_value == 0U) ||
        (parsed->endpoint_count == 0U) ||
        (parsed->endpoint_count > XHCI_MAX_HID_ENDPOINTS)) {
        return false;
    }
    selected.configuration_value = parsed->configuration_value;
    for (index = 0U; index < parsed->endpoint_count; ++index) {
        struct xhci_hid_endpoint_descriptor endpoint =
            parsed->endpoints[index];
        enum xhci_hid_report_format format = XHCI_HID_REPORT_UNSUPPORTED;

        /* Preserve only the exact usb-tablet fixture used by the established
         * QEMU acceptance; protocol 0 alone never selects that decoder. */
        if ((endpoint.interface_subclass == XHCI_USB_HID_SUBCLASS_BOOT) &&
            (endpoint.protocol == XHCI_USB_HID_PROTOCOL_KEYBOARD)) {
            format = XHCI_HID_REPORT_BOOT_KEYBOARD;
        } else if ((endpoint.interface_subclass ==
                    XHCI_USB_HID_SUBCLASS_BOOT) &&
                   (endpoint.protocol == XHCI_USB_HID_PROTOCOL_MOUSE)) {
            format = XHCI_HID_REPORT_BOOT_MOUSE;
        } else if ((vendor_id == XHCI_QEMU_HID_VENDOR_ID) &&
                   (product_id == XHCI_QEMU_HID_PRODUCT_ID) &&
                   (endpoint.interface_number == 0U) &&
                   (endpoint.alternate_setting == 0U) &&
                   (endpoint.interface_subclass == 0U) &&
                   (endpoint.protocol == 0U) &&
                   (endpoint.endpoint_address == 0x81U) &&
                   (endpoint.endpoint_id == 3U) &&
                   (endpoint.max_packet == 8U)) {
            format = XHCI_HID_REPORT_QEMU_ABSOLUTE_TABLET;
        }
        if (format == XHCI_HID_REPORT_UNSUPPORTED) { continue; }
        endpoint.report_format = format;
        selected.endpoints[selected.endpoint_count] = endpoint;
        ++selected.endpoint_count;
    }
    *supported = selected;
    return true;
}

static bool build_no_data_control_td(
    struct xhci_control_td *td, uint64_t ep0_ring_physical,
    uint16_t producer_index, bool producer_cycle, const uint8_t setup[8]) {
    struct xhci_control_td built = {0};
    uint64_t parameter = 0ULL;
    uint8_t index;
    uint32_t cycle;
    if ((td == NULL) || (setup == NULL) ||
        (ep0_ring_physical == 0ULL) ||
        ((ep0_ring_physical & 0x3fULL) != 0ULL) ||
        (producer_index > XHCI_EP0_RING_USABLE - 2U)) {
        return false;
    }
    for (index = 0U; index < 8U; ++index) {
        parameter |= (uint64_t)setup[index] << ((uint64_t)index * 8ULL);
    }
    cycle = producer_cycle ? XHCI_TRB_CYCLE : 0U;
    built.setup.parameter = parameter;
    built.setup.status = 8U;
    built.setup.control =
        ((uint32_t)XHCI_TRB_TYPE_SETUP_STAGE << XHCI_TRB_TYPE_SHIFT) |
        XHCI_TRB_IDT | cycle;
    built.status.control =
        ((uint32_t)XHCI_TRB_TYPE_STATUS_STAGE << XHCI_TRB_TYPE_SHIFT) |
        XHCI_TRB_DIRECTION_IN | XHCI_TRB_IOC | cycle;
    built.setup_physical = ep0_ring_physical +
        ((uint64_t)producer_index * XHCI_TRB_SIZE);
    built.status_physical = built.setup_physical + XHCI_TRB_SIZE;
    if (producer_index == XHCI_EP0_RING_USABLE - 2U) {
        built.next_producer_index = 0U;
        built.next_producer_cycle = !producer_cycle;
    } else {
        built.next_producer_index = (uint16_t)(producer_index + 2U);
        built.next_producer_cycle = producer_cycle;
    }
    *td = built;
    return true;
}

bool xhci_build_set_configuration_control_td(
    struct xhci_control_td *td, uint64_t ep0_ring_physical,
    uint16_t producer_index, bool producer_cycle, uint8_t configuration_value) {
    const uint8_t setup[8] = {
        0U, 9U, configuration_value, 0U, 0U, 0U, 0U, 0U
    };
    return (configuration_value != 0U) && build_no_data_control_td(
        td, ep0_ring_physical, producer_index, producer_cycle, setup);
}

bool xhci_build_hid_set_protocol_control_td(
    struct xhci_control_td *td, uint64_t ep0_ring_physical,
    uint16_t producer_index, bool producer_cycle, uint8_t interface_number) {
    const uint8_t setup[8] = {
        0x21U, 0x0bU, 0U, 0U, interface_number, 0U, 0U, 0U
    };
    return build_no_data_control_td(td, ep0_ring_physical, producer_index,
                                    producer_cycle, setup);
}

bool xhci_validate_no_data_control_event(
    const struct xhci_trb *event, uint64_t ep0_ring_physical,
    uint8_t expected_slot_id, uint64_t expected_status_trb_physical) {
    uint8_t type;
    uint8_t endpoint;
    uint8_t slot;
    uint8_t completion;
    uint32_t residual;
    if ((event == NULL) || (ep0_ring_physical == 0ULL) ||
        ((ep0_ring_physical & 0x3fULL) != 0ULL) ||
        (expected_slot_id == 0U) ||
        !trb_pointer_owned(expected_status_trb_physical, ep0_ring_physical)) {
        return false;
    }
    type = (uint8_t)((event->control >> XHCI_TRB_TYPE_SHIFT) & XHCI_TRB_TYPE_MASK);
    endpoint = (uint8_t)((event->control >> XHCI_EVENT_ENDPOINT_SHIFT) &
                         XHCI_EVENT_ENDPOINT_MASK);
    slot = (uint8_t)(event->control >> XHCI_EVENT_SLOT_SHIFT);
    completion = (uint8_t)(event->status >> XHCI_EVENT_COMPLETION_SHIFT);
    residual = event->status & XHCI_EVENT_RESIDUAL_MASK;
    return (type == XHCI_TRB_TYPE_TRANSFER_EVENT) && (endpoint == 1U) &&
           (slot == expected_slot_id) &&
           (event->parameter == expected_status_trb_physical) &&
           (completion == XHCI_COMPLETION_SUCCESS) && (residual == 0U);
}

bool xhci_build_configure_hid_context(
    void *buffer, uint32_t length, bool context_64_bytes,
    uint8_t max_slots, uint8_t slot_id, uint8_t max_ports,
    uint8_t root_port_id, uint8_t speed,
    const struct xhci_hid_configuration *configuration,
    const uint64_t ring_physical[XHCI_MAX_HID_ENDPOINTS]) {
    uint8_t *bytes = (uint8_t *)buffer;
    const uint32_t context_size = context_64_bytes ?
                                  XHCI_CONTEXT_64_SIZE : XHCI_CONTEXT_32_SIZE;
    uint8_t highest_dci = 0U;
    uint32_t add_flags = 1U;
    uint32_t required;
    uint8_t index;
    uint8_t *slot;
    if ((bytes == NULL) || (configuration == NULL) || (ring_physical == NULL) ||
        (configuration->endpoint_count == 0U) ||
        (configuration->endpoint_count > XHCI_MAX_HID_ENDPOINTS) ||
        (max_slots == 0U) || (slot_id == 0U) || (slot_id > max_slots) ||
        (max_ports == 0U) || (root_port_id == 0U) ||
        (root_port_id > max_ports) || (speed == 0U) || (speed > 3U)) {
        return false;
    }
    for (index = 0U; index < configuration->endpoint_count; ++index) {
        const struct xhci_hid_endpoint_descriptor *endpoint =
            &configuration->endpoints[index];
        uint8_t mapped;
        if (!xhci_usb_endpoint_id(endpoint->endpoint_address, &mapped) ||
            (mapped != endpoint->endpoint_id) || (mapped <= 1U) ||
            (endpoint->max_packet == 0U) ||
            (ring_physical[index] == 0ULL) ||
            ((ring_physical[index] & 0x3fULL) != 0ULL)) {
            return false;
        }
        if (mapped > highest_dci) { highest_dci = mapped; }
        add_flags |= 1U << mapped;
    }
    required = ((uint32_t)highest_dci + 2U) * context_size;
    if (length < required) { return false; }
    zero_bytes(bytes, required);
    context_write32(bytes, 4U, add_flags);
    slot = bytes + context_size;
    context_write32(slot, 0U, ((uint32_t)speed << 20U) |
                                  ((uint32_t)highest_dci << 27U));
    context_write32(slot, 4U, (uint32_t)root_port_id << 16U);
    for (index = 0U; index < configuration->endpoint_count; ++index) {
        const struct xhci_hid_endpoint_descriptor *endpoint =
            &configuration->endpoints[index];
        uint8_t *ep = bytes +
            (((uint32_t)endpoint->endpoint_id + 1U) * context_size);
        context_write32(ep, 0U, (uint32_t)endpoint->xhci_interval << 16U);
        context_write32(ep, 4U, (3U << 1U) | (7U << 3U) |
                                  ((uint32_t)endpoint->max_packet << 16U));
        context_write32(ep, 8U, (uint32_t)(ring_physical[index] | 1ULL));
        context_write32(ep, 12U,
                        (uint32_t)((ring_physical[index] | 1ULL) >> 32U));
        context_write32(ep, 16U, (uint32_t)endpoint->max_packet |
                         ((uint32_t)endpoint->max_packet << 16U));
    }
    return true;
}

bool xhci_build_configure_endpoint_command(
    struct xhci_trb *command, uint64_t input_context_physical,
    uint8_t max_slots, uint8_t slot_id) {
    struct xhci_trb built = {0};
    if ((command == NULL) || (input_context_physical == 0ULL) ||
        ((input_context_physical & 0x3fULL) != 0ULL) ||
        (max_slots == 0U) || (slot_id == 0U) || (slot_id > max_slots)) {
        return false;
    }
    built.parameter = input_context_physical;
    built.control = ((uint32_t)XHCI_TRB_TYPE_CONFIGURE_ENDPOINT <<
                     XHCI_TRB_TYPE_SHIFT) |
                    ((uint32_t)slot_id << XHCI_EVENT_SLOT_SHIFT);
    *command = built;
    return true;
}
