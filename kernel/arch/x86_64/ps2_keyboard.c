#include <stdbool.h>
#include <stdint.h>

#include <boring/input_abi.h>
#include <boring/ps2_keyboard.h>

static uint32_t ps2_keyboard_base_code(uint8_t scan) {
    switch (scan) {
        case 0x01U: return BORING_KEY_ESCAPE;
        case 0x02U: return BORING_KEY_1;
        case 0x03U: return BORING_KEY_2;
        case 0x04U: return BORING_KEY_3;
        case 0x05U: return BORING_KEY_4;
        case 0x06U: return BORING_KEY_5;
        case 0x07U: return BORING_KEY_6;
        case 0x08U: return BORING_KEY_7;
        case 0x09U: return BORING_KEY_8;
        case 0x0aU: return BORING_KEY_9;
        case 0x0bU: return BORING_KEY_0;
        case 0x0cU: return BORING_KEY_MINUS;
        case 0x0dU: return BORING_KEY_EQUAL;
        case 0x0eU: return BORING_KEY_BACKSPACE;
        case 0x0fU: return BORING_KEY_TAB;
        case 0x10U: return BORING_KEY_Q;
        case 0x11U: return BORING_KEY_W;
        case 0x12U: return BORING_KEY_E;
        case 0x13U: return BORING_KEY_R;
        case 0x14U: return BORING_KEY_T;
        case 0x15U: return BORING_KEY_Y;
        case 0x16U: return BORING_KEY_U;
        case 0x17U: return BORING_KEY_I;
        case 0x18U: return BORING_KEY_O;
        case 0x19U: return BORING_KEY_P;
        case 0x1aU: return BORING_KEY_LEFT_BRACKET;
        case 0x1bU: return BORING_KEY_RIGHT_BRACKET;
        case 0x1cU: return BORING_KEY_ENTER;
        case 0x1dU: return BORING_KEY_LEFT_CTRL;
        case 0x1eU: return BORING_KEY_A;
        case 0x1fU: return BORING_KEY_S;
        case 0x20U: return BORING_KEY_D;
        case 0x21U: return BORING_KEY_F;
        case 0x22U: return BORING_KEY_G;
        case 0x23U: return BORING_KEY_H;
        case 0x24U: return BORING_KEY_J;
        case 0x25U: return BORING_KEY_K;
        case 0x26U: return BORING_KEY_L;
        case 0x27U: return BORING_KEY_SEMICOLON;
        case 0x28U: return BORING_KEY_APOSTROPHE;
        case 0x29U: return BORING_KEY_GRAVE;
        case 0x2aU: return BORING_KEY_LEFT_SHIFT;
        case 0x2bU: return BORING_KEY_BACKSLASH;
        case 0x2cU: return BORING_KEY_Z;
        case 0x2dU: return BORING_KEY_X;
        case 0x2eU: return BORING_KEY_C;
        case 0x2fU: return BORING_KEY_V;
        case 0x30U: return BORING_KEY_B;
        case 0x31U: return BORING_KEY_N;
        case 0x32U: return BORING_KEY_M;
        case 0x33U: return BORING_KEY_COMMA;
        case 0x34U: return BORING_KEY_DOT;
        case 0x35U: return BORING_KEY_SLASH;
        case 0x36U: return BORING_KEY_RIGHT_SHIFT;
        case 0x38U: return BORING_KEY_LEFT_ALT;
        case 0x39U: return BORING_KEY_SPACE;
        case 0x3bU: return BORING_KEY_F1;
        case 0x3cU: return BORING_KEY_F2;
        case 0x3dU: return BORING_KEY_F3;
        case 0x3eU: return BORING_KEY_F4;
        case 0x3fU: return BORING_KEY_F5;
        case 0x40U: return BORING_KEY_F6;
        case 0x41U: return BORING_KEY_F7;
        case 0x42U: return BORING_KEY_F8;
        case 0x43U: return BORING_KEY_F9;
        case 0x44U: return BORING_KEY_F10;
        case 0x57U: return BORING_KEY_F11;
        case 0x58U: return BORING_KEY_F12;
        default: return BORING_KEY_NONE;
    }
}

static uint32_t ps2_keyboard_extended_code(uint8_t scan) {
    switch (scan) {
        case 0x1cU: return BORING_KEY_ENTER;
        case 0x1dU: return BORING_KEY_RIGHT_CTRL;
        case 0x38U: return BORING_KEY_RIGHT_ALT;
        case 0x47U: return BORING_KEY_HOME;
        case 0x48U: return BORING_KEY_UP;
        case 0x49U: return BORING_KEY_PAGE_UP;
        case 0x4bU: return BORING_KEY_LEFT;
        case 0x4dU: return BORING_KEY_RIGHT;
        case 0x4fU: return BORING_KEY_END;
        case 0x50U: return BORING_KEY_DOWN;
        case 0x51U: return BORING_KEY_PAGE_DOWN;
        case 0x52U: return BORING_KEY_INSERT;
        case 0x53U: return BORING_KEY_DELETE;
        case 0x5bU: return BORING_KEY_LEFT_SUPER;
        case 0x5cU: return BORING_KEY_RIGHT_SUPER;
        default: return BORING_KEY_NONE;
    }
}

void ps2_keyboard_decoder_init(struct ps2_keyboard_decoder *decoder) {
    if (decoder != NULL) {
        decoder->extended = false;
        decoder->e1_remaining = 0U;
    }
}

bool ps2_keyboard_decoder_feed(struct ps2_keyboard_decoder *decoder,
                               uint8_t byte,
                               struct ps2_keyboard_transition *transition) {
    uint8_t scan;
    bool down;
    uint32_t code;

    if ((decoder == NULL) || (transition == NULL)) {
        return false;
    }
    if (decoder->e1_remaining != 0U) {
        --decoder->e1_remaining;
        decoder->extended = false;
        return false;
    }
    if (byte == 0xe1U) {
        decoder->e1_remaining = 5U;
        decoder->extended = false;
        return false;
    }
    if (byte == 0xe0U) {
        decoder->extended = true;
        return false;
    }

    down = (byte & 0x80U) == 0U;
    scan = (uint8_t)(byte & 0x7fU);
    code = decoder->extended ? ps2_keyboard_extended_code(scan) :
                               ps2_keyboard_base_code(scan);
    decoder->extended = false;
    if (code == (uint32_t)BORING_KEY_NONE) {
        return false;
    }
    transition->code = code;
    transition->down = down;
    return true;
}
