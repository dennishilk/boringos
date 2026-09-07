#ifndef BORING_PS2_MOUSE_H
#define BORING_PS2_MOUSE_H

#include <stdbool.h>
#include <stdint.h>

#define PS2_MOUSE_BUTTON_LEFT_BIT (1U << 0)
#define PS2_MOUSE_BUTTON_RIGHT_BIT (1U << 1)
#define PS2_MOUSE_BUTTON_MIDDLE_BIT (1U << 2)
#define PS2_MOUSE_BUTTON_MASK (PS2_MOUSE_BUTTON_LEFT_BIT | \
                               PS2_MOUSE_BUTTON_RIGHT_BIT | \
                               PS2_MOUSE_BUTTON_MIDDLE_BIT)

struct ps2_mouse_decoder {
    uint8_t bytes[3];
    uint8_t index;
    uint8_t buttons;
};

struct ps2_mouse_packet {
    int32_t dx;
    int32_t dy;
    uint8_t buttons;
    uint8_t changed_buttons;
    bool movement_valid;
    bool overflow;
};

void ps2_mouse_decoder_init(struct ps2_mouse_decoder *decoder);
bool ps2_mouse_decoder_feed(struct ps2_mouse_decoder *decoder,
                            uint8_t byte,
                            struct ps2_mouse_packet *packet);

#endif
