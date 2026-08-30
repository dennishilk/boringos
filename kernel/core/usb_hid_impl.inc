#include <boring/usb_hid.h>
#include <boring/xhci.h>

#if !__STDC_HOSTED__
#include <boring/cpu.h>
#include <boring/event_syscall.h>
#include <boring/input.h>
#include <boring/pmm.h>
#include <boring/serial.h>
#include <boring/vmm.h>
#endif

#define M52_TRB_TYPE_SHIFT 10U
#define M52_TRB_TYPE_MASK 0x3fU
#define M52_TRB_TYPE_LINK 6U
#define M52_TRB_CYCLE (1U << 0)
#define M52_TRB_TOGGLE_CYCLE (1U << 1)
#define M52_TRB_ISP (1U << 2)
#define M52_TRB_IOC (1U << 5)
#define M52_EVENT_ENDPOINT_SHIFT 16U
#define M52_EVENT_ENDPOINT_MASK 0x1fU
#define M52_EVENT_SLOT_SHIFT 24U
#define M52_EVENT_COMPLETION_SHIFT 24U
#define M52_EVENT_RESIDUAL_MASK 0x00ffffffU

static bool usage_present(const uint8_t keys[USB_HID_BOOT_KEYS], uint8_t usage) {
    size_t index;
    for (index = 0U; index < USB_HID_BOOT_KEYS; ++index) {
        if (keys[index] == usage) { return true; }
    }
    return false;
}

static bool report_valid(const uint8_t *report) {
    size_t left;
    for (left = 2U; left < USB_HID_BOOT_KEY_REPORT_SIZE; ++left) {
        size_t right;
        const uint8_t usage = report[left];
        if ((usage >= 1U) && (usage <= 3U)) { return false; }
        if (usage == 0U) { continue; }
        for (right = left + 1U; right < USB_HID_BOOT_KEY_REPORT_SIZE; ++right) {
            if (report[right] == usage) { return false; }
        }
    }
    return true;
}

bool usb_hid_keyboard_decode(struct usb_hid_keyboard_state *state,
                             const uint8_t *report, size_t length,
                             struct usb_hid_key_transition *transitions,
                             size_t capacity, size_t *count_out,
                             uint8_t *modifiers_out) {
    uint8_t next[USB_HID_BOOT_KEYS];
    size_t count = 0U;
    size_t index;

    if ((state == NULL) || (report == NULL) || (transitions == NULL) ||
        (count_out == NULL) || (modifiers_out == NULL) ||
        (length != USB_HID_BOOT_KEY_REPORT_SIZE) || !report_valid(report)) {
        return false;
    }
    for (index = 0U; index < USB_HID_BOOT_KEYS; ++index) {
        next[index] = report[index + 2U];
    }
    for (index = 0U; index < USB_HID_BOOT_KEYS; ++index) {
        const uint8_t usage = state->keys[index];
        if ((usage != 0U) && !usage_present(next, usage)) {
            if (count >= capacity) { return false; }
            transitions[count].usage = usage;
            transitions[count].down = false;
            ++count;
        }
    }
    for (index = 0U; index < USB_HID_BOOT_KEYS; ++index) {
        const uint8_t usage = next[index];
        if ((usage != 0U) && !usage_present(state->keys, usage)) {
            if (count >= capacity) { return false; }
            transitions[count].usage = usage;
            transitions[count].down = true;
            ++count;
        }
    }
    state->modifiers = report[0];
    for (index = 0U; index < USB_HID_BOOT_KEYS; ++index) {
        state->keys[index] = next[index];
    }
    *count_out = count;
    *modifiers_out = report[0];
    return true;
}

bool usb_hid_mouse_decode(const uint8_t *report, size_t length,
                          struct usb_hid_mouse_report *decoded) {
    if ((report == NULL) || (decoded == NULL) ||
        (length < USB_HID_BOOT_MOUSE_REPORT_MIN) || (length > 4U)) {
        return false;
    }
    decoded->buttons = (uint8_t)(report[0] & 0x1fU);
    decoded->dx = (int16_t)(int8_t)report[1];
    decoded->dy = (int16_t)(int8_t)report[2];
    decoded->wheel = (length == 4U) ? (int8_t)report[3] : 0;
    return true;
}

bool usb_hid_absolute_tablet_decode(
    const uint8_t *report, size_t length,
    struct usb_hid_absolute_tablet_report *decoded) {
    if ((report == NULL) || (decoded == NULL) ||
        (length != USB_HID_ABSOLUTE_TABLET_REPORT_SIZE)) {
        return false;
    }
    decoded->buttons = (uint8_t)(report[0] & 0x1fU);
    decoded->x = (uint16_t)((uint16_t)report[1] |
                            ((uint16_t)report[2] << 8U));
    decoded->y = (uint16_t)((uint16_t)report[3] |
                            ((uint16_t)report[4] << 8U));
    decoded->wheel = (int8_t)report[5];
    return true;
}

