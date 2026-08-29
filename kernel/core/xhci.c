#include <stddef.h>
#include <stdint.h>

#include <boring/xhci.h>

#define XHCI_CAP_MIN_LENGTH 0x20U
#define XHCI_OPERATION_MIN_SIZE 0x40U
#define XHCI_PORT_BASE 0x400U
#define XHCI_PORT_STRIDE 0x10U

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
