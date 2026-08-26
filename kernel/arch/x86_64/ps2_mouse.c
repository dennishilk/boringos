#include <stdbool.h>
#include <stdint.h>

#include <boring/ps2_mouse.h>

void ps2_mouse_decoder_init(struct ps2_mouse_decoder *decoder) {
    if (decoder != NULL) {
        decoder->bytes[0] = 0U;
        decoder->bytes[1] = 0U;
        decoder->bytes[2] = 0U;
        decoder->index = 0U;
        decoder->buttons = 0U;
    }
}

bool ps2_mouse_decoder_feed(struct ps2_mouse_decoder *decoder,
                            uint8_t byte,
                            struct ps2_mouse_packet *packet) {
    uint8_t first;
    uint8_t buttons;
    int32_t native_y;

    if ((decoder == NULL) || (packet == NULL)) {
        return false;
    }
    if (decoder->index == 0U) {
        if ((byte & 0x08U) == 0U) {
            return false;
        }
        decoder->bytes[0] = byte;
        decoder->index = 1U;
        return false;
    }
    decoder->bytes[decoder->index] = byte;
    ++decoder->index;
    if (decoder->index != 3U) {
        return false;
    }
    decoder->index = 0U;

    first = decoder->bytes[0];
    buttons = (uint8_t)(first & (uint8_t)PS2_MOUSE_BUTTON_MASK);
    packet->buttons = buttons;
    packet->changed_buttons = (uint8_t)(buttons ^ decoder->buttons);
    decoder->buttons = buttons;
    packet->overflow = (first & 0xc0U) != 0U;
    packet->movement_valid = !packet->overflow;
    packet->dx = 0;
    packet->dy = 0;
    if (packet->movement_valid) {
        packet->dx = (int32_t)decoder->bytes[1];
        if ((first & 0x10U) != 0U) {
            packet->dx -= 256;
        }
        native_y = (int32_t)decoder->bytes[2];
        if ((first & 0x20U) != 0U) {
            native_y -= 256;
        }
        packet->dy = -native_y;
    }
    return true;
}
