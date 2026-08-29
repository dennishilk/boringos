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
#define XHCI_TRB_TYPE_ENABLE_SLOT 9U
#define XHCI_TRB_TYPE_DISABLE_SLOT 10U
#define XHCI_TRB_TYPE_ADDRESS_DEVICE 11U
#define XHCI_TRB_TYPE_PORT_STATUS_EVENT 34U
#define XHCI_TRB_TYPE_MASK 0x3fU
#define XHCI_COMMAND_SLOT_SHIFT 24U
#define XHCI_EVENT_WAIT_LIMIT 10000000U
#define XHCI_PORTSC_CCS (1U << 0)
#define XHCI_PORTSC_PED (1U << 1)
#define XHCI_PORTSC_PR (1U << 4)
#define XHCI_PORTSC_SPEED_SHIFT 10U
#define XHCI_PORTSC_SPEED_MASK 0x0fU
#define XHCI_PORTSC_CHANGE_MASK (0x7fU << 17U)
#define XHCI_PORTSC_PRESERVE_MASK ((1U << 9U) | (3U << 14U) | \
                                   (7U << 25U))

struct xhci_erst_entry {
    uint64_t ring_base;
    uint32_t ring_size;
    uint32_t reserved;
};

static struct xhci_state active_state;

struct xhci_runtime {
    volatile uint8_t *mmio;
    volatile uint64_t *dcbaa;
    volatile struct xhci_trb *command_ring;
    volatile struct xhci_trb *event_ring;
    uint16_t command_index;
    uint16_t event_index;
    bool command_cycle;
    bool event_cycle;
};

static struct xhci_runtime runtime_state;

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
    struct xhci_trb *command;
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
    command = (struct xhci_trb *)command_virtual;
    command[XHCI_COMMAND_RING_USABLE].parameter =
        state->command_ring_physical;
    command[XHCI_COMMAND_RING_USABLE].status = 0U;
    command[XHCI_COMMAND_RING_USABLE].control =
        ((uint32_t)XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
        XHCI_TRB_TOGGLE_CYCLE | XHCI_TRB_CYCLE;
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
    runtime_state.mmio = base;
    runtime_state.dcbaa = (volatile uint64_t *)dcbaa_virtual;
    runtime_state.command_ring =
        (volatile struct xhci_trb *)command_virtual;
    runtime_state.event_ring = (volatile struct xhci_trb *)event_virtual;
    runtime_state.command_index = 0U;
    runtime_state.event_index = 0U;
    runtime_state.command_cycle = true;
    runtime_state.event_cycle = true;
    return (mmio_read64(base, operational + 0x30U) ==
            state->dcbaa_physical);
}

static void state_clear(struct xhci_state *state) {
    uint8_t *bytes = (uint8_t *)state;
    size_t index;
    for (index = 0U; index < sizeof(*state); ++index) { bytes[index] = 0U; }
}

static void runtime_clear(void) {
    uint8_t *bytes = (uint8_t *)&runtime_state;
    size_t index;
    for (index = 0U; index < sizeof(runtime_state); ++index) {
        bytes[index] = 0U;
    }
}

static uint32_t port_offset(uint8_t root_port_id) {
    return (uint32_t)active_state.capabilities.capability_length +
           XHCI_PORT_BASE + (((uint32_t)root_port_id - 1U) * XHCI_PORT_STRIDE);
}

