#include <boring/usb_hid.h>
#include <boring/xhci.h>

#if !__STDC_HOSTED__ || defined(BORING_USB_HID_RUNTIME_HOST_TEST)
#include <boring/cpu.h>
#include <boring/event_syscall.h>
#include <boring/input.h>
#if defined(BORING_M61_PHYSICAL_BREADCRUMBS)
#include <boring/m61_runtime_hid.h>
#endif
#include <boring/pmm.h>
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

#define HID_USAGE_PAGE_GENERIC_DESKTOP 0x01U
#define HID_USAGE_PAGE_BUTTON 0x09U
#define HID_USAGE_POINTER 0x01U
#define HID_USAGE_MOUSE 0x02U
#define HID_USAGE_X 0x30U
#define HID_USAGE_Y 0x31U
#define HID_USAGE_WHEEL 0x38U
#define HID_REPORT_STATE_CAPACITY 8U
#define HID_GLOBAL_STACK_CAPACITY 4U
#define HID_LOCAL_USAGE_CAPACITY 16U
#define HID_COLLECTION_DEPTH_CAPACITY 8U

struct hid_report_global_state {
    uint32_t usage_page;
    uint32_t report_size;
    uint32_t report_count;
    int32_t logical_minimum;
    int32_t logical_maximum;
    uint8_t report_id;
    bool report_size_set;
    bool report_count_set;
    bool logical_minimum_set;
    bool logical_maximum_set;
};

struct hid_report_local_state {
    uint32_t usages[HID_LOCAL_USAGE_CAPACITY];
    uint32_t usage_minimum;
    uint32_t usage_maximum;
    uint8_t usage_count;
    bool usage_minimum_set;
    bool usage_maximum_set;
};

struct hid_mouse_report_state {
    struct usb_hid_mouse_layout layout;
    uint16_t bits;
    uint8_t report_id;
    bool used;
    bool x_found;
    bool y_found;
};

static uint32_t hid_item_unsigned(const uint8_t *bytes, uint8_t size) {
    uint32_t value = 0U;
    uint8_t index;
    for (index = 0U; index < size; ++index) {
        value |= (uint32_t)bytes[index] << ((uint32_t)index * 8U);
    }
    return value;
}

static int32_t hid_item_signed(const uint8_t *bytes, uint8_t size) {
    const uint32_t value = hid_item_unsigned(bytes, size);
    if ((size != 0U) && (size < 4U) &&
        ((value & (1U << ((uint32_t)size * 8U - 1U))) != 0U)) {
        const uint32_t mask = UINT32_MAX << ((uint32_t)size * 8U);
        return (int32_t)(value | mask);
    }
    return (int32_t)value;
}

static void hid_local_reset(struct hid_report_local_state *local) {
    *local = (struct hid_report_local_state){0};
}

static uint32_t hid_usage_page(uint32_t usage, uint32_t global_page) {
    return (usage > 0xffffU) ? (usage >> 16U) : global_page;
}

static uint32_t hid_usage_value(uint32_t usage) {
    return usage & 0xffffU;
}

static bool hid_local_usage_for_field(
    const struct hid_report_local_state *local, uint32_t index,
    uint32_t *usage) {
    if ((local == NULL) || (usage == NULL)) { return false; }
    if (index < local->usage_count) {
        *usage = local->usages[index];
        return true;
    }
    if (local->usage_minimum_set && local->usage_maximum_set &&
        (local->usage_minimum <= local->usage_maximum) &&
        (index <= local->usage_maximum - local->usage_minimum)) {
        *usage = local->usage_minimum + index;
        return true;
    }
    if (local->usage_count != 0U) {
        *usage = local->usages[local->usage_count - 1U];
        return true;
    }
    return false;
}

static struct hid_mouse_report_state *hid_report_state(
    struct hid_mouse_report_state states[HID_REPORT_STATE_CAPACITY],
    uint8_t report_id) {
    uint8_t index;
    for (index = 0U; index < HID_REPORT_STATE_CAPACITY; ++index) {
        if (states[index].used && (states[index].report_id == report_id)) {
            return &states[index];
        }
    }
    for (index = 0U; index < HID_REPORT_STATE_CAPACITY; ++index) {
        if (!states[index].used) {
            states[index].used = true;
            states[index].report_id = report_id;
            states[index].layout.report_id = report_id;
            states[index].layout.has_report_id = report_id != 0U;
            return &states[index];
        }
    }
    return NULL;
}

