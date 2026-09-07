#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <boring/input_abi.h>

#include "input.h"

size_t boring_terminal_key_bytes(uint32_t code, uint32_t modifiers,
                        char output[BORING_TERMINAL_KEY_BYTES_MAX]) {
    const bool shift = (modifiers & BORING_MOD_SHIFT) != 0U;
    if (output == NULL) { return 0U; }
    if ((modifiers & (BORING_MOD_SUPER | BORING_MOD_ALT)) != 0U) {
        return 0U;
    }
    if ((code >= BORING_KEY_A) && (code <= BORING_KEY_Z)) {
        char value = (char)('a' + (char)(code - BORING_KEY_A));
        if (shift) {
            value = (char)('A' + (char)(code - BORING_KEY_A));
        }
        if ((modifiers & BORING_MOD_CTRL) != 0U) {
            value = (char)(1U + (unsigned char)(code - BORING_KEY_A));
        }
        output[0] = value;
        return 1U;
    }
    if ((code >= BORING_KEY_0) && (code <= BORING_KEY_9)) {
        static const char shifted[10] = {')', '!', '@', '#', '$', '%', '^', '&', '*', '('};
        const uint32_t digit = code - BORING_KEY_0;
        output[0] = shift ? shifted[digit] : (char)('0' + (char)digit);
        return 1U;
    }
    switch (code) {
        case BORING_KEY_ESCAPE: output[0] = '\x1b'; return 1U;
        case BORING_KEY_TAB: output[0] = '\t'; return 1U;
        case BORING_KEY_ENTER: output[0] = '\r'; return 1U;
        case BORING_KEY_BACKSPACE: output[0] = '\x7f'; return 1U;
        case BORING_KEY_SPACE: output[0] = ' '; return 1U;
        case BORING_KEY_MINUS: output[0] = shift ? '_' : '-'; return 1U;
        case BORING_KEY_EQUAL: output[0] = shift ? '+' : '='; return 1U;
        case BORING_KEY_LEFT_BRACKET: output[0] = shift ? '{' : '['; return 1U;
        case BORING_KEY_RIGHT_BRACKET: output[0] = shift ? '}' : ']'; return 1U;
        case BORING_KEY_BACKSLASH: output[0] = shift ? '|' : '\\'; return 1U;
        case BORING_KEY_SEMICOLON: output[0] = shift ? ':' : ';'; return 1U;
        case BORING_KEY_APOSTROPHE: output[0] = shift ? '"' : '\''; return 1U;
        case BORING_KEY_GRAVE: output[0] = shift ? '~' : '`'; return 1U;
        case BORING_KEY_COMMA: output[0] = shift ? '<' : ','; return 1U;
        case BORING_KEY_DOT: output[0] = shift ? '>' : '.'; return 1U;
        case BORING_KEY_SLASH: output[0] = shift ? '?' : '/'; return 1U;
        case BORING_KEY_LEFT: output[0] = '\x1b'; output[1] = '['; output[2] = 'D'; return 3U;
        case BORING_KEY_RIGHT: output[0] = '\x1b'; output[1] = '['; output[2] = 'C'; return 3U;
        case BORING_KEY_UP: output[0] = '\x1b'; output[1] = '['; output[2] = 'A'; return 3U;
        case BORING_KEY_DOWN: output[0] = '\x1b'; output[1] = '['; output[2] = 'B'; return 3U;
        case BORING_KEY_HOME: output[0] = '\x1b'; output[1] = '['; output[2] = 'H'; return 3U;
        case BORING_KEY_END: output[0] = '\x1b'; output[1] = '['; output[2] = 'F'; return 3U;
        case BORING_KEY_DELETE: output[0] = '\x1b'; output[1] = '['; output[2] = '3'; output[3] = '~'; return 4U;
        default: return 0U;
    }
}