static bool interrupt_pointer_owned(uint64_t pointer, uint64_t ring_physical) {
    const uint64_t end = ring_physical +
        ((uint64_t)XHCI_INTERRUPT_RING_USABLE * XHCI_TRB_SIZE);
    return ((pointer & 0x0fULL) == 0ULL) && (pointer >= ring_physical) &&
           (pointer < end);
}

bool xhci_build_interrupt_in_trb(
    struct xhci_trb *trb, uint64_t ring_physical,
    uint16_t producer_index, bool producer_cycle,
    uint64_t buffer_physical, uint16_t length,
    uint64_t *trb_physical, uint16_t *next_producer_index,
    bool *next_producer_cycle) {
    struct xhci_trb built = {0};
    uint16_t next_index;
    bool next_cycle;
    if ((trb == NULL) || (trb_physical == NULL) ||
        (next_producer_index == NULL) || (next_producer_cycle == NULL) ||
        (ring_physical == 0ULL) || ((ring_physical & 0x3fULL) != 0ULL) ||
        (producer_index >= XHCI_INTERRUPT_RING_USABLE) ||
        (buffer_physical == 0ULL) || ((buffer_physical & 0xfffULL) != 0ULL) ||
        (length == 0U) || (length > XHCI_HID_REPORT_BUFFER_BYTES)) {
        return false;
    }
    built.parameter = buffer_physical;
    built.status = (uint32_t)length;
    built.control = ((uint32_t)XHCI_TRB_TYPE_NORMAL << M52_TRB_TYPE_SHIFT) |
                    M52_TRB_ISP | M52_TRB_IOC |
                    (producer_cycle ? M52_TRB_CYCLE : 0U);
    *trb_physical = ring_physical +
        ((uint64_t)producer_index * XHCI_TRB_SIZE);
    if (producer_index == XHCI_INTERRUPT_RING_USABLE - 1U) {
        next_index = 0U;
        next_cycle = !producer_cycle;
    } else {
        next_index = (uint16_t)(producer_index + 1U);
        next_cycle = producer_cycle;
    }
    *trb = built;
    *next_producer_index = next_index;
    *next_producer_cycle = next_cycle;
    return true;
}

bool xhci_validate_interrupt_transfer_event(
    const struct xhci_trb *event, uint64_t ring_physical,
    uint8_t expected_slot_id, uint8_t expected_endpoint_id,
    uint64_t expected_trb_physical, uint16_t requested_length,
    uint16_t *actual_length, bool *short_packet) {
    uint8_t type;
    uint8_t endpoint;
    uint8_t slot;
    uint8_t completion;
    uint32_t residual;
    if ((event == NULL) || (actual_length == NULL) || (short_packet == NULL) ||
        (ring_physical == 0ULL) || ((ring_physical & 0x3fULL) != 0ULL) ||
        (expected_slot_id == 0U) || (expected_endpoint_id <= 1U) ||
        (expected_endpoint_id > 31U) || (requested_length == 0U) ||
        (requested_length > XHCI_HID_REPORT_BUFFER_BYTES) ||
        !interrupt_pointer_owned(expected_trb_physical, ring_physical)) {
        return false;
    }
    type = (uint8_t)((event->control >> M52_TRB_TYPE_SHIFT) & M52_TRB_TYPE_MASK);
    endpoint = (uint8_t)((event->control >> M52_EVENT_ENDPOINT_SHIFT) &
                         M52_EVENT_ENDPOINT_MASK);
    slot = (uint8_t)(event->control >> M52_EVENT_SLOT_SHIFT);
    completion = (uint8_t)(event->status >> M52_EVENT_COMPLETION_SHIFT);
    residual = event->status & M52_EVENT_RESIDUAL_MASK;
    if ((type != XHCI_TRB_TYPE_TRANSFER_EVENT) ||
        (endpoint != expected_endpoint_id) || (slot != expected_slot_id) ||
        (event->parameter != expected_trb_physical) ||
        !interrupt_pointer_owned(event->parameter, ring_physical) ||
        (residual > requested_length)) {
        return false;
    }
    if ((completion == XHCI_COMPLETION_SUCCESS) && (residual == 0U)) {
        *actual_length = requested_length;
        *short_packet = false;
        return true;
    }
    if (completion == XHCI_COMPLETION_SHORT_PACKET) {
        *actual_length = (uint16_t)((uint32_t)requested_length - residual);
        *short_packet = true;
        return true;
    }
    return false;
}

#if !__STDC_HOSTED__