static bool hid_layout_axis(
    struct hid_mouse_report_state *state, uint32_t usage,
    uint16_t bit_offset, uint32_t bit_size,
    const struct hid_report_global_state *global) {
    uint8_t *bits = NULL;
    uint16_t *offset = NULL;
    int32_t *minimum = NULL;
    int32_t *maximum = NULL;
    bool *found = NULL;

    if ((state == NULL) || (global == NULL) ||
        (bit_size == 0U) || (bit_size > 16U) ||
        !global->logical_minimum_set || !global->logical_maximum_set ||
        (global->logical_minimum >= 0) ||
        (global->logical_maximum <= global->logical_minimum)) {
        return false;
    }
    if (usage == HID_USAGE_X) {
        bits = &state->layout.x_bits;
        offset = &state->layout.x_bit_offset;
        minimum = &state->layout.x_logical_minimum;
        maximum = &state->layout.x_logical_maximum;
        found = &state->x_found;
    } else if (usage == HID_USAGE_Y) {
        bits = &state->layout.y_bits;
        offset = &state->layout.y_bit_offset;
        minimum = &state->layout.y_logical_minimum;
        maximum = &state->layout.y_logical_maximum;
        found = &state->y_found;
    } else if (usage == HID_USAGE_WHEEL) {
        if (bit_size > 8U) { return false; }
        bits = &state->layout.wheel_bits;
        offset = &state->layout.wheel_bit_offset;
        minimum = &state->layout.wheel_logical_minimum;
        maximum = &state->layout.wheel_logical_maximum;
        found = &state->layout.has_wheel;
    } else {
        return false;
    }
    if (*found) { return true; }
    *bits = (uint8_t)bit_size;
    *offset = bit_offset;
    *minimum = global->logical_minimum;
    *maximum = global->logical_maximum;
    *found = true;
    return true;
}

static enum usb_hid_report_parse_result hid_parse_input_item(
    uint32_t flags, bool inside_mouse,
    const struct hid_report_global_state *global,
    const struct hid_report_local_state *local,
    struct hid_mouse_report_state states[HID_REPORT_STATE_CAPACITY]) {
    struct hid_mouse_report_state *state;
    uint32_t item_bits;
    uint32_t index;
    const bool constant = (flags & 0x01U) != 0U;
    const bool variable = (flags & 0x02U) != 0U;
    const bool relative = (flags & 0x04U) != 0U;

    if ((global == NULL) || (local == NULL) ||
        !global->report_size_set || !global->report_count_set ||
        (global->report_size == 0U) || (global->report_count == 0U) ||
        (global->report_size > USB_HID_MOUSE_REPORT_MAX_BITS) ||
        (global->report_count > USB_HID_MOUSE_REPORT_MAX_BITS) ||
        (global->report_size >
         USB_HID_MOUSE_REPORT_MAX_BITS / global->report_count)) {
        return USB_HID_REPORT_PARSE_BIT_OVERFLOW;
    }
    item_bits = global->report_size * global->report_count;
    state = hid_report_state(states, global->report_id);
    if (state == NULL) { return USB_HID_REPORT_PARSE_VALID_UNSUPPORTED; }
    if ((uint32_t)state->bits > USB_HID_MOUSE_REPORT_MAX_BITS - item_bits) {
        return USB_HID_REPORT_PARSE_BIT_OVERFLOW;
    }

    if (inside_mouse && !constant && variable) {
        uint32_t first_usage = 0U;
        if (hid_local_usage_for_field(local, 0U, &first_usage) &&
            (hid_usage_page(first_usage, global->usage_page) ==
             HID_USAGE_PAGE_BUTTON) &&
            (hid_usage_value(first_usage) == 1U) &&
            (global->report_size == 1U) &&
            (state->layout.button_count == 0U)) {
            state->layout.buttons_bit_offset = state->bits;
            state->layout.button_count =
                (global->report_count > 8U)
                    ? 8U : (uint8_t)global->report_count;
        }
        for (index = 0U; index < global->report_count; ++index) {
            uint32_t usage;
            const uint32_t bit_offset =
                (uint32_t)state->bits + index * global->report_size;
            if (!hid_local_usage_for_field(local, index, &usage) ||
                (hid_usage_page(usage, global->usage_page) !=
                 HID_USAGE_PAGE_GENERIC_DESKTOP)) {
                continue;
            }
            if (relative &&
                ((hid_usage_value(usage) == HID_USAGE_X) ||
                 (hid_usage_value(usage) == HID_USAGE_Y) ||
                 (hid_usage_value(usage) == HID_USAGE_WHEEL))) {
                (void)hid_layout_axis(
                    state, hid_usage_value(usage), (uint16_t)bit_offset,
                    global->report_size, global);
            }
        }
    }
    state->bits = (uint16_t)((uint32_t)state->bits + item_bits);
    return USB_HID_REPORT_PARSE_SUPPORTED;
}

