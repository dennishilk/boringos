#include "managed.h"
#include "wallpaper.h"

void display_managed_init(struct display_managed *state) {
    *state = (struct display_managed){0};
    state->background = 0x00282828U;
}

uint32_t display_control_validate(const struct display_control *r, size_t size) {
    bool geometry;
    if ((r == NULL) || (size != sizeof(*r)) ||
        (r->version != BORING_DISPLAY_CONTROL_VERSION)) { return BORING_DISPLAY_STATUS_INVALID; }
    geometry = (r->x != 0U) || (r->y != 0U) || (r->width != 0U) ||
        (r->height != 0U) || (r->border != 0U) || (r->color != 0U) || (r->order != 0U);
    switch (r->type) {
        case DISPLAY_INFO:
        case DISPLAY_MANAGER:
        case DISPLAY_PRESENT:
        case DISPLAY_PRESENT_FOCUS:
        case DISPLAY_INPUT_ACK:
            if ((r->surface != 0U) || (r->window != 0U) || geometry ||
                (r->owner_pid != 0ULL) || ((r->type != DISPLAY_PRESENT) &&
                (r->background != 0U))) { return BORING_DISPLAY_STATUS_INVALID; }
            break;
        case DISPLAY_DELEGATE:
            if ((r->surface == 0U) || (r->window != 0U) || geometry ||
                (r->background != 0U) || (r->owner_pid != 0ULL)) {
                return BORING_DISPLAY_STATUS_INVALID;
            }
            break;
        case DISPLAY_BIND:
        case DISPLAY_UNBIND:
            if ((r->surface == 0U) || (r->window == 0U) || geometry ||
                (r->background != 0U) ||
                ((r->type == DISPLAY_BIND) != (r->owner_pid != 0ULL))) {
                return BORING_DISPLAY_STATUS_INVALID;
            }
            break;
        case DISPLAY_PLACE:
            if ((r->surface == 0U) || (r->window == 0U) ||
                (r->owner_pid != 0ULL) || (r->background != 0U)) {
                return BORING_DISPLAY_STATUS_INVALID;
            }
            break;
        default: return BORING_DISPLAY_STATUS_INVALID;
    }
    return BORING_DISPLAY_STATUS_OK;
}

void display_managed_forget(struct display_managed *state, uint32_t surface) {
    uint32_t index;
    for (index = 0U; index < BORING_DISPLAY_SURFACE_MAX; ++index) {
        if (state->placements[index].surface == surface) {
            state->placements[index] = (struct display_placement){0};
        }
    }
}

