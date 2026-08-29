#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/cpu.h>
#include <boring/pci.h>
#include <boring/pci_inventory.h>
#include <boring/pmm.h>
#include <boring/vmm.h>
#include <boring/xhci.h>

#define XHCI_CLASS_SERIAL_BUS 0x0cU
#define XHCI_SUBCLASS_USB 0x03U
#define XHCI_PROG_IF 0x30U
#define XHCI_PORT_BASE 0x400U
#define XHCI_PORT_STRIDE 0x10U
#define XHCI_RUNTIME_INTERRUPTER0 0x20U
#define XHCI_WAIT_LIMIT 10000000U
#define XHCI_EXTENDED_CAP_LIMIT 64U
#define XHCI_LEGACY_CAP_ID 1U
#define XHCI_LEGACY_BIOS_OWNED (1U << 16)
#define XHCI_LEGACY_OS_OWNED (1U << 24)
#define XHCI_USBCMD_RUN (1U << 0)
#define XHCI_USBCMD_RESET (1U << 1)
#define XHCI_USBSTS_HALTED (1U << 0)
#define XHCI_USBSTS_NOT_READY (1U << 11)
#define XHCI_TRB_TYPE_LINK 6U
#define XHCI_TRB_TYPE_SHIFT 10U
#define XHCI_TRB_TOGGLE_CYCLE (1U << 1)
#define XHCI_TRB_CYCLE (1U << 0)

struct xhci_erst_entry {
    uint64_t ring_base;
    uint32_t ring_size;
    uint32_t reserved;
};

static struct xhci_state active_state;

static uint32_t mmio_read32(const volatile uint8_t *base, uint32_t offset) {
    return *(const volatile uint32_t *)(const volatile void *)(base + offset);
}

static uint64_t mmio_read64(const volatile uint8_t *base, uint32_t offset) {
    const uint64_t low = (uint64_t)mmio_read32(base, offset);
    const uint64_t high = (uint64_t)mmio_read32(base, offset + 4U);
    return low | (high << 32U);
}

static void mmio_write32(volatile uint8_t *base, uint32_t offset,
                         uint32_t value) {
    *(volatile uint32_t *)(volatile void *)(base + offset) = value;
}

static void mmio_write64(volatile uint8_t *base, uint32_t offset,
                         uint64_t value) {
    mmio_write32(base, offset, (uint32_t)value);
    mmio_write32(base, offset + 4U, (uint32_t)(value >> 32U));
}

static void memory_barrier(void) {
    __asm__ volatile ("mfence" ::: "memory");
}

static bool find_controller(struct pci_device *device) {
    const struct boring_pci_inventory *inventory = boring_pci_inventory_get();
    uint32_t index;

    if ((device == NULL) || (inventory == NULL) || !inventory->complete) {
        return false;
    }
    for (index = 0U; index < inventory->stored; ++index) {
        const struct boring_pci_entry *entry = &inventory->entries[index];
        if ((entry->class_code == XHCI_CLASS_SERIAL_BUS) &&
            (entry->subclass == XHCI_SUBCLASS_USB) &&
            (entry->prog_if == XHCI_PROG_IF)) {
            device->bdf = entry->bdf;
            device->vendor_id = entry->vendor_id;
            device->device_id = entry->device_id;
            device->class_code = entry->class_code;
            device->subclass = entry->subclass;
            device->header_type = entry->header_type;
            device->revision = entry->revision;
            return true;
        }
    }
    return false;
}

static bool legacy_handoff(volatile uint8_t *base,
                           const struct xhci_capabilities *capabilities,
                           bool *complete) {
    uint32_t offset = capabilities->extended_capability_offset;
    uint32_t count;

    *complete = false;
    for (count = 0U; (offset != 0U) &&
                     (count < XHCI_EXTENDED_CAP_LIMIT); ++count) {
        uint32_t header;
        uint32_t next;
        if (offset > XHCI_MMIO_WINDOW_SIZE - 4U) { return false; }
        header = mmio_read32(base, offset);
        if ((header & 0xffU) == XHCI_LEGACY_CAP_ID) {
            uint32_t wait;
            mmio_write32(base, offset, header | XHCI_LEGACY_OS_OWNED);
            for (wait = 0U; wait < XHCI_WAIT_LIMIT; ++wait) {
                header = mmio_read32(base, offset);
                if ((header & XHCI_LEGACY_BIOS_OWNED) == 0U) {
                    *complete = true;
                    return (header & XHCI_LEGACY_OS_OWNED) != 0U;
                }
                x86_64_pause();
            }
            return false;
        }
        next = (header >> 8U) & 0xffU;
        if (next == 0U) { break; }
        if ((next > (XHCI_MMIO_WINDOW_SIZE / 4U)) ||
            (offset > XHCI_MMIO_WINDOW_SIZE - (next * 4U))) {
            return false;
        }
        offset += next * 4U;
    }
    *complete = true;
    return (offset == 0U) || (count < XHCI_EXTENDED_CAP_LIMIT);
}

static bool wait_mask(volatile uint8_t *base, uint32_t offset,
                      uint32_t mask, bool set) {
    uint32_t attempt;
    for (attempt = 0U; attempt < XHCI_WAIT_LIMIT; ++attempt) {
        if (((mmio_read32(base, offset) & mask) != 0U) == set) { return true; }
        x86_64_pause();
    }
    return false;
}

static bool frame_alloc_zero(uint64_t *physical, void **virtual_address) {
    size_t index;
    uint8_t *bytes;
    if (!pmm_alloc_frame(physical)) {
        return false;
    }
    if (!vmm_pmm_frame_to_hhdm(*physical, virtual_address)) {
        (void)pmm_free_frame(*physical);
        *physical = 0ULL;
        return false;
    }
    bytes = (uint8_t *)*virtual_address;
    for (index = 0U; index < PMM_PAGE_SIZE; ++index) { bytes[index] = 0U; }
    return true;
}

