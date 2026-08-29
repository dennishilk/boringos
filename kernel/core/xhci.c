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
#define XHCI_TRB_CHAIN (1U << 4)
#define XHCI_TRB_IOC (1U << 5)
#define XHCI_TRB_IDT (1U << 6)
#define XHCI_TRB_DIRECTION_IN (1U << 16)
#define XHCI_SETUP_TRT_IN_DATA (3U << 16)

static uint8_t read8(const volatile uint8_t *base, uint32_t offset) {
    return base[offset];
}

static uint32_t read32(const volatile uint8_t *base, uint32_t offset) {
    return *(const volatile uint32_t *)(const volatile void *)(base + offset);
}

static uint16_t little16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
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
    parsed.scratchpad_count = (uint16_t)((((hcs2 >> 27U) & 0x1fU) << 5U) |
                                         ((hcs2 >> 21U) & 0x1fU));
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
        XHCI_TRB_IDT | XHCI_TRB_CHAIN | XHCI_SETUP_TRT_IN_DATA | cycle;
    built.data.parameter = buffer_physical;
    built.data.status = (uint32_t)length;
    built.data.control =
        ((uint32_t)XHCI_TRB_TYPE_DATA_STAGE << XHCI_TRB_TYPE_SHIFT) |
        XHCI_TRB_DIRECTION_IN | XHCI_TRB_ISP | XHCI_TRB_CHAIN | cycle;
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