uint32_t display_managed_control(struct display_managed *state,
                                 const struct boring_display_core *core,
                                 uint32_t endpoint, uint64_t peer_pid,
                                 const struct display_control *r) {
    uint32_t index;
    struct display_placement *p;
    const struct boring_display_surface_state *surface = NULL;
    if ((state == NULL) || (core == NULL) || (endpoint == 0U) ||
        (display_control_validate(r, sizeof(*r)) != BORING_DISPLAY_STATUS_OK)) {
        return BORING_DISPLAY_STATUS_INVALID;
    }
    if ((r->type != DISPLAY_DELEGATE) &&
        (endpoint != state->manager_endpoint)) { return BORING_DISPLAY_STATUS_ACCESS; }
    if (r->type == DISPLAY_PRESENT) {
        if ((r->background & 0xff000000U) != 0U) { return BORING_DISPLAY_STATUS_INVALID; }
        state->background = r->background;
        state->wallpaper = true;
        return BORING_DISPLAY_STATUS_OK;
    }
    if (r->type == DISPLAY_PRESENT_FOCUS) {
        return BORING_DISPLAY_STATUS_OK;
    }
    for (index = 0U; index < BORING_DISPLAY_SURFACE_MAX; ++index) {
        if (core->surfaces[index].active && (core->surfaces[index].token == r->surface)) {
            surface = &core->surfaces[index];
            break;
        }
    }
    if (surface == NULL) { return BORING_DISPLAY_STATUS_INVALID; }
    p = &state->placements[index];
    if (r->type == DISPLAY_DELEGATE) {
        if ((surface->owner_endpoint != endpoint) || (peer_pid == 0ULL)) {
            return BORING_DISPLAY_STATUS_ACCESS;
        }
        if (p->delegated || (p->window != 0U)) { return BORING_DISPLAY_STATUS_INVALID; }
        *p = (struct display_placement){0};
        p->surface = surface->token;
        p->owner_pid = peer_pid;
        p->delegated = true;
        return BORING_DISPLAY_STATUS_OK;
    }
    if (!p->delegated || (p->surface != r->surface)) { return BORING_DISPLAY_STATUS_ACCESS; }
    if (r->type == DISPLAY_BIND) {
        if ((p->owner_pid != r->owner_pid) || (p->window != 0U)) {
            return BORING_DISPLAY_STATUS_ACCESS;
        }
        p->window = r->window;
        return BORING_DISPLAY_STATUS_OK;
    }
    if (p->window != r->window) { return BORING_DISPLAY_STATUS_ACCESS; }
    if (r->type == DISPLAY_UNBIND) {
        p->window = 0U;
        p->visible = false;
        return BORING_DISPLAY_STATUS_OK;
    }
    if (r->type != DISPLAY_PLACE) { return BORING_DISPLAY_STATUS_INVALID; }
    if ((r->x > core->width) || (r->y > core->height) ||
        (r->width > core->width - r->x) || (r->height > core->height - r->y) ||
        (r->border > r->width / 2U) || (r->border > r->height / 2U) ||
        (r->width - 2U * r->border > surface->width) ||
        (r->height - 2U * r->border > surface->height) ||
        (r->order >= BORING_DISPLAY_SURFACE_MAX) || ((r->color & 0xff000000U) != 0U)) {
        return BORING_DISPLAY_STATUS_INVALID;
    }
    p->x = r->x; p->y = r->y; p->width = r->width; p->height = r->height;
    p->border = r->border; p->color = r->color; p->order = r->order;
    p->visible = (r->width != 0U) && (r->height != 0U);
    return BORING_DISPLAY_STATUS_OK;
}

static void put(uint8_t *output, size_t offset, uint32_t color) {
    output[offset] = (uint8_t)color;
    output[offset + 1U] = (uint8_t)(color >> 8U);
    output[offset + 2U] = (uint8_t)(color >> 16U);
    output[offset + 3U] = 0U;
}

bool display_managed_compose_scene(const struct display_managed *state,
                                   const struct boring_display_core *core,
                                   uint8_t *output, size_t size) {
    size_t offset;
    uint32_t order, index, row;
    if ((state == NULL) || (core == NULL) || (output == NULL) ||
        (size != core->byte_size)) { return false; }
    for (offset = 0U; offset < size; offset += 4U) { put(output, offset, state->background); }
    if (state->wallpaper) { (void)display_wallpaper_compose(core, output, size); }
    for (order = 0U; order < BORING_DISPLAY_SURFACE_MAX; ++order) {
        for (index = 0U; index < BORING_DISPLAY_SURFACE_MAX; ++index) {
            const struct display_placement *p = &state->placements[index];
            const struct boring_display_surface_state *s = &core->surfaces[index];
            if (!p->visible || (p->order != order) || !s->active ||
                (p->surface != s->token) || (s->pixels == NULL)) { continue; }
            for (row = 0U; row < p->height; ++row) {
                uint32_t column;
                for (column = 0U; column < p->width; ++column) {
                    uint32_t color = p->color;
                    offset = (size_t)(p->y + row) * core->stride + (size_t)(p->x + column) * 4U;
                    if ((row >= p->border) && (column >= p->border) &&
                        (row < p->height - p->border) && (column < p->width - p->border)) {
                        size_t source = (size_t)(row - p->border) * s->stride +
                            (size_t)(column - p->border) * 4U;
                        color = (uint32_t)s->pixels[source] |
                            ((uint32_t)s->pixels[source + 1U] << 8U) |
                            ((uint32_t)s->pixels[source + 2U] << 16U);
                    }
                    put(output, offset, color);
                }
            }
        }
    }
    return true;
}

static bool placement_ready(const struct display_placement *placement,
                            const struct boring_display_surface_state *surface,
                            const struct boring_display_core *core) {
    return placement->visible && surface->active &&
        (placement->surface == surface->token) && (surface->pixels != NULL) &&
        (placement->width != 0U) && (placement->height != 0U) &&
        (placement->x <= core->width) && (placement->y <= core->height) &&
        (placement->width <= core->width - placement->x) &&
        (placement->height <= core->height - placement->y) &&
        (placement->border <= placement->width / 2U) &&
        (placement->border <= placement->height / 2U);
}

