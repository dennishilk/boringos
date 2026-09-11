#ifndef BORING_DISPLAY_MANAGED_H
#define BORING_DISPLAY_MANAGED_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <boring/display_control.h>
#include "core.h"

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

#define BORING_DISPLAY_FOCUS_REGION_MAX (BORING_DISPLAY_SURFACE_MAX * 4U)
#define BORING_DISPLAY_LAYOUT_REGION_MAX (BORING_DISPLAY_SURFACE_MAX * 2U)

struct display_layout_change {
    struct boring_display_region old_region;
    bool changed;
    bool old_visible;
};

struct display_layout_damage {
    struct display_layout_change changes[BORING_DISPLAY_SURFACE_MAX];
    bool full;
};

void display_managed_init(struct display_managed *state);
void display_layout_damage_init(struct display_layout_damage *damage);
uint32_t display_control_validate(const struct display_control *request, size_t size);
uint32_t display_managed_control(struct display_managed *state,
                                 const struct boring_display_core *core,
                                 uint32_t endpoint, uint64_t peer_pid,
                                 const struct display_control *request);
uint32_t display_managed_layout_control(
    struct display_managed *state,
    struct display_layout_damage *damage,
    const struct boring_display_core *core,
    uint32_t endpoint,
    uint64_t peer_pid,
    const struct display_control *request);
bool display_managed_layout_regions(
    const struct display_managed *state,
    const struct display_layout_damage *damage,
    const struct boring_display_core *core,
    struct boring_display_region *regions,
    size_t region_capacity,
    size_t *region_count);
void display_managed_layout_complete(struct display_layout_damage *damage);
void display_managed_forget(struct display_managed *state, uint32_t surface);
bool display_managed_compose_scene(const struct display_managed *state,
                                   const struct boring_display_core *core,
                                   uint8_t *output, size_t size);
bool display_managed_compose_scene_region(
    const struct display_managed *state,
    const struct boring_display_core *core,
    uint8_t *output, size_t size,
    const struct boring_display_region *region,
    uint64_t *pixel_count);
bool display_managed_damage_region(
    const struct display_managed *state,
    const struct boring_display_core *core,
    uint32_t surface,
    const struct boring_display_region *client_damage,
    struct boring_display_region *screen_damage);
bool display_managed_compose_focus_borders(
    const struct display_managed *state,
    const struct boring_display_core *core,
    uint8_t *output,
    size_t size,
    struct boring_display_region *regions,
    size_t region_capacity,
    size_t *region_count,
    uint64_t *pixel_count);
bool display_managed_compose(const struct display_managed *state,
                             const struct boring_display_core *core,
                             uint8_t *output, size_t size);
#endif