static void frame_release(uint64_t physical) {
    if (physical != 0ULL) { (void)pmm_free_frame(physical); }
}

static bool rings_initialize(volatile uint8_t *base,
                             const struct xhci_capabilities *capabilities,
                             struct xhci_state *state) {
    void *dcbaa_virtual = NULL;
    void *command_virtual = NULL;
    void *event_virtual = NULL;
    void *erst_virtual = NULL;
    uint64_t *command;
    struct xhci_erst_entry *erst;
    const uint32_t operational = capabilities->capability_length;
    const uint32_t interrupter = capabilities->runtime_offset +
                                 XHCI_RUNTIME_INTERRUPTER0;

    if (capabilities->scratchpad_count != 0U) { return false; }
    if (!frame_alloc_zero(&state->dcbaa_physical, &dcbaa_virtual) ||
        !frame_alloc_zero(&state->command_ring_physical, &command_virtual) ||
        !frame_alloc_zero(&state->event_ring_physical, &event_virtual) ||
        !frame_alloc_zero(&state->erst_physical, &erst_virtual)) {
        return false;
    }
    (void)dcbaa_virtual;
    (void)event_virtual;
    command = (uint64_t *)command_virtual;
    command[252U * 2U] = state->command_ring_physical;
    command[252U * 2U + 1U] =
        (((uint64_t)XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
         XHCI_TRB_TOGGLE_CYCLE | XHCI_TRB_CYCLE) << 32U;
    erst = (struct xhci_erst_entry *)erst_virtual;
    erst->ring_base = state->event_ring_physical;
    erst->ring_size = 256U;
    erst->reserved = 0U;
    memory_barrier();

    mmio_write64(base, operational + 0x30U, state->dcbaa_physical);
    mmio_write64(base, operational + 0x18U,
                 state->command_ring_physical | XHCI_TRB_CYCLE);
    mmio_write32(base, operational + 0x38U,
                 (uint32_t)capabilities->max_slots);
    mmio_write32(base, interrupter + 0x00U, 0U);
    mmio_write32(base, interrupter + 0x08U, 1U);
    mmio_write64(base, interrupter + 0x10U, state->erst_physical);
    mmio_write64(base, interrupter + 0x18U, state->event_ring_physical);
    memory_barrier();
    return (mmio_read64(base, operational + 0x30U) ==
            state->dcbaa_physical);
}

static void state_clear(struct xhci_state *state) {
    uint8_t *bytes = (uint8_t *)state;
    size_t index;
    for (index = 0U; index < sizeof(*state); ++index) { bytes[index] = 0U; }
}

bool xhci_init(struct xhci_state *state) {
    struct pci_bar bar;
    volatile void *mapping = NULL;
    volatile uint8_t *base;
    uint32_t operational;
    uint8_t port;
    bool success = false;

    if ((state == NULL) || active_state.controller_running) { return false; }
    state_clear(&active_state);
    if (!find_controller(&active_state.device) ||
        !pci_get_bar(&active_state.device, 0U, &bar) ||
        !bar.memory || !pci_enable_memory_bus_master(&active_state.device) ||
        !vmm_map_mmio_region(bar.base, XHCI_MMIO_WINDOW_SIZE, &mapping)) {
        goto out;
    }
    base = (volatile uint8_t *)mapping;
    active_state.mmio_physical = bar.base;
    if (!xhci_parse_capabilities(base, XHCI_MMIO_WINDOW_SIZE,
                                 &active_state.capabilities) ||
        !legacy_handoff(base, &active_state.capabilities,
                        &active_state.legacy_handoff_complete)) {
        goto out;
    }
    operational = active_state.capabilities.capability_length;
    mmio_write32(base, operational + 0x00U,
                 mmio_read32(base, operational + 0x00U) & ~XHCI_USBCMD_RUN);
    if (!wait_mask(base, operational + 0x04U, XHCI_USBSTS_HALTED, true)) {
        goto out;
    }
    mmio_write32(base, operational + 0x00U,
                 mmio_read32(base, operational + 0x00U) | XHCI_USBCMD_RESET);
    if (!wait_mask(base, operational + 0x00U, XHCI_USBCMD_RESET, false) ||
        !wait_mask(base, operational + 0x04U, XHCI_USBSTS_NOT_READY, false) ||
        !rings_initialize(base, &active_state.capabilities, &active_state)) {
        goto out;
    }
    mmio_write32(base, operational + 0x00U,
                 mmio_read32(base, operational + 0x00U) | XHCI_USBCMD_RUN);
    if (!wait_mask(base, operational + 0x04U, XHCI_USBSTS_HALTED, false)) {
        goto out;
    }
    active_state.controller_running = true;
    for (port = 0U; port < active_state.capabilities.max_ports; ++port) {
        const uint32_t portsc = mmio_read32(
            base, operational + XHCI_PORT_BASE +
                  ((uint32_t)port * XHCI_PORT_STRIDE));
        if ((portsc & 1U) != 0U) { active_state.connected_ports |= 1ULL << port; }
    }
    success = true;

out:
    if (mapping != NULL) {
        (void)vmm_unmap_mmio_region(mapping, XHCI_MMIO_WINDOW_SIZE);
    }
    if (!success) {
        frame_release(active_state.erst_physical);
        frame_release(active_state.event_ring_physical);
        frame_release(active_state.command_ring_physical);
        frame_release(active_state.dcbaa_physical);
        state_clear(&active_state);
    }
    *state = active_state;
    return success;
}

const struct xhci_state *xhci_get_state(void) {
    return active_state.controller_running ? &active_state : NULL;
}
