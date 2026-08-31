#ifndef BORING_DISPLAY_MANAGED_H
#define BORING_DISPLAY_MANAGED_H
#include "core.h"
#include <boring/display_control.h>

struct display_placement {
    uint32_t surface, window, x, y, width, height, border, color, order;
    uint64_t owner_pid;
    bool delegated, visible;
};
struct display_managed {
    uint32_t manager_endpoint, background;
    bool wallpaper;
    struct display_placement placements[BORING_DISPLAY_SURFACE_MAX];
};
void display_managed_init(struct display_managed *state);
uint32_t display_control_validate(const struct display_control *request, size_t size);
uint32_t display_managed_control(struct display_managed *state,
                                 const struct boring_display_core *core,
                                 uint32_t endpoint, uint64_t peer_pid,
                                 const struct display_control *request);
void display_managed_forget(struct display_managed *state, uint32_t surface);
bool display_managed_compose(const struct display_managed *state,
                             const struct boring_display_core *core,
                             uint8_t *output, size_t size);
#endif