enum usb_hid_report_parse_result usb_hid_parse_mouse_report_descriptor(
    const uint8_t *bytes, size_t length,
    struct usb_hid_mouse_layout *layout) {
    struct hid_report_global_state global = {0};
    struct hid_report_global_state
        global_stack[HID_GLOBAL_STACK_CAPACITY];
    struct hid_report_local_state local = {0};
    struct hid_mouse_report_state states[HID_REPORT_STATE_CAPACITY] = {0};
    bool mouse_collections[HID_COLLECTION_DEPTH_CAPACITY] = {false};
    size_t offset = 0U;
    uint8_t global_depth = 0U;
    uint8_t collection_depth = 0U;
    uint8_t index;

    if ((bytes == NULL) || (layout == NULL) || (length == 0U) ||
        (length > USB_HID_REPORT_DESCRIPTOR_MAX_BYTES)) {
        return USB_HID_REPORT_PARSE_MALFORMED;
    }
    while (offset < length) {
        const uint8_t prefix = bytes[offset++];
        uint8_t size;
        uint8_t type;
        uint8_t tag;
        uint32_t value;

        if (prefix == 0xfeU) {
            uint8_t long_size;
            if (length - offset < 2U) {
                return USB_HID_REPORT_PARSE_MALFORMED;
            }
            long_size = bytes[offset];
            offset += 2U;
            if ((size_t)long_size > length - offset) {
                return USB_HID_REPORT_PARSE_MALFORMED;
            }
            offset += long_size;
            continue;
        }
        size = (uint8_t)(prefix & 0x03U);
        if (size == 3U) { size = 4U; }
        type = (uint8_t)((prefix >> 2U) & 0x03U);
        tag = (uint8_t)(prefix >> 4U);
        if ((size_t)size > length - offset) {
            return USB_HID_REPORT_PARSE_MALFORMED;
        }
        value = hid_item_unsigned(&bytes[offset], size);

        if (type == 1U) {
            switch (tag) {
                case 0U:
                    if ((size == 0U) || (value > 0xffffU)) {
                        return USB_HID_REPORT_PARSE_VALID_UNSUPPORTED;
                    }
                    global.usage_page = value;
                    break;
                case 1U:
                    if (size == 0U) {
                        return USB_HID_REPORT_PARSE_MALFORMED;
                    }
                    global.logical_minimum =
                        hid_item_signed(&bytes[offset], size);
                    global.logical_minimum_set = true;
                    break;
                case 2U:
                    if (size == 0U) {
                        return USB_HID_REPORT_PARSE_MALFORMED;
                    }
                    global.logical_maximum =
                        (global.logical_minimum_set &&
                         (global.logical_minimum < 0))
                            ? hid_item_signed(&bytes[offset], size)
                            : (int32_t)value;
                    global.logical_maximum_set = true;
                    break;
                case 7U:
                    if (size == 0U) {
                        return USB_HID_REPORT_PARSE_MALFORMED;
                    }
                    global.report_size = value;
                    global.report_size_set = true;
                    break;
                case 8U:
                    if ((size == 0U) || (value == 0U) || (value > 255U)) {
                        return USB_HID_REPORT_PARSE_MALFORMED;
                    }
                    global.report_id = (uint8_t)value;
                    break;
                case 9U:
                    if (size == 0U) {
                        return USB_HID_REPORT_PARSE_MALFORMED;
                    }
                    global.report_count = value;
                    global.report_count_set = true;
                    break;
                case 10U:
                    if ((size != 0U) ||
                        (global_depth == HID_GLOBAL_STACK_CAPACITY)) {
                        return USB_HID_REPORT_PARSE_MALFORMED;
                    }
                    global_stack[global_depth++] = global;
                    break;
                case 11U:
                    if ((size != 0U) || (global_depth == 0U)) {
                        return USB_HID_REPORT_PARSE_MALFORMED;
                    }
                    global = global_stack[--global_depth];
                    break;
                default:
                    break;
            }
        } else if (type == 2U) {
            switch (tag) {
                case 0U:
                    if ((size == 0U) ||
                        (local.usage_count == HID_LOCAL_USAGE_CAPACITY)) {
                        return USB_HID_REPORT_PARSE_VALID_UNSUPPORTED;
                    }
                    local.usages[local.usage_count++] = value;
                    break;
                case 1U:
                    if (size == 0U) {
                        return USB_HID_REPORT_PARSE_MALFORMED;
                    }
                    local.usage_minimum = value;
                    local.usage_minimum_set = true;
                    break;
                case 2U:
                    if (size == 0U) {
                        return USB_HID_REPORT_PARSE_MALFORMED;
                    }
                    local.usage_maximum = value;
                    local.usage_maximum_set = true;
                    break;
                case 10U:
                    return USB_HID_REPORT_PARSE_VALID_UNSUPPORTED;
                default:
                    break;
            }
        } else if (type == 0U) {
            if (tag == 8U) {
                enum usb_hid_report_parse_result result =
                    hid_parse_input_item(
                        value,
                        (collection_depth != 0U) &&
                        mouse_collections[collection_depth - 1U],
                        &global, &local, states);
                if (result != USB_HID_REPORT_PARSE_SUPPORTED) {
                    return result;
                }
            } else if (tag == 10U) {
                uint32_t usage = 0U;
                const bool parent_mouse =
                    (collection_depth != 0U) &&
                    mouse_collections[collection_depth - 1U];
                bool current_mouse = parent_mouse;
                if ((size == 0U) ||
                    (collection_depth == HID_COLLECTION_DEPTH_CAPACITY)) {
                    return USB_HID_REPORT_PARSE_MALFORMED;
                }
                if (hid_local_usage_for_field(&local, 0U, &usage) &&
                    (hid_usage_page(usage, global.usage_page) ==
                     HID_USAGE_PAGE_GENERIC_DESKTOP) &&
                    ((hid_usage_value(usage) == HID_USAGE_MOUSE) ||
                     (parent_mouse &&
                      (hid_usage_value(usage) == HID_USAGE_POINTER)))) {
                    current_mouse = true;
                }
                mouse_collections[collection_depth++] = current_mouse;
            } else if (tag == 12U) {
                if ((size != 0U) || (collection_depth == 0U)) {
                    return USB_HID_REPORT_PARSE_MALFORMED;
                }
                --collection_depth;
            } else if ((tag != 9U) && (tag != 11U)) {
                return USB_HID_REPORT_PARSE_VALID_UNSUPPORTED;
            }
            hid_local_reset(&local);
        } else {
            return USB_HID_REPORT_PARSE_VALID_UNSUPPORTED;
        }
        offset += size;
    }
    if ((collection_depth != 0U) || (global_depth != 0U)) {
        return USB_HID_REPORT_PARSE_MALFORMED;
    }
    for (index = 0U; index < HID_REPORT_STATE_CAPACITY; ++index) {
        if (!states[index].used) { continue; }
        states[index].layout.report_bits = states[index].bits;
        if (states[index].x_found && states[index].y_found &&
            (states[index].bits != 0U)) {
            *layout = states[index].layout;
            return USB_HID_REPORT_PARSE_SUPPORTED;
        }
    }
    return USB_HID_REPORT_PARSE_VALID_UNSUPPORTED;
}