static bool placements_overlap(const struct display_placement *first,
                               const struct display_placement *second) {
    return (first->x < second->x + second->width) &&
        (second->x < first->x + first->width) &&
        (first->y < second->y + second->height) &&
        (second->y < first->y + first->height);
}

static bool focus_region(struct boring_display_region *regions,
                         size_t region_capacity,
                         size_t *region_count,
                         uint64_t *pixel_count,
                         const struct boring_display_region *region,
                         const struct boring_display_core *core,
                         uint8_t *output,
                         uint32_t color) {
    uint32_t row;

    if ((region->width == 0U) || (region->height == 0U)) {
        return true;
    }
    if (*region_count >= region_capacity) {
        return false;
    }
    for (row = 0U; row < region->height; ++row) {
        uint32_t column;
        for (column = 0U; column < region->width; ++column) {
            const size_t offset =
                (size_t)(region->y + row) * (size_t)core->stride +
                (size_t)(region->x + column) * 4U;
            put(output, offset, color);
        }
    }
    regions[*region_count] = *region;
    ++*region_count;
    *pixel_count += (uint64_t)region->width * (uint64_t)region->height;
    return true;
}

bool display_managed_compose_focus_borders(
    const struct display_managed *state,
    const struct boring_display_core *core,
    uint8_t *output,
    size_t size,
    struct boring_display_region *regions,
    size_t region_capacity,
    size_t *region_count,
    uint64_t *pixel_count) {
    uint32_t first;

    if ((state == NULL) || (core == NULL) || (output == NULL) ||
        (regions == NULL) || (region_count == NULL) || (pixel_count == NULL) ||
        (size != core->byte_size) ||
        (region_capacity < BORING_DISPLAY_FOCUS_REGION_MAX)) {
        return false;
    }
    *region_count = 0U;
    *pixel_count = 0ULL;
    for (first = 0U; first < BORING_DISPLAY_SURFACE_MAX; ++first) {
        const struct display_placement *a = &state->placements[first];
        const struct boring_display_surface_state *surface =
            &core->surfaces[first];

        if (!a->visible) {
            continue;
        }
        if (!placement_ready(a, surface, core)) {
            return false;
        }
    }
    for (first = 0U; first < BORING_DISPLAY_SURFACE_MAX; ++first) {
        const struct display_placement *a = &state->placements[first];
        uint32_t second;

        if (!a->visible) {
            continue;
        }
        for (second = first + 1U;
             second < BORING_DISPLAY_SURFACE_MAX;
             ++second) {
            const struct display_placement *b = &state->placements[second];
            if (b->visible && placements_overlap(a, b)) {
                return false;
            }
        }
    }
    for (first = 0U; first < BORING_DISPLAY_SURFACE_MAX; ++first) {
        const struct display_placement *p = &state->placements[first];
        struct boring_display_region region;
        uint32_t middle_height;

        if (!p->visible || (p->border == 0U)) {
            continue;
        }
        middle_height = p->height - 2U * p->border;
        region = (struct boring_display_region){
            p->x, p->y, p->width, p->border
        };
        if (!focus_region(regions, region_capacity, region_count, pixel_count,
                          &region, core, output, p->color)) {
            return false;
        }
        region.y = p->y + p->height - p->border;
        if (!focus_region(regions, region_capacity, region_count, pixel_count,
                          &region, core, output, p->color)) {
            return false;
        }
        region = (struct boring_display_region){
            p->x, p->y + p->border, p->border, middle_height
        };
        if (!focus_region(regions, region_capacity, region_count, pixel_count,
                          &region, core, output, p->color)) {
            return false;
        }
        region.x = p->x + p->width - p->border;
        if (!focus_region(regions, region_capacity, region_count, pixel_count,
                          &region, core, output, p->color)) {
            return false;
        }
    }
    return true;
}

bool display_managed_compose(const struct display_managed *state,
                             const struct boring_display_core *core,
                             uint8_t *output, size_t size) {
    if (!display_managed_compose_scene(state, core, output, size)) {
        return false;
    }
    boring_display_compose_cursor(core, output);
    return true;
}