#define M52_MMIO_WINDOW_SIZE XHCI_MMIO_WINDOW_SIZE
#define M52_RUNTIME_INTERRUPTER0 0x20U
#define M52_EVENT_WAIT_LIMIT 30000000U
#define M52_TRB_TYPE_PORT_STATUS_EVENT 34U

static void m52_zero_page(void *page) {
    uint8_t *bytes = (uint8_t *)page;
    size_t index;
    for (index = 0U; index < PMM_PAGE_SIZE; ++index) { bytes[index] = 0U; }
}

static void m52_barrier(void) {
    __asm__ volatile ("mfence" ::: "memory");
}

static void m52_mmio_write32(volatile uint8_t *base, uint32_t offset,
                             uint32_t value) {
    *(volatile uint32_t *)(volatile void *)(base + offset) = value;
}

static void m52_mmio_write64(volatile uint8_t *base, uint32_t offset,
                             uint64_t value) {
    m52_mmio_write32(base, offset, (uint32_t)value);
    m52_mmio_write32(base, offset + 4U, (uint32_t)(value >> 32U));
}

static uint64_t m52_consumed_events(const struct xhci_state *state) {
    uint64_t total;
    uint8_t device_index;
    total = (uint64_t)state->command_completions +
            (uint64_t)state->port_events_consumed;
    for (device_index = 0U; device_index < state->addressed_count;
         ++device_index) {
        total += (uint64_t)state->addressed[device_index].transfer_events;
    }
    return total;
}

static bool m52_allocate_report_buffer(struct xhci_hid_endpoint_runtime *runtime) {
    void *virtual_address = NULL;
    if (runtime->report_buffer_physical != 0ULL) { return true; }
    if (!pmm_alloc_frame(&runtime->report_buffer_physical) ||
        !vmm_pmm_frame_to_hhdm(runtime->report_buffer_physical,
                               &virtual_address)) {
        if (runtime->report_buffer_physical != 0ULL) {
            (void)pmm_free_frame(runtime->report_buffer_physical);
            runtime->report_buffer_physical = 0ULL;
        }
        return false;
    }
    m52_zero_page(virtual_address);
    runtime->producer_index = 0U;
    runtime->producer_cycle = true;
    return true;
}

static bool m52_submit_endpoint(volatile uint8_t *mmio,
                                struct xhci_addressed_device *device,
                                uint8_t endpoint_index) {
    struct xhci_hid_endpoint_runtime *runtime;
    const struct xhci_hid_endpoint_descriptor *descriptor;
    void *ring_virtual = NULL;
    void *buffer_virtual = NULL;
    volatile struct xhci_trb *ring;
    struct xhci_trb trb;
    uint64_t trb_physical;
    uint16_t next_index;
    bool next_cycle;
    uint32_t doorbell;
    if ((mmio == NULL) || (device == NULL) ||
        (endpoint_index >= device->hid_configuration.endpoint_count) ||
        (endpoint_index >= XHCI_MAX_HID_ENDPOINTS)) {
        return false;
    }
    runtime = &device->hid_runtime[endpoint_index];
    descriptor = &device->hid_configuration.endpoints[endpoint_index];
    if (!device->device_configured || !device->hid_endpoint_ready ||
        runtime->transfer_outstanding || (descriptor->endpoint_id <= 1U) ||
        (descriptor->max_packet == 0U) ||
        (descriptor->max_packet > XHCI_HID_REPORT_BUFFER_BYTES) ||
        (device->hid_ring_physical[endpoint_index] == 0ULL) ||
        !m52_allocate_report_buffer(runtime) ||
        !vmm_pmm_frame_to_hhdm(device->hid_ring_physical[endpoint_index],
                               &ring_virtual) ||
        !vmm_pmm_frame_to_hhdm(runtime->report_buffer_physical,
                               &buffer_virtual) ||
        !xhci_build_interrupt_in_trb(
            &trb, device->hid_ring_physical[endpoint_index],
            runtime->producer_index, runtime->producer_cycle,
            runtime->report_buffer_physical, descriptor->max_packet,
            &trb_physical, &next_index, &next_cycle)) {
        return false;
    }
    m52_zero_page(buffer_virtual);
    ring = (volatile struct xhci_trb *)ring_virtual;
    ring[runtime->producer_index] = trb;
    if (runtime->producer_index == XHCI_INTERRUPT_RING_USABLE - 1U) {
        ring[XHCI_INTERRUPT_RING_USABLE].parameter =
            device->hid_ring_physical[endpoint_index];
        ring[XHCI_INTERRUPT_RING_USABLE].status = 0U;
        ring[XHCI_INTERRUPT_RING_USABLE].control =
            ((uint32_t)M52_TRB_TYPE_LINK << M52_TRB_TYPE_SHIFT) |
            M52_TRB_TOGGLE_CYCLE |
            (runtime->producer_cycle ? M52_TRB_CYCLE : 0U);
    }
    if (runtime->submitted_transfers == UINT32_MAX) { return false; }
    runtime->expected_trb_physical = trb_physical;
    runtime->producer_index = next_index;
    runtime->producer_cycle = next_cycle;
    runtime->transfer_outstanding = true;
    ++runtime->submitted_transfers;
    m52_barrier();
    doorbell = (uint32_t)device->slot_id * 4U;
    if ((device->slot_id == 0U) ||
        (doorbell > M52_MMIO_WINDOW_SIZE - 4U) ||
        (device->hid_configuration.endpoints[endpoint_index].endpoint_id > 31U)) {
        runtime->transfer_outstanding = false;
        return false;
    }
    doorbell += ((struct xhci_state *)(uintptr_t)xhci_get_state())
                    ->capabilities.doorbell_offset;
    if (doorbell > M52_MMIO_WINDOW_SIZE - 4U) {
        runtime->transfer_outstanding = false;
        return false;
    }
    m52_mmio_write32(mmio, doorbell, (uint32_t)descriptor->endpoint_id);
    return true;
}