static bool hid_extract_bits(const uint8_t *report, size_t length,
                             uint16_t offset, uint8_t count,
                             uint32_t *value) {
    uint32_t extracted = 0U;
    uint8_t index;
    if ((report == NULL) || (value == NULL) || (count == 0U) ||
        (count > 16U) ||
        (length > SIZE_MAX / 8U) ||
        ((size_t)offset + (size_t)count > length * 8U)) {
        return false;
    }
    for (index = 0U; index < count; ++index) {
        const size_t bit = (size_t)offset + index;
        if ((report[bit / 8U] & (uint8_t)(1U << (bit % 8U))) != 0U) {
            extracted |= 1U << index;
        }
    }
    *value = extracted;
    return true;
}

static int32_t hid_sign_extend(uint32_t value, uint8_t bits) {
    const uint32_t sign = 1U << (bits - 1U);
    return (int32_t)((value ^ sign) - sign);
}

bool usb_hid_mouse_layout_decode(
    const struct usb_hid_mouse_layout *layout,
    const uint8_t *report, size_t length,
    struct usb_hid_mouse_report *decoded) {
    const uint16_t prefix = layout != NULL && layout->has_report_id ? 8U : 0U;
    uint32_t x;
    uint32_t y;
    uint32_t wheel = 0U;
    uint8_t buttons = 0U;
    uint8_t index;

    if ((layout == NULL) || (report == NULL) || (decoded == NULL) ||
        (length > SIZE_MAX / 8U) ||
        (layout->report_bits == 0U) ||
        (layout->report_bits > USB_HID_MOUSE_REPORT_MAX_BITS) ||
        (layout->x_bits == 0U) || (layout->x_bits > 16U) ||
        (layout->y_bits == 0U) || (layout->y_bits > 16U) ||
        (layout->button_count > 8U) ||
        (layout->has_wheel &&
         ((layout->wheel_bits == 0U) || (layout->wheel_bits > 8U))) ||
        (layout->has_report_id &&
         ((length == 0U) || (report[0] != layout->report_id))) ||
        ((size_t)layout->report_bits + prefix > length * 8U) ||
        !hid_extract_bits(report, length,
                          (uint16_t)(layout->x_bit_offset + prefix),
                          layout->x_bits, &x) ||
        !hid_extract_bits(report, length,
                          (uint16_t)(layout->y_bit_offset + prefix),
                          layout->y_bits, &y)) {
        return false;
    }
    if (layout->has_wheel &&
        !hid_extract_bits(report, length,
                          (uint16_t)(layout->wheel_bit_offset + prefix),
                          layout->wheel_bits, &wheel)) {
        return false;
    }
    for (index = 0U; index < layout->button_count; ++index) {
        uint32_t down;
        if (!hid_extract_bits(
                report, length,
                (uint16_t)(layout->buttons_bit_offset + prefix + index),
                1U, &down)) {
            return false;
        }
        if (down != 0U) { buttons = (uint8_t)(buttons | (uint8_t)(1U << index)); }
    }
    decoded->buttons = buttons;
    decoded->dx = (int16_t)hid_sign_extend(x, layout->x_bits);
    decoded->dy = (int16_t)hid_sign_extend(y, layout->y_bits);
    decoded->wheel = layout->has_wheel
        ? (int8_t)hid_sign_extend(wheel, layout->wheel_bits) : 0;
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

#if !__STDC_HOSTED__ || defined(BORING_USB_HID_RUNTIME_HOST_TEST)

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

static bool m52_submit_endpoint(const struct xhci_state *active,
                                volatile uint8_t *mmio,
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
    if ((active == NULL) || (mmio == NULL) || (device == NULL) ||
        (device->controller_index != active->controller_index) ||
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
    doorbell += active->capabilities.doorbell_offset;
    if (doorbell > M52_MMIO_WINDOW_SIZE - 4U) {
        runtime->transfer_outstanding = false;
        return false;
    }
    m52_mmio_write32(mmio, doorbell, (uint32_t)descriptor->endpoint_id);
#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
    if ((device->topology.depth != 0U) &&
        ((descriptor->report_format == XHCI_HID_REPORT_BOOT_MOUSE) ||
         (descriptor->report_format == XHCI_HID_REPORT_GENERIC_MOUSE))) {
        boring_m66_physical_usb_mouse_witness(
            (uint8_t)M66_POST_DOWNSTREAM_INTERRUPT_ARMED);
    }
#endif
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
    if (runtime->decoded_reports == UINT32_MAX) { return false; }
    if (descriptor->report_format == XHCI_HID_REPORT_BOOT_KEYBOARD) {
        struct usb_hid_keyboard_state next_state = runtime->keyboard_state;
        struct usb_hid_key_transition transitions[USB_HID_BOOT_KEYS * 2U];
        const uint8_t previous_modifiers = runtime->keyboard_state.modifiers;
        size_t count = 0U;
        size_t presses = 0U;
        size_t releases = 0U;
        uint8_t modifiers = 0U;
        size_t index;
        if (!usb_hid_keyboard_decode(&next_state, report,
                                     (size_t)length, transitions,
                                     USB_HID_BOOT_KEYS * 2U, &count,
                                     &modifiers)) {
            return false;
        }
        for (index = 0U; index < count; ++index) {
            if (transitions[index].down) {
                ++presses;
            } else {
                ++releases;
            }
        }
        if ((UINT32_MAX - runtime->key_presses < (uint32_t)presses) ||
            (UINT32_MAX - runtime->key_releases < (uint32_t)releases)) {
            return false;
        }
        runtime->keyboard_state = next_state;
        runtime->key_presses += (uint32_t)presses;
        runtime->key_releases += (uint32_t)releases;
        for (index = 0U; index < count; ++index) {
            runtime->last_key_usage = transitions[index].usage;
            runtime->last_key_down = transitions[index].down;
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
    } else if ((descriptor->report_format == XHCI_HID_REPORT_BOOT_MOUSE) ||
               (descriptor->report_format == XHCI_HID_REPORT_GENERIC_MOUSE)) {
        struct usb_hid_mouse_report decoded;
        const uint8_t previous_buttons = runtime->last_pointer_buttons;
        bool decoded_ok;
        if ((descriptor->report_format == XHCI_HID_REPORT_GENERIC_MOUSE) &&
            descriptor->mouse_layout.has_report_id &&
            ((length == 0U) ||
             (report[0] != descriptor->mouse_layout.report_id))) {
            return true;
        }
        decoded_ok = descriptor->report_format == XHCI_HID_REPORT_BOOT_MOUSE
            ? usb_hid_mouse_decode(report, (size_t)length, &decoded)
            : usb_hid_mouse_layout_decode(
                  &descriptor->mouse_layout, report, (size_t)length,
                  &decoded);
        if (!decoded_ok ||
            (runtime->pointer_reports == UINT32_MAX)) {
            return false;
        }
        runtime->last_pointer_x = (uint16_t)decoded.dx;
        runtime->last_pointer_y = (uint16_t)decoded.dy;
        runtime->last_pointer_buttons = decoded.buttons;
        runtime->pointer_valid = true;
        ++runtime->pointer_reports;
#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
        if (device->topology.depth != 0U) {
            boring_m66_physical_usb_mouse_witness(
                (uint8_t)M66_POST_DOWNSTREAM_MOUSE_REPORT);
        }
#endif
        m53_publish_pointer_move((int32_t)decoded.dx, (int32_t)decoded.dy);
#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
        if ((device->topology.depth != 0U) &&
            ((decoded.dx != 0) || (decoded.dy != 0))) {
            boring_m66_physical_usb_mouse_witness(
                (uint8_t)M66_POST_CANONICAL_MOUSE_MOVE);
        }
#endif
        m53_publish_pointer_buttons(previous_buttons, decoded.buttons);
    } else if (descriptor->report_format ==
               XHCI_HID_REPORT_QEMU_ABSOLUTE_TABLET) {
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
            if (!runtime->transfer_outstanding) { continue; }
            if (!xhci_validate_interrupt_transfer_event(
                    event, device->hid_ring_physical[endpoint_index],
                    device->slot_id, descriptor->endpoint_id,
                    runtime->expected_trb_physical, descriptor->max_packet,
                    &actual, &short_packet)) {
                continue;
            }
#if defined(BORING_M61_PHYSICAL_BREADCRUMBS)
            boring_m61_runtime_hid_post(
                (uint8_t)M61_RUNTIME_HID_POST_C_EVENT_VALIDATED);
#endif
            if ((actual == 0U) ||
                !vmm_pmm_frame_to_hhdm(runtime->report_buffer_physical,
                                       &buffer_virtual) ||
                (runtime->completed_transfers == UINT32_MAX) ||
                (UINT32_MAX - runtime->report_bytes < actual) ||
                (device->transfer_events == UINT32_MAX) ||
                (short_packet && (runtime->short_packets == UINT32_MAX)) ||
                (*completed == UINT32_MAX)) {
                return false;
            }
            m52_barrier();
            if (!m52_decode_report(device, endpoint_index,
                                   (const uint8_t *)buffer_virtual, actual)) {
                return false;
            }
            runtime->transfer_outstanding = false;
            runtime->expected_trb_physical = 0ULL;
            runtime->last_report_length = actual;
            ++runtime->completed_transfers;
            runtime->report_bytes += actual;
            if (short_packet) { ++runtime->short_packets; }
            ++device->transfer_events;
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
    uint16_t event_index;
    bool event_cycle;
    uint32_t completed = 0U;
    uint32_t attempt;
    uint8_t device_index;
    bool success = false;
    if ((state == NULL) || (completion_goal == 0U) || (wait_limit == 0U)) {
        return false;
    }
    published = xhci_get_controller(state->controller_index);
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
                !m52_submit_endpoint(active, mmio, device, endpoint_index)) {
                goto out;
            }
        }
    }
    if (!xhci_event_dequeue_position(active, &event_index, &event_cycle)) {
        goto out;
    }
    for (attempt = 0U;
         (attempt < wait_limit) && (completed < completion_goal);
         ++attempt) {
        struct xhci_trb event;
        uint32_t control = event_ring[event_index].control;
        uint8_t type;
        uint16_t next_index;
        bool next_cycle;
        const uint32_t interrupter = active->capabilities.runtime_offset +
                                     M52_RUNTIME_INTERRUPTER0;
        if (((control & M52_TRB_CYCLE) != 0U) != event_cycle) {
            x86_64_pause();
            continue;
        }
        event.parameter = event_ring[event_index].parameter;
        event.status = event_ring[event_index].status;
        event.control = control;
        type = (uint8_t)((event.control >> M52_TRB_TYPE_SHIFT) &
                         M52_TRB_TYPE_MASK);
#if defined(BORING_M61_PHYSICAL_BREADCRUMBS)
        if (type == XHCI_TRB_TYPE_TRANSFER_EVENT) {
            boring_m61_runtime_hid_post(
                (uint8_t)M61_RUNTIME_HID_POST_B_TRANSFER_EVENT);
        }
#endif
        if ((type == M52_TRB_TYPE_PORT_STATUS_EVENT) &&
            !xhci_consume_port_status_event(active, &event)) {
            goto out;
        }
        if ((interrupter > M52_MMIO_WINDOW_SIZE - 0x20U) ||
            !xhci_event_dequeue_advance(active, event_index, event_cycle,
                                        &next_index, &next_cycle)) {
            goto out;
        }
        m52_barrier();
        m52_mmio_write64(mmio, interrupter + 0x18U,
            (active->event_ring_physical +
             ((uint64_t)next_index * XHCI_TRB_SIZE)) | (1ULL << 3U));
        event_index = next_index;
        event_cycle = next_cycle;
        if (type == M52_TRB_TYPE_PORT_STATUS_EVENT) { goto out; }
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
                        !m52_submit_endpoint(active, mmio, device, endpoint_index)) {
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
