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
#define XHCI_EVENT_SLOT_SHIFT 24U
#define XHCI_EVENT_COMPLETION_SHIFT 24U

static uint8_t read8(const volatile uint8_t *base, uint32_t offset) {
    return base[offset];
}

static uint32_t read32(const volatile uint8_t *base, uint32_t offset) {
    return *(const volatile uint32_t *)(const volatile void *)(base + offset);
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
        case 1U: value = 8U; break;   /* full speed before descriptors */
        case 2U: value = 8U; break;   /* low speed */
        case 3U: value = 64U; break;  /* high speed */
        case 4U: value = 512U; break; /* SuperSpeed */
        case 5U: value = 512U; break; /* SuperSpeedPlus */
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
    context_write32(bytes, 4U, 3U); /* add Slot Context and EP0 Context */
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
