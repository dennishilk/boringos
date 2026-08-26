#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/cpu.h>
#include <boring/input.h>

struct boring_input_state {
    struct boring_input_event queue[BORING_INPUT_QUEUE_CAPACITY];
    bool held[BORING_KEY_MAX + 1U];
    uint64_t owner_pid;
    uint64_t dropped_events;
    size_t head;
    size_t count;
    uint32_t modifiers;
    bool owned;
    bool owner_waiting;
    bool initialized;
};

static struct boring_input_state input_state;

static void input_restore_interrupts(bool enabled) {
    if (enabled) {
        x86_64_interrupts_enable();
    }
}

static void input_clear_queue(void) {
    input_state.head = 0U;
    input_state.count = 0U;
}

static void input_clear_keys(void) {
    size_t index;

    for (index = 0U; index <= (size_t)BORING_KEY_MAX; ++index) {
        input_state.held[index] = false;
    }
    input_state.modifiers = 0U;
}

static uint32_t input_modifier_mask(void) {
    uint32_t modifiers = 0U;

    if (input_state.held[BORING_KEY_LEFT_SHIFT] ||
        input_state.held[BORING_KEY_RIGHT_SHIFT]) {
        modifiers |= BORING_MOD_SHIFT;
    }
    if (input_state.held[BORING_KEY_LEFT_CTRL] ||
        input_state.held[BORING_KEY_RIGHT_CTRL]) {
        modifiers |= BORING_MOD_CTRL;
    }
    if (input_state.held[BORING_KEY_LEFT_ALT] ||
        input_state.held[BORING_KEY_RIGHT_ALT]) {
        modifiers |= BORING_MOD_ALT;
    }
    if (input_state.held[BORING_KEY_LEFT_SUPER] ||
        input_state.held[BORING_KEY_RIGHT_SUPER]) {
        modifiers |= BORING_MOD_SUPER;
    }
    return modifiers;
}

static bool input_push(const struct boring_input_event *event) {
    size_t tail;

    if ((!input_state.initialized) || (!input_state.owned) ||
        (event == NULL)) {
        return false;
    }
    if (input_state.count == (size_t)BORING_INPUT_QUEUE_CAPACITY) {
        if (input_state.dropped_events != UINT64_MAX) {
            ++input_state.dropped_events;
        }
        return false;
    }
    tail = (input_state.head + input_state.count) %
           (size_t)BORING_INPUT_QUEUE_CAPACITY;
    input_state.queue[tail] = *event;
    ++input_state.count;
    input_state.owner_waiting = false;
    return true;
}

bool boring_input_init(void) {
    size_t index;
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();

    x86_64_interrupts_disable();
    if (input_state.initialized) {
        input_restore_interrupts(interrupts_were_enabled);
        return false;
    }
    input_state.owner_pid = 0ULL;
    input_state.dropped_events = 0ULL;
    input_state.owned = false;
    input_state.owner_waiting = false;
    input_state.initialized = true;
    input_clear_queue();
    input_clear_keys();
    for (index = 0U; index < (size_t)BORING_INPUT_QUEUE_CAPACITY; ++index) {
        input_state.queue[index].type = 0U;
        input_state.queue[index].code = 0U;
        input_state.queue[index].value1 = 0;
        input_state.queue[index].value2 = 0;
        input_state.queue[index].modifiers = 0U;
        input_state.queue[index].flags = 0U;
    }
    input_restore_interrupts(interrupts_were_enabled);
    return true;
}

enum boring_input_result boring_input_claim(uint64_t pid) {
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();
    enum boring_input_result result = BORING_INPUT_RESULT_OK;

    x86_64_interrupts_disable();
    if (!input_state.initialized) {
        result = BORING_INPUT_RESULT_NOT_INITIALIZED;
    } else if (pid == 0ULL || pid == UINT64_MAX) {
        result = BORING_INPUT_RESULT_INVALID;
    } else if (input_state.owned && input_state.owner_pid != pid) {
        result = BORING_INPUT_RESULT_BUSY;
    } else if (!input_state.owned) {
        input_state.owned = true;
        input_state.owner_pid = pid;
        input_state.owner_waiting = false;
        input_clear_queue();
        input_clear_keys();
    }
    input_restore_interrupts(interrupts_were_enabled);
    return result;
}

enum boring_input_result boring_input_release(uint64_t pid) {
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();
    enum boring_input_result result = BORING_INPUT_RESULT_OK;

    x86_64_interrupts_disable();
    if (!input_state.initialized) {
        result = BORING_INPUT_RESULT_NOT_INITIALIZED;
    } else if ((!input_state.owned) || (input_state.owner_pid != pid)) {
        result = BORING_INPUT_RESULT_ACCESS;
    } else {
        input_state.owned = false;
        input_state.owner_pid = 0ULL;
        input_state.owner_waiting = false;
        input_clear_queue();
        input_clear_keys();
    }
    input_restore_interrupts(interrupts_were_enabled);
    return result;
}

