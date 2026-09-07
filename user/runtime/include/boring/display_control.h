#ifndef BORING_DISPLAY_CONTROL_H
#define BORING_DISPLAY_CONTROL_H
#include <boring/display_abi.h>
#include <boring/input_abi.h>

/* Additive v2 control envelopes on boring.display; v1 remains unchanged. */
#define BORING_DISPLAY_CONTROL_VERSION 2U
#define DISPLAY_INFO 10U
#define DISPLAY_MANAGER 11U
#define DISPLAY_DELEGATE 12U
#define DISPLAY_BIND 13U
#define DISPLAY_PLACE 14U
#define DISPLAY_UNBIND 15U
#define DISPLAY_PRESENT 16U
#define DISPLAY_INPUT 17U
#define DISPLAY_REPLY 18U
#define DISPLAY_INPUT_ACK 19U

struct display_control {
    uint32_t version, type, surface, window;
    uint32_t x, y, width, height, border, color, background, order;
    uint64_t owner_pid;
};
struct display_event {
    uint32_t version, type, status, surface;
    uint32_t width, height, cursor_x, cursor_y;
    struct boring_input_event input;
};
_Static_assert(sizeof(struct display_control) == 56U, "display control size");
_Static_assert(sizeof(struct display_event) == 56U, "display event size");
#endif
