#ifndef BORING_INPUT_H
#define BORING_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/input_abi.h>

enum boring_input_result {
    BORING_INPUT_RESULT_OK = 0,
    BORING_INPUT_RESULT_INVALID = 1,
    BORING_INPUT_RESULT_BUSY = 2,
    BORING_INPUT_RESULT_ACCESS = 3,
    BORING_INPUT_RESULT_NOT_INITIALIZED = 4
};

struct boring_input_stats {
    uint64_t owner_pid;
    uint64_t dropped_events;
    size_t queued_events;
    uint32_t modifiers;
    bool owned;
    bool waiting;
    bool initialized;
};

bool boring_input_init(void);
enum boring_input_result boring_input_claim(uint64_t pid);
enum boring_input_result boring_input_release(uint64_t pid);
bool boring_input_process_teardown(uint64_t pid, bool *released_out);
enum boring_input_result boring_input_read(uint64_t pid,
                                           struct boring_input_event *events,
                                           size_t capacity,
                                           size_t *count_out);
bool boring_input_wait_prepare(uint64_t pid);
void boring_input_wait_cancel(uint64_t pid);
bool boring_input_submit_key(uint32_t code, bool down);
bool boring_input_submit_mouse_move(int32_t dx, int32_t dy);
bool boring_input_submit_mouse_button(uint32_t button, bool down);
bool boring_input_get_stats(struct boring_input_stats *stats);

#endif