static uint32_t m53_usage_keycode(uint8_t usage) {
    if ((usage >= 0x04U) && (usage <= 0x1dU)) {
        return (uint32_t)BORING_KEY_A + (uint32_t)(usage - 0x04U);
    }
    switch (usage) {
        case 0x1eU: return BORING_KEY_1;
        case 0x1fU: return BORING_KEY_2;
        case 0x20U: return BORING_KEY_3;
        case 0x21U: return BORING_KEY_4;
        case 0x22U: return BORING_KEY_5;
        case 0x23U: return BORING_KEY_6;
        case 0x24U: return BORING_KEY_7;
        case 0x25U: return BORING_KEY_8;
        case 0x26U: return BORING_KEY_9;
        case 0x27U: return BORING_KEY_0;
        case 0x28U: return BORING_KEY_ENTER;
        case 0x29U: return BORING_KEY_ESCAPE;
        case 0x2aU: return BORING_KEY_BACKSPACE;
        case 0x2bU: return BORING_KEY_TAB;
        case 0x2cU: return BORING_KEY_SPACE;
        case 0x2dU: return BORING_KEY_MINUS;
        case 0x2eU: return BORING_KEY_EQUAL;
        case 0x2fU: return BORING_KEY_LEFT_BRACKET;
        case 0x30U: return BORING_KEY_RIGHT_BRACKET;
        case 0x31U: return BORING_KEY_BACKSLASH;
        case 0x33U: return BORING_KEY_SEMICOLON;
        case 0x34U: return BORING_KEY_APOSTROPHE;
        case 0x35U: return BORING_KEY_GRAVE;
        case 0x36U: return BORING_KEY_COMMA;
        case 0x37U: return BORING_KEY_DOT;
        case 0x38U: return BORING_KEY_SLASH;
        case 0x3aU: return BORING_KEY_F1;
        case 0x3bU: return BORING_KEY_F2;
        case 0x3cU: return BORING_KEY_F3;
        case 0x3dU: return BORING_KEY_F4;
        case 0x3eU: return BORING_KEY_F5;
        case 0x3fU: return BORING_KEY_F6;
        case 0x40U: return BORING_KEY_F7;
        case 0x41U: return BORING_KEY_F8;
        case 0x42U: return BORING_KEY_F9;
        case 0x43U: return BORING_KEY_F10;
        case 0x44U: return BORING_KEY_F11;
        case 0x45U: return BORING_KEY_F12;
        case 0x49U: return BORING_KEY_INSERT;
        case 0x4aU: return BORING_KEY_HOME;
        case 0x4bU: return BORING_KEY_PAGE_UP;
        case 0x4cU: return BORING_KEY_DELETE;
        case 0x4dU: return BORING_KEY_END;
        case 0x4eU: return BORING_KEY_PAGE_DOWN;
        case 0x4fU: return BORING_KEY_RIGHT;
        case 0x50U: return BORING_KEY_LEFT;
        case 0x51U: return BORING_KEY_DOWN;
        case 0x52U: return BORING_KEY_UP;
        default: return BORING_KEY_NONE;
    }
}

static uint32_t m53_modifier_keycode(uint8_t bit) {
    switch (bit) {
        case 0U: return BORING_KEY_LEFT_CTRL;
        case 1U: return BORING_KEY_LEFT_SHIFT;
        case 2U: return BORING_KEY_LEFT_ALT;
        case 3U: return BORING_KEY_LEFT_SUPER;
        case 4U: return BORING_KEY_RIGHT_CTRL;
        case 5U: return BORING_KEY_RIGHT_SHIFT;
        case 6U: return BORING_KEY_RIGHT_ALT;
        case 7U: return BORING_KEY_RIGHT_SUPER;
        default: return BORING_KEY_NONE;
    }
}

