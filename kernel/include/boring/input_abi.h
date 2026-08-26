#ifndef BORING_INPUT_ABI_H
#define BORING_INPUT_ABI_H

#include <stddef.h>
#include <stdint.h>

#define BORING_INPUT_QUEUE_CAPACITY 128U
#define BORING_INPUT_READ_MAX 16U

#define BORING_INPUT_EVENT_KEY 1U
#define BORING_INPUT_EVENT_MOUSE_MOVE 2U
#define BORING_INPUT_EVENT_MOUSE_BUTTON 3U

#define BORING_INPUT_FLAG_REPEAT (1U << 0)

#define BORING_MOD_SHIFT (1U << 0)
#define BORING_MOD_CTRL (1U << 1)
#define BORING_MOD_ALT (1U << 2)
#define BORING_MOD_SUPER (1U << 3)
#define BORING_MOD_MASK (BORING_MOD_SHIFT | BORING_MOD_CTRL | \
                         BORING_MOD_ALT | BORING_MOD_SUPER)

#define BORING_MOUSE_BUTTON_LEFT 1U
#define BORING_MOUSE_BUTTON_MIDDLE 2U
#define BORING_MOUSE_BUTTON_RIGHT 3U

#define BORING_KEY_UP_VALUE 0
#define BORING_KEY_DOWN_VALUE 1

enum boring_keycode {
    BORING_KEY_NONE = 0,
    BORING_KEY_A = 1,
    BORING_KEY_B = 2,
    BORING_KEY_C = 3,
    BORING_KEY_D = 4,
    BORING_KEY_E = 5,
    BORING_KEY_F = 6,
    BORING_KEY_G = 7,
    BORING_KEY_H = 8,
    BORING_KEY_I = 9,
    BORING_KEY_J = 10,
    BORING_KEY_K = 11,
    BORING_KEY_L = 12,
    BORING_KEY_M = 13,
    BORING_KEY_N = 14,
    BORING_KEY_O = 15,
    BORING_KEY_P = 16,
    BORING_KEY_Q = 17,
    BORING_KEY_R = 18,
    BORING_KEY_S = 19,
    BORING_KEY_T = 20,
    BORING_KEY_U = 21,
    BORING_KEY_V = 22,
    BORING_KEY_W = 23,
    BORING_KEY_X = 24,
    BORING_KEY_Y = 25,
    BORING_KEY_Z = 26,
    BORING_KEY_0 = 27,
    BORING_KEY_1 = 28,
    BORING_KEY_2 = 29,
    BORING_KEY_3 = 30,
    BORING_KEY_4 = 31,
    BORING_KEY_5 = 32,
    BORING_KEY_6 = 33,
    BORING_KEY_7 = 34,
    BORING_KEY_8 = 35,
    BORING_KEY_9 = 36,
    BORING_KEY_ESCAPE = 37,
    BORING_KEY_TAB = 38,
    BORING_KEY_ENTER = 39,
    BORING_KEY_BACKSPACE = 40,
    BORING_KEY_SPACE = 41,
    BORING_KEY_MINUS = 42,
    BORING_KEY_EQUAL = 43,
    BORING_KEY_LEFT_BRACKET = 44,
    BORING_KEY_RIGHT_BRACKET = 45,
    BORING_KEY_BACKSLASH = 46,
    BORING_KEY_SEMICOLON = 47,
    BORING_KEY_APOSTROPHE = 48,
    BORING_KEY_GRAVE = 49,
    BORING_KEY_COMMA = 50,
    BORING_KEY_DOT = 51,
    BORING_KEY_SLASH = 52,
    BORING_KEY_INSERT = 53,
    BORING_KEY_DELETE = 54,
    BORING_KEY_HOME = 55,
    BORING_KEY_END = 56,
    BORING_KEY_PAGE_UP = 57,
    BORING_KEY_PAGE_DOWN = 58,
    BORING_KEY_LEFT = 59,
    BORING_KEY_RIGHT = 60,
    BORING_KEY_UP = 61,
    BORING_KEY_DOWN = 62,
    BORING_KEY_F1 = 63,
    BORING_KEY_F2 = 64,
    BORING_KEY_F3 = 65,
    BORING_KEY_F4 = 66,
    BORING_KEY_F5 = 67,
    BORING_KEY_F6 = 68,
    BORING_KEY_F7 = 69,
    BORING_KEY_F8 = 70,
    BORING_KEY_F9 = 71,
    BORING_KEY_F10 = 72,
    BORING_KEY_F11 = 73,
    BORING_KEY_F12 = 74,
    BORING_KEY_LEFT_SHIFT = 75,
    BORING_KEY_RIGHT_SHIFT = 76,
    BORING_KEY_LEFT_CTRL = 77,
    BORING_KEY_RIGHT_CTRL = 78,
    BORING_KEY_LEFT_ALT = 79,
    BORING_KEY_RIGHT_ALT = 80,
    BORING_KEY_LEFT_SUPER = 81,
    BORING_KEY_RIGHT_SUPER = 82,
    BORING_KEY_MAX = BORING_KEY_RIGHT_SUPER
};

struct boring_input_event {
    uint32_t type;
    uint32_t code;
    int32_t value1;
    int32_t value2;
    uint32_t modifiers;
    uint32_t flags;
};

_Static_assert(BORING_INPUT_QUEUE_CAPACITY == 128U,
               "M31 input queue capacity contract changed");
_Static_assert(BORING_INPUT_READ_MAX == 16U,
               "M31 input read bound changed");
_Static_assert(BORING_KEY_Q == 17,
               "BoringOS Q keycode contract changed");
_Static_assert(BORING_KEY_ENTER == 39,
               "BoringOS Enter keycode contract changed");
_Static_assert(BORING_KEY_LEFT_SUPER == 81,
               "BoringOS Left Super keycode contract changed");
_Static_assert(sizeof(struct boring_input_event) == 24U,
               "BoringOS input event ABI must remain 24 bytes");
_Static_assert(offsetof(struct boring_input_event, type) == 0U,
               "input event type offset changed");
_Static_assert(offsetof(struct boring_input_event, code) == 4U,
               "input event code offset changed");
_Static_assert(offsetof(struct boring_input_event, value1) == 8U,
               "input event value1 offset changed");
_Static_assert(offsetof(struct boring_input_event, value2) == 12U,
               "input event value2 offset changed");
_Static_assert(offsetof(struct boring_input_event, modifiers) == 16U,
               "input event modifiers offset changed");
_Static_assert(offsetof(struct boring_input_event, flags) == 20U,
               "input event flags offset changed");

#endif