static bool command_submit(uint64_t parameter, uint32_t status,
                           uint32_t control, uint64_t *command_physical) {
    volatile struct xhci_trb *trb;
    uint16_t index;
    if ((runtime_state.mmio == NULL) ||
        (runtime_state.command_ring == NULL) || (command_physical == NULL) ||
        (runtime_state.command_index >= XHCI_COMMAND_RING_USABLE)) {
        return false;
    }
    index = runtime_state.command_index;
    trb = &runtime_state.command_ring[index];
    trb->parameter = parameter;
    trb->status = status;
    trb->control = control |
                   (runtime_state.command_cycle ? XHCI_TRB_CYCLE : 0U);
    *command_physical = active_state.command_ring_physical +
                        ((uint64_t)index * XHCI_TRB_SIZE);
    ++runtime_state.command_index;
    if (runtime_state.command_index == XHCI_COMMAND_RING_USABLE) {
        volatile struct xhci_trb *link =
            &runtime_state.command_ring[XHCI_COMMAND_RING_USABLE];
        link->control = ((uint32_t)XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
                        XHCI_TRB_TOGGLE_CYCLE |
                        (runtime_state.command_cycle ? XHCI_TRB_CYCLE : 0U);
        runtime_state.command_index = 0U;
        runtime_state.command_cycle = !runtime_state.command_cycle;
    }
    memory_barrier();
    mmio_write32(runtime_state.mmio,
                 active_state.capabilities.doorbell_offset, 0U);
    return true;
}

static bool event_take(struct xhci_trb *event, bool *available) {
    const volatile struct xhci_trb *source;
    uint32_t control;
    const uint32_t interrupter = active_state.capabilities.runtime_offset +
                                 XHCI_RUNTIME_INTERRUPTER0;
    if ((event == NULL) || (available == NULL) ||
        (runtime_state.mmio == NULL) || (runtime_state.event_ring == NULL) ||
        (runtime_state.event_index >= XHCI_EVENT_RING_TRBS)) {
        return false;
    }
    source = &runtime_state.event_ring[runtime_state.event_index];
    control = source->control;
    if (((control & XHCI_TRB_CYCLE) != 0U) != runtime_state.event_cycle) {
        *available = false;
        return true;
    }
    event->parameter = source->parameter;
    event->status = source->status;
    event->control = control;
    ++runtime_state.event_index;
    if (runtime_state.event_index == XHCI_EVENT_RING_TRBS) {
        runtime_state.event_index = 0U;
        runtime_state.event_cycle = !runtime_state.event_cycle;
    }
    memory_barrier();
    mmio_write64(runtime_state.mmio, interrupter + 0x18U,
                 (active_state.event_ring_physical +
                  ((uint64_t)runtime_state.event_index * XHCI_TRB_SIZE)) |
                 (1ULL << 3U));
    *available = true;
    return true;
}

static bool command_wait(uint64_t command_physical, uint8_t *slot_id) {
    uint32_t attempt;
    for (attempt = 0U; attempt < XHCI_EVENT_WAIT_LIMIT; ++attempt) {
        struct xhci_trb event;
        bool available;
        uint8_t type;
        if (!event_take(&event, &available)) { return false; }
        if (!available) {
            x86_64_pause();
            continue;
        }
        type = (uint8_t)((event.control >> XHCI_TRB_TYPE_SHIFT) &
                         XHCI_TRB_TYPE_MASK);
        if (type == XHCI_TRB_TYPE_PORT_STATUS_EVENT) {
            const uint8_t port_id = (uint8_t)(event.parameter >> 24U);
            const uint8_t completion = (uint8_t)(event.status >> 24U);
            if ((port_id == 0U) ||
                (port_id > active_state.capabilities.max_ports) ||
                (completion != XHCI_COMPLETION_SUCCESS)) {
                return false;
            }
            if (active_state.port_events_consumed != UINT32_MAX) {
                ++active_state.port_events_consumed;
            }
            continue;
        }
        if (!xhci_validate_command_completion(
                &event, command_physical, active_state.capabilities.max_slots,
                slot_id)) {
            return false;
        }
        if (active_state.command_completions != UINT32_MAX) {
            ++active_state.command_completions;
        }
        return true;
    }
    return false;
}

static bool command_enable_slot(uint8_t *slot_id) {
    uint64_t command_physical;
    return command_submit(0ULL, 0U,
                          (uint32_t)XHCI_TRB_TYPE_ENABLE_SLOT <<
                          XHCI_TRB_TYPE_SHIFT,
                          &command_physical) &&
           command_wait(command_physical, slot_id);
}

static bool command_disable_slot(uint8_t slot_id) {
    uint64_t command_physical;
    uint8_t completed_slot;
    if ((slot_id == 0U) || (slot_id > active_state.capabilities.max_slots)) {
        return false;
    }
    return command_submit(0ULL, 0U,
                          ((uint32_t)XHCI_TRB_TYPE_DISABLE_SLOT <<
                           XHCI_TRB_TYPE_SHIFT) |
                          ((uint32_t)slot_id << XHCI_COMMAND_SLOT_SHIFT),
                          &command_physical) &&
           command_wait(command_physical, &completed_slot) &&
           (completed_slot == slot_id);
}

static bool command_address_device(uint8_t slot_id,
                                   uint64_t input_context_physical) {
    uint64_t command_physical;
    uint8_t completed_slot;
    if ((slot_id == 0U) || (slot_id > active_state.capabilities.max_slots) ||
        ((input_context_physical & 0x3fULL) != 0ULL)) {
        return false;
    }
    return command_submit(input_context_physical, 0U,
                          ((uint32_t)XHCI_TRB_TYPE_ADDRESS_DEVICE <<
                           XHCI_TRB_TYPE_SHIFT) |
                          ((uint32_t)slot_id << XHCI_COMMAND_SLOT_SHIFT),
                          &command_physical) &&
           command_wait(command_physical, &completed_slot) &&
           (completed_slot == slot_id);
}

static bool root_port_reset(uint8_t root_port_id, uint8_t *speed) {
    uint32_t current;
    uint32_t attempt;
    const uint32_t offset = port_offset(root_port_id);
    if ((speed == NULL) || (root_port_id == 0U) ||
        (root_port_id > active_state.capabilities.max_ports)) {
        return false;
    }
    current = mmio_read32(runtime_state.mmio, offset);
    if ((current & XHCI_PORTSC_CCS) == 0U) { return false; }
    if ((current & XHCI_PORTSC_PED) == 0U) {
        mmio_write32(runtime_state.mmio, offset,
                     (current & XHCI_PORTSC_PRESERVE_MASK) | XHCI_PORTSC_PR);
        for (attempt = 0U; attempt < XHCI_WAIT_LIMIT; ++attempt) {
            current = mmio_read32(runtime_state.mmio, offset);
            if (((current & XHCI_PORTSC_PR) == 0U) &&
                ((current & XHCI_PORTSC_PED) != 0U)) {
                break;
            }
            x86_64_pause();
        }
        if (attempt == XHCI_WAIT_LIMIT) { return false; }
    }
    current = mmio_read32(runtime_state.mmio, offset);
    if (((current & XHCI_PORTSC_CCS) == 0U) ||
        ((current & XHCI_PORTSC_PED) == 0U)) {
        return false;
    }
    *speed = (uint8_t)((current >> XHCI_PORTSC_SPEED_SHIFT) &
                       XHCI_PORTSC_SPEED_MASK);
    if ((*speed == 0U) || (*speed > 5U)) { return false; }
    mmio_write32(runtime_state.mmio, offset,
                 (current & XHCI_PORTSC_PRESERVE_MASK) |
                 (current & XHCI_PORTSC_CHANGE_MASK));
    return true;
}

static void device_frames_release(struct xhci_addressed_device *device) {
    uint8_t *bytes = (uint8_t *)device;
    size_t index;
    frame_release(device->ep0_ring_physical);
    frame_release(device->device_context_physical);
    frame_release(device->input_context_physical);
    for (index = 0U; index < sizeof(*device); ++index) { bytes[index] = 0U; }
}

static bool address_root_port(uint8_t root_port_id,
                              struct xhci_addressed_device *device) {
    void *input_virtual = NULL;
    void *device_virtual = NULL;
    void *ep0_virtual = NULL;
    struct xhci_trb *ep0_ring;
    uint8_t slot_id = 0U;
    uint8_t speed;
    bool slot_enabled = false;

    if ((device == NULL) || !root_port_reset(root_port_id, &speed) ||
        !command_enable_slot(&slot_id)) {
        return false;
    }
    slot_enabled = true;
    if ((slot_id == 0U) || (slot_id > active_state.capabilities.max_slots) ||
        !frame_alloc_zero(&device->input_context_physical, &input_virtual) ||
        !frame_alloc_zero(&device->device_context_physical, &device_virtual) ||
        !frame_alloc_zero(&device->ep0_ring_physical, &ep0_virtual)) {
        goto fail;
    }
    ep0_ring = (struct xhci_trb *)ep0_virtual;
    ep0_ring[XHCI_COMMAND_RING_USABLE].parameter = device->ep0_ring_physical;
    ep0_ring[XHCI_COMMAND_RING_USABLE].status = 0U;
    ep0_ring[XHCI_COMMAND_RING_USABLE].control =
        ((uint32_t)XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
        XHCI_TRB_TOGGLE_CYCLE | XHCI_TRB_CYCLE;
    if (!xhci_build_address_input_context(
            input_virtual, (uint32_t)PMM_PAGE_SIZE,
            active_state.capabilities.context_64_bytes,
            active_state.capabilities.max_slots, slot_id,
            active_state.capabilities.max_ports, root_port_id, speed,
            device->ep0_ring_physical)) {
        goto fail;
    }
    runtime_state.dcbaa[slot_id] = device->device_context_physical;
    memory_barrier();
    if (!command_address_device(slot_id, device->input_context_physical)) {
        runtime_state.dcbaa[slot_id] = 0ULL;
        memory_barrier();
        goto fail;
    }
    (void)device_virtual;
    device->root_port_id = root_port_id;
    device->slot_id = slot_id;
    device->speed = speed;
    device->addressed = true;
    return true;

fail:
    if (slot_enabled) { (void)command_disable_slot(slot_id); }
    device_frames_release(device);
    return false;
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
    runtime_clear();
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
    if ((!success) && (mapping != NULL)) {
        (void)vmm_unmap_mmio_region(mapping, XHCI_MMIO_WINDOW_SIZE);
    }
    if (!success) {
        frame_release(active_state.erst_physical);
        frame_release(active_state.event_ring_physical);
        frame_release(active_state.command_ring_physical);
        frame_release(active_state.dcbaa_physical);
        state_clear(&active_state);
        runtime_clear();
    }
    *state = active_state;
    return success;
}

bool xhci_address_connected(struct xhci_state *state) {
    uint8_t port_index;
    if ((state == NULL) || !active_state.controller_running ||
        (runtime_state.mmio == NULL) || (runtime_state.dcbaa == NULL) ||
        (active_state.addressed_count != 0U)) {
        return false;
    }
    for (port_index = 0U;
         port_index < active_state.capabilities.max_ports; ++port_index) {
        if ((active_state.connected_ports & (1ULL << port_index)) == 0ULL) {
            continue;
        }
        if (active_state.addressed_count == XHCI_MAX_ADDRESSED_DEVICES) {
            active_state.addressing_truncated = true;
            break;
        }
        if (!address_root_port(
                (uint8_t)(port_index + 1U),
                &active_state.addressed[active_state.addressed_count])) {
            *state = active_state;
            return false;
        }
        ++active_state.addressed_count;
    }
    *state = active_state;
    return active_state.addressed_count != 0U;
}

const struct xhci_state *xhci_get_state(void) {
    return active_state.controller_running ? &active_state : NULL;
}