static void m53_publish_key(uint32_t code, bool down) {
    if ((code != (uint32_t)BORING_KEY_NONE) &&
        boring_input_submit_key(code, down)) {
        boring_event_input_irq();
    }
}

static void m53_publish_modifier_changes(uint8_t before, uint8_t after,
                                         bool down) {
    uint8_t bit;
    for (bit = 0U; bit < 8U; ++bit) {
        const uint8_t mask = (uint8_t)(1U << bit);
        const bool was_down = (before & mask) != 0U;
        const bool is_down = (after & mask) != 0U;
        if ((down && !was_down && is_down) ||
            (!down && was_down && !is_down)) {
            m53_publish_key(m53_modifier_keycode(bit), down);
        }
    }
}

static void m53_publish_pointer_buttons(uint8_t before, uint8_t after) {
    static const uint8_t bits[3] = { 0x01U, 0x02U, 0x04U };
    static const uint32_t buttons[3] = {
        BORING_MOUSE_BUTTON_LEFT,
        BORING_MOUSE_BUTTON_RIGHT,
        BORING_MOUSE_BUTTON_MIDDLE
    };
    size_t index;
    for (index = 0U; index < 3U; ++index) {
        if (((before ^ after) & bits[index]) != 0U &&
            boring_input_submit_mouse_button(
                buttons[index], (after & bits[index]) != 0U)) {
            boring_event_input_irq();
        }
    }
}

static void m53_publish_pointer_move(int32_t dx, int32_t dy) {
    if (((dx != 0) || (dy != 0)) &&
        boring_input_submit_mouse_move(dx, dy)) {
        boring_event_input_irq();
    }
}

static bool m52_decode_report(struct xhci_addressed_device *device,
                              uint8_t endpoint_index,
                              const uint8_t *report,
                              uint16_t length) {
    struct xhci_hid_endpoint_runtime *runtime =
        &device->hid_runtime[endpoint_index];
    const struct xhci_hid_endpoint_descriptor *descriptor =
        &device->hid_configuration.endpoints[endpoint_index];
    if (descriptor->protocol == 1U) {
        struct usb_hid_key_transition transitions[USB_HID_BOOT_KEYS * 2U];
        const uint8_t previous_modifiers = runtime->keyboard_state.modifiers;
        size_t count = 0U;
        uint8_t modifiers = 0U;
        size_t index;
        if (!usb_hid_keyboard_decode(&runtime->keyboard_state, report,
                                     (size_t)length, transitions,
                                     USB_HID_BOOT_KEYS * 2U, &count,
                                     &modifiers)) {
            return false;
        }
        for (index = 0U; index < count; ++index) {
            runtime->last_key_usage = transitions[index].usage;
            runtime->last_key_down = transitions[index].down;
            if (transitions[index].down) {
                if (runtime->key_presses == UINT32_MAX) { return false; }
                ++runtime->key_presses;
            } else {
                if (runtime->key_releases == UINT32_MAX) { return false; }
                ++runtime->key_releases;
            }
        }
        for (index = 0U; index < count; ++index) {
            if (!transitions[index].down) {
                m53_publish_key(m53_usage_keycode(transitions[index].usage),
                                false);
            }
        }
        m53_publish_modifier_changes(previous_modifiers, modifiers, false);
        m53_publish_modifier_changes(previous_modifiers, modifiers, true);
        for (index = 0U; index < count; ++index) {
            if (transitions[index].down) {
                m53_publish_key(m53_usage_keycode(transitions[index].usage),
                                true);
            }
        }
    } else if (descriptor->protocol == 2U) {
        struct usb_hid_mouse_report decoded;
        const uint8_t previous_buttons = runtime->last_pointer_buttons;
        if (!usb_hid_mouse_decode(report, (size_t)length, &decoded) ||
            (runtime->pointer_reports == UINT32_MAX)) {
            return false;
        }
        runtime->last_pointer_x = (uint16_t)decoded.dx;
        runtime->last_pointer_y = (uint16_t)decoded.dy;
        runtime->last_pointer_buttons = decoded.buttons;
        runtime->pointer_valid = true;
        ++runtime->pointer_reports;
        m53_publish_pointer_move((int32_t)decoded.dx, (int32_t)decoded.dy);
        m53_publish_pointer_buttons(previous_buttons, decoded.buttons);
    } else if (descriptor->protocol == 0U) {
        struct usb_hid_absolute_tablet_report decoded;
        const bool previous_valid = runtime->pointer_valid;
        const uint16_t previous_x = runtime->last_pointer_x;
        const uint16_t previous_y = runtime->last_pointer_y;
        const uint8_t previous_buttons = runtime->last_pointer_buttons;
        if (!usb_hid_absolute_tablet_decode(report, (size_t)length, &decoded) ||
            (runtime->pointer_reports == UINT32_MAX)) {
            return false;
        }
        runtime->last_pointer_x = decoded.x;
        runtime->last_pointer_y = decoded.y;
        runtime->last_pointer_buttons = decoded.buttons;
        runtime->pointer_valid = true;
        ++runtime->pointer_reports;
        if (previous_valid) {
            m53_publish_pointer_move((int32_t)decoded.x - (int32_t)previous_x,
                                     (int32_t)decoded.y - (int32_t)previous_y);
        }
        m53_publish_pointer_buttons(previous_buttons, decoded.buttons);
    } else {
        return false;
    }
    if (runtime->decoded_reports == UINT32_MAX) { return false; }
    ++runtime->decoded_reports;
    return true;
}