bool boring_input_process_teardown(uint64_t pid, bool *released_out) {
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();
    bool released = false;

    x86_64_interrupts_disable();
    if ((!input_state.initialized) || (pid == 0ULL) || (pid == UINT64_MAX)) {
        input_restore_interrupts(interrupts_were_enabled);
        return false;
    }
    if (input_state.owned && input_state.owner_pid == pid) {
        input_state.owned = false;
        input_state.owner_pid = 0ULL;
        input_state.owner_waiting = false;
        input_clear_queue();
        input_clear_keys();
        released = true;
    }
    if (released_out != NULL) {
        *released_out = released;
    }
    input_restore_interrupts(interrupts_were_enabled);
    return true;
}

enum boring_input_result boring_input_read(uint64_t pid,
                                           struct boring_input_event *events,
                                           size_t capacity,
                                           size_t *count_out) {
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();
    enum boring_input_result result = BORING_INPUT_RESULT_OK;
    size_t count = 0U;

    x86_64_interrupts_disable();
    if (!input_state.initialized) {
        result = BORING_INPUT_RESULT_NOT_INITIALIZED;
    } else if ((events == NULL) || (count_out == NULL) || (capacity == 0U) ||
               (capacity > (size_t)BORING_INPUT_READ_MAX)) {
        result = BORING_INPUT_RESULT_INVALID;
    } else if ((!input_state.owned) || (input_state.owner_pid != pid)) {
        result = BORING_INPUT_RESULT_ACCESS;
    } else {
        while ((count < capacity) && (input_state.count != 0U)) {
            events[count] = input_state.queue[input_state.head];
            input_state.head = (input_state.head + 1U) %
                               (size_t)BORING_INPUT_QUEUE_CAPACITY;
            --input_state.count;
            ++count;
        }
        *count_out = count;
    }
    input_restore_interrupts(interrupts_were_enabled);
    return result;
}

bool boring_input_wait_prepare(uint64_t pid) {
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();
    bool prepared = false;

    x86_64_interrupts_disable();
    if (input_state.initialized && input_state.owned &&
        (input_state.owner_pid == pid) && (input_state.count == 0U)) {
        input_state.owner_waiting = true;
        prepared = true;
    }
    input_restore_interrupts(interrupts_were_enabled);
    return prepared;
}

void boring_input_wait_cancel(uint64_t pid) {
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();

    x86_64_interrupts_disable();
    if (input_state.initialized && input_state.owned &&
        (input_state.owner_pid == pid)) {
        input_state.owner_waiting = false;
    }
    input_restore_interrupts(interrupts_were_enabled);
}

bool boring_input_submit_key(uint32_t code, bool down) {
    struct boring_input_event event;
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();
    bool repeat;
    bool pushed;

    x86_64_interrupts_disable();
    if ((!input_state.initialized) || (!input_state.owned) ||
        (code == (uint32_t)BORING_KEY_NONE) ||
        (code > (uint32_t)BORING_KEY_MAX)) {
        input_restore_interrupts(interrupts_were_enabled);
        return false;
    }
    repeat = down && input_state.held[code];
    input_state.held[code] = down;
    input_state.modifiers = input_modifier_mask();
    event.type = BORING_INPUT_EVENT_KEY;
    event.code = code;
    event.value1 = down ? BORING_KEY_DOWN_VALUE : BORING_KEY_UP_VALUE;
    event.value2 = 0;
    event.modifiers = input_state.modifiers;
    event.flags = repeat ? BORING_INPUT_FLAG_REPEAT : 0U;
    pushed = input_push(&event);
    input_restore_interrupts(interrupts_were_enabled);
    return pushed;
}

bool boring_input_submit_mouse_move(int32_t dx, int32_t dy) {
    struct boring_input_event event;
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();
    bool pushed;

    if ((dx == 0) && (dy == 0)) {
        return true;
    }
    x86_64_interrupts_disable();
    event.type = BORING_INPUT_EVENT_MOUSE_MOVE;
    event.code = 0U;
    event.value1 = dx;
    event.value2 = dy;
    event.modifiers = input_state.modifiers;
    event.flags = 0U;
    pushed = input_push(&event);
    input_restore_interrupts(interrupts_were_enabled);
    return pushed;
}

bool boring_input_submit_mouse_button(uint32_t button, bool down) {
    struct boring_input_event event;
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();
    bool pushed;

    if ((button < BORING_MOUSE_BUTTON_LEFT) ||
        (button > BORING_MOUSE_BUTTON_RIGHT)) {
        return false;
    }
    x86_64_interrupts_disable();
    event.type = BORING_INPUT_EVENT_MOUSE_BUTTON;
    event.code = button;
    event.value1 = down ? 1 : 0;
    event.value2 = 0;
    event.modifiers = input_state.modifiers;
    event.flags = 0U;
    pushed = input_push(&event);
    input_restore_interrupts(interrupts_were_enabled);
    return pushed;
}

bool boring_input_get_stats(struct boring_input_stats *stats) {
    const bool interrupts_were_enabled = x86_64_interrupts_enabled();

    if (stats == NULL) {
        return false;
    }
    x86_64_interrupts_disable();
    if (!input_state.initialized) {
        input_restore_interrupts(interrupts_were_enabled);
        return false;
    }
    stats->owner_pid = input_state.owner_pid;
    stats->dropped_events = input_state.dropped_events;
    stats->queued_events = input_state.count;
    stats->modifiers = input_state.modifiers;
    stats->owned = input_state.owned;
    stats->waiting = input_state.owner_waiting;
    stats->initialized = input_state.initialized;
    input_restore_interrupts(interrupts_were_enabled);
    return true;
}
