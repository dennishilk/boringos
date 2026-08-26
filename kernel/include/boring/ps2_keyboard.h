#ifndef BORING_PS2_KEYBOARD_H
#define BORING_PS2_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

struct ps2_keyboard_decoder {
    bool extended;
    uint8_t e1_remaining;
};

struct ps2_keyboard_transition {
    uint32_t code;
    bool down;
};

void ps2_keyboard_decoder_init(struct ps2_keyboard_decoder *decoder);
bool ps2_keyboard_decoder_feed(struct ps2_keyboard_decoder *decoder,
                               uint8_t byte,
                               struct ps2_keyboard_transition *transition);

#endif