static bool m52_complete_event(struct xhci_state *active,
                               const struct xhci_trb *event,
                               uint32_t *completed) {
    uint8_t device_index;
    for (device_index = 0U; device_index < active->addressed_count;
         ++device_index) {
        struct xhci_addressed_device *device = &active->addressed[device_index];
        uint8_t endpoint_index;
        for (endpoint_index = 0U;
             endpoint_index < device->hid_configuration.endpoint_count;
             ++endpoint_index) {
            struct xhci_hid_endpoint_runtime *runtime =
                &device->hid_runtime[endpoint_index];
            const struct xhci_hid_endpoint_descriptor *descriptor =
                &device->hid_configuration.endpoints[endpoint_index];
            uint16_t actual = 0U;
            bool short_packet = false;
            void *buffer_virtual = NULL;
            bool validator_ok;
            if (!runtime->transfer_outstanding) { continue; }
            validator_ok = xhci_validate_interrupt_transfer_event(
                event, device->hid_ring_physical[endpoint_index],
                device->slot_id, descriptor->endpoint_id,
                runtime->expected_trb_physical, descriptor->max_packet,
                &actual, &short_packet);
            if (!validator_ok) {
                static bool traced_validator_failure;
                const uint8_t event_type = (uint8_t)(
                    (event->control >> M52_TRB_TYPE_SHIFT) &
                    M52_TRB_TYPE_MASK);
                const uint8_t event_endpoint = (uint8_t)(
                    (event->control >> M52_EVENT_ENDPOINT_SHIFT) &
                    M52_EVENT_ENDPOINT_MASK);
                const uint8_t event_slot =
                    (uint8_t)(event->control >> M52_EVENT_SLOT_SHIFT);
                const uint8_t completion =
                    (uint8_t)(event->status >> M52_EVENT_COMPLETION_SHIFT);
                const uint32_t residual =
                    event->status & M52_EVENT_RESIDUAL_MASK;
                const uint64_t ring =
                    device->hid_ring_physical[endpoint_index];
                const bool published_match =
                    (event_type == XHCI_TRB_TYPE_TRANSFER_EVENT) &&
                    (event_slot == device->slot_id) &&
                    (event_endpoint == descriptor->endpoint_id) &&
                    (event->parameter == runtime->expected_trb_physical);
                if (!traced_validator_failure && published_match) {
                    const char *reason = "completion";
                    const bool expected_aligned =
                        (runtime->expected_trb_physical & 0x0fULL) == 0ULL;
                    const bool event_aligned =
                        (event->parameter & 0x0fULL) == 0ULL;
                    const bool expected_owned = interrupt_pointer_owned(
                        runtime->expected_trb_physical, ring);
                    const bool event_owned =
                        interrupt_pointer_owned(event->parameter, ring);
                    if (ring == 0ULL) { reason = "ring-zero"; }
                    else if ((ring & 0x3fULL) != 0ULL) {
                        reason = "ring-align";
                    } else if (device->slot_id == 0U) {
                        reason = "slot-invalid";
                    } else if ((descriptor->endpoint_id <= 1U) ||
                               (descriptor->endpoint_id > 31U)) {
                        reason = "endpoint-invalid";
                    } else if ((descriptor->max_packet == 0U) ||
                               (descriptor->max_packet >
                                XHCI_HID_REPORT_BUFFER_BYTES)) {
                        reason = "requested-invalid";
                    } else if (!expected_aligned) {
                        reason = "expected-align";
                    } else if (!expected_owned) {
                        reason = "expected-ring-owner";
                    } else if (!event_aligned) {
                        reason = "event-align";
                    } else if (!event_owned) {
                        reason = "event-ring-owner";
                    } else if (residual > descriptor->max_packet) {
                        reason = "residual";
                    }
                    traced_validator_failure = true;
                    serial_write_string("m52-complete-diag: reason=");
                    serial_write_string(reason);
                    serial_write_string(" dev/slot/index/ep/proto/max=");
                    serial_write_u64((uint64_t)device_index);
                    serial_write_string("/");
                    serial_write_u64((uint64_t)device->slot_id);
                    serial_write_string("/");
                    serial_write_u64((uint64_t)endpoint_index);
                    serial_write_string("/");
                    serial_write_u64((uint64_t)descriptor->endpoint_id);
                    serial_write_string("/");
                    serial_write_u64((uint64_t)descriptor->protocol);
                    serial_write_string("/");
                    serial_write_u64((uint64_t)descriptor->max_packet);
                    serial_write_string("\n");
                    serial_write_string(
                        "m52-complete-diag: ring/out/expected/report=");
                    serial_write_u64(ring);
                    serial_write_string("/");
                    serial_write_u64(runtime->transfer_outstanding ?
                                     1ULL : 0ULL);
                    serial_write_string("/");
                    serial_write_u64(runtime->expected_trb_physical);
                    serial_write_string("/");
                    serial_write_u64(runtime->report_buffer_physical);
                    serial_write_string("\n");
                    serial_write_string(
                        "m52-complete-diag: event type/slot/ep/param/cc/res=");
                    serial_write_u64((uint64_t)event_type);
                    serial_write_string("/");
                    serial_write_u64((uint64_t)event_slot);
                    serial_write_string("/");
                    serial_write_u64((uint64_t)event_endpoint);
                    serial_write_string("/");
                    serial_write_u64(event->parameter);
                    serial_write_string("/");
                    serial_write_u64((uint64_t)completion);
                    serial_write_string("/");
                    serial_write_u64((uint64_t)residual);
                    serial_write_string("\n");
                    serial_write_string(
                        "m52-complete-diag: align/owned/equal/slot/ep/requested/validator/actual/short=");
                    serial_write_u64(expected_aligned ? 1ULL : 0ULL);
                    serial_write_string("/");
                    serial_write_u64(event_aligned ? 1ULL : 0ULL);
                    serial_write_string("/");
                    serial_write_u64(expected_owned ? 1ULL : 0ULL);
                    serial_write_string("/");
                    serial_write_u64(event_owned ? 1ULL : 0ULL);
                    serial_write_string("/");
                    serial_write_u64(event->parameter ==
                                     runtime->expected_trb_physical ?
                                     1ULL : 0ULL);
                    serial_write_string("/");
                    serial_write_u64(event_slot == device->slot_id ?
                                     1ULL : 0ULL);
                    serial_write_string("/");
                    serial_write_u64(event_endpoint == descriptor->endpoint_id ?
                                     1ULL : 0ULL);
                    serial_write_string("/");
                    serial_write_u64((descriptor->max_packet != 0U) &&
                                     (descriptor->max_packet <=
                                      XHCI_HID_REPORT_BUFFER_BYTES) ?
                                     1ULL : 0ULL);
                    serial_write_string("/0/");
                    serial_write_u64((uint64_t)actual);
                    serial_write_string("/");
                    serial_write_u64(short_packet ? 1ULL : 0ULL);
                    serial_write_string("\n");
                }
                continue;
            }
            if ((actual == 0U) ||
                !vmm_pmm_frame_to_hhdm(runtime->report_buffer_physical,
                                       &buffer_virtual) ||
                (runtime->completed_transfers == UINT32_MAX) ||
                (UINT32_MAX - runtime->report_bytes < actual) ||
                (device->transfer_events == UINT32_MAX)) {
                return false;
            }
            m52_barrier();
            runtime->transfer_outstanding = false;
            runtime->expected_trb_physical = 0ULL;
            runtime->last_report_length = actual;
            ++runtime->completed_transfers;
            runtime->report_bytes += actual;
            if (short_packet) {
                if (runtime->short_packets == UINT32_MAX) { return false; }
                ++runtime->short_packets;
            }
            ++device->transfer_events;
            if (!m52_decode_report(device, endpoint_index,
                                   (const uint8_t *)buffer_virtual, actual)) {
                return false;
            }
            if (*completed == UINT32_MAX) { return false; }
            ++*completed;
            return true;
        }
    }
    return false;
}

static bool m52_poll_hid_reports_limit(struct xhci_state *state,
                                        uint32_t completion_goal,
                                        uint32_t wait_limit,
                                        bool rearm_after_completion) {
    const struct xhci_state *published;
    struct xhci_state *active;
    volatile void *mapping = NULL;
    volatile uint8_t *mmio;
    void *event_virtual = NULL;
    volatile struct xhci_trb *event_ring;
    uint64_t consumed;
    uint16_t event_index;
    bool event_cycle;
    uint32_t completed = 0U;
    uint32_t attempt;
    uint8_t device_index;
    bool success = false;
    if ((state == NULL) || (completion_goal == 0U) || (wait_limit == 0U)) {
        return false;
    }
    published = xhci_get_state();
    if ((published == NULL) || !published->controller_running ||
        (published->addressed_count == 0U) ||
        !vmm_map_mmio_region(published->mmio_physical,
                             M52_MMIO_WINDOW_SIZE, &mapping) ||
        !vmm_pmm_frame_to_hhdm(published->event_ring_physical,
                               &event_virtual)) {
        if (mapping != NULL) {
            (void)vmm_unmap_mmio_region(mapping, M52_MMIO_WINDOW_SIZE);
        }
        return false;
    }
    active = (struct xhci_state *)(uintptr_t)published;
    mmio = (volatile uint8_t *)mapping;
    event_ring = (volatile struct xhci_trb *)event_virtual;
    for (device_index = 0U; device_index < active->addressed_count;
         ++device_index) {
        struct xhci_addressed_device *device = &active->addressed[device_index];
        uint8_t endpoint_index;
        if (!device->device_configured || !device->hid_endpoint_ready) {
            goto out;
        }
        for (endpoint_index = 0U;
             endpoint_index < device->hid_configuration.endpoint_count;
             ++endpoint_index) {
            if (!device->hid_runtime[endpoint_index].transfer_outstanding &&
                !m52_submit_endpoint(mmio, device, endpoint_index)) {
                goto out;
            }
        }
    }
    consumed = m52_consumed_events(active);
    event_index = (uint16_t)(consumed % XHCI_EVENT_RING_TRBS);
    event_cycle = (((consumed / XHCI_EVENT_RING_TRBS) & 1ULL) == 0ULL);
    for (attempt = 0U;
         (attempt < wait_limit) && (completed < completion_goal);
         ++attempt) {
        struct xhci_trb event;
        uint32_t control = event_ring[event_index].control;
        uint8_t type;
        const uint32_t interrupter = active->capabilities.runtime_offset +
                                     M52_RUNTIME_INTERRUPTER0;
        if (((control & M52_TRB_CYCLE) != 0U) != event_cycle) {
            x86_64_pause();
            continue;
        }
        event.parameter = event_ring[event_index].parameter;
        event.status = event_ring[event_index].status;
        event.control = control;
        ++event_index;
        if (event_index == XHCI_EVENT_RING_TRBS) {
            event_index = 0U;
            event_cycle = !event_cycle;
        }
        m52_barrier();
        if (interrupter > M52_MMIO_WINDOW_SIZE - 0x20U) { goto out; }
        m52_mmio_write64(mmio, interrupter + 0x18U,
            (active->event_ring_physical +
             ((uint64_t)event_index * XHCI_TRB_SIZE)) | (1ULL << 3U));
        type = (uint8_t)((event.control >> M52_TRB_TYPE_SHIFT) &
                         M52_TRB_TYPE_MASK);
        if (type == M52_TRB_TYPE_PORT_STATUS_EVENT) {
            goto out;
        }
        if ((type != XHCI_TRB_TYPE_TRANSFER_EVENT) ||
            !m52_complete_event(active, &event, &completed)) {
            goto out;
        }
        if ((completed < completion_goal) || rearm_after_completion) {
            for (device_index = 0U; device_index < active->addressed_count;
                 ++device_index) {
                struct xhci_addressed_device *device =
                    &active->addressed[device_index];
                uint8_t endpoint_index;
                for (endpoint_index = 0U;
                     endpoint_index < device->hid_configuration.endpoint_count;
                     ++endpoint_index) {
                    if (!device->hid_runtime[endpoint_index].transfer_outstanding &&
                        !m52_submit_endpoint(mmio, device, endpoint_index)) {
                        goto out;
                    }
                }
            }
        }
    }
    success = completed >= completion_goal;
out:
    *state = *active;
    if (mapping != NULL) {
        (void)vmm_unmap_mmio_region(mapping, M52_MMIO_WINDOW_SIZE);
    }
    return success;
}

bool xhci_poll_hid_reports(struct xhci_state *state, uint32_t completion_goal) {
    return m52_poll_hid_reports_limit(state, completion_goal,
                                      M52_EVENT_WAIT_LIMIT, false);
}

#define M54_SERVICE_EVENT_WAIT_LIMIT 4096U

bool xhci_service_hid_reports(struct xhci_state *state) {
    return m52_poll_hid_reports_limit(state, 1U,
                                      M54_SERVICE_EVENT_WAIT_LIMIT, true);
}

#endif
