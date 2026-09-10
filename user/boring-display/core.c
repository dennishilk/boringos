#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core.h"

#define SURFACE_SLOT_BITS 8U
#define SURFACE_SLOT_MASK 0xffU
#define SURFACE_GENERATION_MAX 0x00ffffffU

static uint32_t next_generation(uint32_t generation) {
    if ((generation == 0U) || (generation >= SURFACE_GENERATION_MAX)) {
        return 1U;
    }
    return generation + 1U;
}

static uint32_t token_encode(size_t slot, uint32_t generation) {
    if ((slot >= (size_t)BORING_DISPLAY_SURFACE_MAX) || (generation == 0U)) {
        return BORING_DISPLAY_SURFACE_INVALID;
    }
    return (generation << SURFACE_SLOT_BITS) | (uint32_t)(slot + 1U);
}

static const struct boring_display_surface_state *surface_lookup(
    const struct boring_display_core *core,
    uint32_t token) {
    uint32_t low;
    uint32_t generation;
    size_t slot;

    if ((core == NULL) || (token == BORING_DISPLAY_SURFACE_INVALID)) {
        return NULL;
    }
    low = token & SURFACE_SLOT_MASK;
    generation = token >> SURFACE_SLOT_BITS;
    if ((low == 0U) || (low > BORING_DISPLAY_SURFACE_MAX) ||
        (generation == 0U)) {
        return NULL;
    }
    slot = (size_t)(low - 1U);
    if (!core->surfaces[slot].active ||
        (core->surfaces[slot].generation != generation) ||
        (core->surfaces[slot].token != token)) {
        return NULL;
    }
    return &core->surfaces[slot];
}

static struct boring_display_surface_state *surface_lookup_mutable(
    struct boring_display_core *core,
    uint32_t token) {
    return (struct boring_display_surface_state *)(uintptr_t)
        surface_lookup(core, token);
}

bool boring_display_core_init(struct boring_display_core *core,
                              const struct boring_display_scanout_info *info) {
    size_t index;

    if ((core == NULL) || (info == NULL) ||
        (info->version != BORING_DISPLAY_SCANOUT_VERSION) ||
        (info->width == 0U) || (info->height == 0U) ||
        (info->width > BORING_DISPLAY_MAX_WIDTH) ||
        (info->height > BORING_DISPLAY_MAX_HEIGHT) ||
        (info->width > UINT32_MAX / BORING_DISPLAY_BYTES_PER_PIXEL) ||
        (info->stride != info->width * BORING_DISPLAY_BYTES_PER_PIXEL) ||
        ((uint64_t)info->stride * (uint64_t)info->height != info->byte_size) ||
        (info->byte_size == 0ULL) ||
        (info->byte_size > BORING_DISPLAY_MAX_SCANOUT_BYTES)) {
        return false;
    }
    core->width = info->width;
    core->height = info->height;
    core->stride = info->stride;
    core->byte_size = info->byte_size;
    core->cursor_x = info->width / 2U;
    core->cursor_y = info->height / 2U;
    core->live_surfaces = 0U;
    core->next_creation_order = 1ULL;
    for (index = 0U; index < (size_t)BORING_DISPLAY_SURFACE_MAX; ++index) {
        core->surfaces[index].owner_endpoint = 0U;
        core->surfaces[index].token = 0U;
        core->surfaces[index].width = 0U;
        core->surfaces[index].height = 0U;
        core->surfaces[index].stride = 0U;
        core->surfaces[index].buffer_handle = 0U;
        core->surfaces[index].pixels = NULL;
        core->surfaces[index].generation = 1U;
        core->surfaces[index].creation_order = 0ULL;
        core->surfaces[index].active = false;
    }
    return true;
}

uint32_t boring_display_validate_create(
    const struct boring_display_core *core,
    const struct boring_display_request *request,
    uint64_t actual_buffer_size) {
    uint64_t row_bytes;
    uint64_t required;

    if ((core == NULL) || (request == NULL) ||
        (request->version != BORING_DISPLAY_PROTOCOL_VERSION) ||
        (request->type != BORING_DISPLAY_REQUEST_CREATE) ||
        (request->surface_token != BORING_DISPLAY_SURFACE_INVALID) ||
        (request->reserved != 0U) ||
        (request->pixel_format != BORING_DISPLAY_PIXEL_FORMAT_XRGB8888) ||
        (request->width == 0U) || (request->height == 0U) ||
        (request->width > core->width) || (request->height > core->height)) {
        return BORING_DISPLAY_STATUS_INVALID;
    }
    row_bytes = (uint64_t)request->width *
                (uint64_t)BORING_DISPLAY_BYTES_PER_PIXEL;
    if ((row_bytes > (uint64_t)UINT32_MAX) ||
        (request->stride != (uint32_t)row_bytes) ||
        ((uint64_t)request->height > UINT64_MAX / row_bytes)) {
        return BORING_DISPLAY_STATUS_INVALID;
    }
    required = row_bytes * (uint64_t)request->height;
    if ((required == 0ULL) || (request->byte_size != required) ||
        (actual_buffer_size != required) ||
        (required > (uint64_t)BORING_BUFFER_MAX_BYTES)) {
        return BORING_DISPLAY_STATUS_INVALID;
    }
    return BORING_DISPLAY_STATUS_OK;
}

uint32_t boring_display_surface_add(struct boring_display_core *core,
                                    uint32_t owner_endpoint,
                                    const struct boring_display_request *request,
                                    uint32_t buffer_handle,
                                    uint8_t *pixels,
                                    uint32_t *token_out) {
    size_t index;
    uint32_t token;

    if ((core == NULL) || (request == NULL) || (owner_endpoint == 0U) ||
        (buffer_handle == 0U) || (pixels == NULL) || (token_out == NULL)) {
        return BORING_DISPLAY_STATUS_INVALID;
    }
    if (core->live_surfaces >= BORING_DISPLAY_SURFACE_MAX) {
        return BORING_DISPLAY_STATUS_NO_SPACE;
    }
    if (core->next_creation_order == 0ULL) {
        return BORING_DISPLAY_STATUS_INTERNAL;
    }
    for (index = 0U; index < (size_t)BORING_DISPLAY_SURFACE_MAX; ++index) {
        struct boring_display_surface_state *surface = &core->surfaces[index];
        if (surface->active) {
            continue;
        }
        token = token_encode(index, surface->generation);
        if (token == BORING_DISPLAY_SURFACE_INVALID) {
            return BORING_DISPLAY_STATUS_INTERNAL;
        }
        surface->owner_endpoint = owner_endpoint;
        surface->token = token;
        surface->width = request->width;
        surface->height = request->height;
        surface->stride = request->stride;
        surface->buffer_handle = buffer_handle;
        surface->pixels = pixels;
        surface->creation_order = core->next_creation_order;
        core->next_creation_order =
            (core->next_creation_order == UINT64_MAX) ?
            0ULL : core->next_creation_order + 1ULL;
        surface->active = true;
        ++core->live_surfaces;
        *token_out = token;
        return BORING_DISPLAY_STATUS_OK;
    }
    return BORING_DISPLAY_STATUS_NO_SPACE;
}

uint32_t boring_display_surface_commit(const struct boring_display_core *core,
                                       uint32_t owner_endpoint,
                                       uint32_t token) {
    const struct boring_display_surface_state *surface = surface_lookup(core, token);

    if (surface == NULL) {
        return BORING_DISPLAY_STATUS_INVALID;
    }
    return (surface->owner_endpoint == owner_endpoint) ?
        BORING_DISPLAY_STATUS_OK : BORING_DISPLAY_STATUS_ACCESS;
}

uint32_t boring_display_surface_destroy(struct boring_display_core *core,
                                        uint32_t owner_endpoint,
                                        uint32_t token,
                                        uint32_t *buffer_handle_out,
                                        uint8_t **pixels_out) {
    struct boring_display_surface_state *surface =
        surface_lookup_mutable(core, token);

    if ((surface == NULL) || (buffer_handle_out == NULL) ||
        (pixels_out == NULL)) {
        return BORING_DISPLAY_STATUS_INVALID;
    }
    if (surface->owner_endpoint != owner_endpoint) {
        return BORING_DISPLAY_STATUS_ACCESS;
    }
    *buffer_handle_out = surface->buffer_handle;
    *pixels_out = surface->pixels;
    surface->owner_endpoint = 0U;
    surface->token = 0U;
    surface->width = 0U;
    surface->height = 0U;
    surface->stride = 0U;
    surface->buffer_handle = 0U;
    surface->pixels = NULL;
    surface->creation_order = 0ULL;
    surface->active = false;
    surface->generation = next_generation(surface->generation);
    if (core->live_surfaces != 0U) {
        --core->live_surfaces;
    }
    return BORING_DISPLAY_STATUS_OK;
}

size_t boring_display_peer_cleanup(struct boring_display_core *core,
                                   uint32_t owner_endpoint,
                                   uint32_t handles[BORING_DISPLAY_SURFACE_MAX],
                                   uint8_t *pixels[BORING_DISPLAY_SURFACE_MAX]) {
    size_t index;
    size_t count = 0U;

    if ((core == NULL) || (owner_endpoint == 0U) ||
        (handles == NULL) || (pixels == NULL)) {
        return 0U;
    }
    for (index = 0U; index < (size_t)BORING_DISPLAY_SURFACE_MAX; ++index) {
        struct boring_display_surface_state *surface = &core->surfaces[index];
        if (!surface->active || (surface->owner_endpoint != owner_endpoint)) {
            continue;
        }
        handles[count] = surface->buffer_handle;
        pixels[count] = surface->pixels;
        ++count;
        surface->owner_endpoint = 0U;
        surface->token = 0U;
        surface->width = 0U;
        surface->height = 0U;
        surface->stride = 0U;
        surface->buffer_handle = 0U;
        surface->pixels = NULL;
        surface->creation_order = 0ULL;
        surface->active = false;
        surface->generation = next_generation(surface->generation);
        if (core->live_surfaces != 0U) {
            --core->live_surfaces;
        }
    }
    return count;
}

static uint32_t clamp_axis(uint32_t current, int32_t delta, uint32_t limit) {
    int64_t next;

    if (limit == 0U) {
        return 0U;
    }
    next = (int64_t)current + (int64_t)delta;
    if (next < 0LL) {
        return 0U;
    }
    if ((uint64_t)next >= (uint64_t)limit) {
        return limit - 1U;
    }
    return (uint32_t)next;
}

void boring_display_cursor_move(struct boring_display_core *core,
                                int32_t dx,
                                int32_t dy) {
    if (core == NULL) {
        return;
    }
    core->cursor_x = clamp_axis(core->cursor_x, dx, core->width);
    core->cursor_y = clamp_axis(core->cursor_y, dy, core->height);
}

static void output_pixel(uint8_t *output,
                         uint32_t stride,
                         uint32_t x,
                         uint32_t y,
                         uint8_t red,
                         uint8_t green,
                         uint8_t blue) {
    const size_t offset = (size_t)y * (size_t)stride +
                          (size_t)x * (size_t)BORING_DISPLAY_BYTES_PER_PIXEL;
    output[offset] = blue;
    output[offset + 1U] = green;
    output[offset + 2U] = red;
    output[offset + 3U] = 0U;
}

static void compose_surface(const struct boring_display_core *core,
                            const struct boring_display_surface_state *surface,
                            size_t slot,
                            uint8_t *output) {
    uint32_t origin_x = 80U + (uint32_t)slot * 100U;
    uint32_t origin_y = 80U + (uint32_t)slot * 60U;
    uint32_t y;

    if ((surface == NULL) || !surface->active || (surface->pixels == NULL)) {
        return;
    }
    if (origin_x >= core->width) {
        origin_x = core->width - 1U;
    }
    if (origin_y >= core->height) {
        origin_y = core->height - 1U;
    }
    for (y = 0U; y < surface->height && origin_y + y < core->height; ++y) {
        uint32_t x;
        for (x = 0U; x < surface->width && origin_x + x < core->width; ++x) {
            const size_t source = (size_t)y * (size_t)surface->stride +
                                  (size_t)x * 4U;
            const size_t destination =
                (size_t)(origin_y + y) * (size_t)core->stride +
                (size_t)(origin_x + x) * 4U;
            output[destination] = surface->pixels[source];
            output[destination + 1U] = surface->pixels[source + 1U];
            output[destination + 2U] = surface->pixels[source + 2U];
            output[destination + 3U] = 0U;
        }
    }
}

static bool compose_surfaces(const struct boring_display_core *core,
                             uint8_t *output) {
    uint64_t previous_order = 0ULL;
    size_t rendered;

    for (rendered = 0U; rendered < (size_t)core->live_surfaces; ++rendered) {
        uint64_t best_order = 0ULL;
        size_t best_index = 0U;
        bool found = false;
        size_t index;

        for (index = 0U; index < (size_t)BORING_DISPLAY_SURFACE_MAX; ++index) {
            const struct boring_display_surface_state *surface =
                &core->surfaces[index];

            if (!surface->active || (surface->creation_order == 0ULL) ||
                (surface->creation_order <= previous_order)) {
                continue;
            }
            if (!found || (surface->creation_order < best_order)) {
                best_order = surface->creation_order;
                best_index = index;
                found = true;
            }
        }
        if (!found) {
            return false;
        }
        compose_surface(core, &core->surfaces[best_index], best_index, output);
        previous_order = best_order;
    }
    return true;
}

void boring_display_compose_cursor(const struct boring_display_core *core, uint8_t *output) {
    uint32_t row;

    for (row = 0U; row < 12U; ++row) {
        uint32_t column;
        for (column = 0U; column <= row / 2U; ++column) {
            const uint32_t x = core->cursor_x + column;
            const uint32_t y = core->cursor_y + row;
            if ((x >= core->width) || (y >= core->height)) {
                continue;
            }
            output_pixel(output, core->stride, x, y,
                         (column == 0U) ? 255U : 48U,
                         238U, 245U);
        }
    }
}

static struct boring_display_region cursor_region(
    const struct boring_display_core *core) {
    struct boring_display_region region = {0U, 0U, 0U, 0U};

    if ((core == NULL) || (core->cursor_x >= core->width) ||
        (core->cursor_y >= core->height)) {
        return region;
    }
    region.x = core->cursor_x;
    region.y = core->cursor_y;
    region.width = core->width - region.x;
    if (region.width > BORING_DISPLAY_CURSOR_WIDTH) {
        region.width = BORING_DISPLAY_CURSOR_WIDTH;
    }
    region.height = core->height - region.y;
    if (region.height > BORING_DISPLAY_CURSOR_HEIGHT) {
        region.height = BORING_DISPLAY_CURSOR_HEIGHT;
    }
    return region;
}

static bool cursor_damage_arguments_valid(
    const struct boring_display_cursor_damage *damage,
    const struct boring_display_core *core,
    const uint8_t *output,
    size_t output_size) {
    return (damage != NULL) && (core != NULL) && (output != NULL) &&
           (core->byte_size <= (uint64_t)SIZE_MAX) &&
           (output_size == (size_t)core->byte_size) &&
           (core->width != 0U) && (core->height != 0U) &&
           ((uint64_t)core->stride ==
            (uint64_t)core->width * BORING_DISPLAY_BYTES_PER_PIXEL) &&
           (core->byte_size ==
            (uint64_t)core->stride * (uint64_t)core->height);
}

static void cursor_underlay_copy(
    struct boring_display_cursor_damage *damage,
    const struct boring_display_core *core,
    uint8_t *output,
    const struct boring_display_region *region,
    bool restore) {
    uint32_t row;

    for (row = 0U; row < region->height; ++row) {
        uint32_t column;
        for (column = 0U; column < region->width; ++column) {
            const size_t frame_offset =
                (size_t)(region->y + row) * (size_t)core->stride +
                (size_t)(region->x + column) *
                    (size_t)BORING_DISPLAY_BYTES_PER_PIXEL;
            const size_t saved_offset =
                ((size_t)row * (size_t)BORING_DISPLAY_CURSOR_WIDTH +
                 (size_t)column) *
                (size_t)BORING_DISPLAY_BYTES_PER_PIXEL;
            size_t byte;

            for (byte = 0U;
                 byte < (size_t)BORING_DISPLAY_BYTES_PER_PIXEL;
                 ++byte) {
                if (restore) {
                    output[frame_offset + byte] =
                        damage->saved[saved_offset + byte];
                } else {
                    damage->saved[saved_offset + byte] =
                        output[frame_offset + byte];
                }
            }
        }
    }
}

static uint64_t region_pixels(const struct boring_display_region *region) {
    return (uint64_t)region->width * (uint64_t)region->height;
}

static void counter_add(uint64_t *counter, uint64_t value) {
    if (value > UINT64_MAX - *counter) {
        *counter = UINT64_MAX;
    } else {
        *counter += value;
    }
}

static uint64_t cursor_drawn_pixels(
    const struct boring_display_core *core) {
    uint64_t count = 0ULL;
    uint32_t row;

    for (row = 0U; row < BORING_DISPLAY_CURSOR_HEIGHT; ++row) {
        uint32_t column;
        for (column = 0U; column <= row / 2U; ++column) {
            if ((core->cursor_x + column < core->width) &&
                (core->cursor_y + row < core->height)) {
                ++count;
            }
        }
    }
    return count;
}

void boring_display_cursor_damage_init(
    struct boring_display_cursor_damage *damage) {
    size_t index;

    if (damage == NULL) {
        return;
    }
    for (index = 0U; index < sizeof(damage->saved); ++index) {
        damage->saved[index] = 0U;
    }
    damage->saved_region = (struct boring_display_region){0U, 0U, 0U, 0U};
    damage->moves = 0ULL;
    damage->restored_pixels = 0ULL;
    damage->saved_pixels = 0ULL;
    damage->drawn_pixels = 0ULL;
    damage->ready = false;
}

bool boring_display_cursor_damage_reset(
    struct boring_display_cursor_damage *damage,
    const struct boring_display_core *core,
    uint8_t *output,
    size_t output_size) {
    struct boring_display_region region;

    if (!cursor_damage_arguments_valid(damage, core, output, output_size)) {
        return false;
    }
    region = cursor_region(core);
    if ((region.width == 0U) || (region.height == 0U)) {
        return false;
    }
    cursor_underlay_copy(damage, core, output, &region, false);
    damage->saved_region = region;
    counter_add(&damage->saved_pixels, region_pixels(&region));
    counter_add(&damage->drawn_pixels, cursor_drawn_pixels(core));
    damage->ready = true;
    boring_display_compose_cursor(core, output);
    return true;
}

bool boring_display_cursor_damage_restore(
    struct boring_display_cursor_damage *damage,
    const struct boring_display_core *core,
    uint8_t *output,
    size_t output_size) {
    struct boring_display_region expected;

    if (!cursor_damage_arguments_valid(damage, core, output, output_size) ||
        !damage->ready) {
        return false;
    }
    expected = cursor_region(core);
    if ((damage->saved_region.x != expected.x) ||
        (damage->saved_region.y != expected.y) ||
        (damage->saved_region.width != expected.width) ||
        (damage->saved_region.height != expected.height)) {
        return false;
    }
    cursor_underlay_copy(damage, core, output, &expected, true);
    counter_add(&damage->restored_pixels, region_pixels(&expected));
    damage->ready = false;
    return true;
}

bool boring_display_cursor_damage_move(
    struct boring_display_cursor_damage *damage,
    struct boring_display_core *core,
    uint8_t *output,
    size_t output_size,
    int32_t dx,
    int32_t dy,
    struct boring_display_region *old_region,
    struct boring_display_region *new_region) {
    struct boring_display_region expected;
    const uint32_t old_x = (core != NULL) ? core->cursor_x : 0U;
    const uint32_t old_y = (core != NULL) ? core->cursor_y : 0U;

    if ((old_region == NULL) || (new_region == NULL)) {
        return false;
    }
    *old_region = (struct boring_display_region){0U, 0U, 0U, 0U};
    *new_region = (struct boring_display_region){0U, 0U, 0U, 0U};
    if (!cursor_damage_arguments_valid(damage, core, output, output_size) ||
        !damage->ready) {
        return false;
    }
    expected = cursor_region(core);
    if ((damage->saved_region.x != expected.x) ||
        (damage->saved_region.y != expected.y) ||
        (damage->saved_region.width != expected.width) ||
        (damage->saved_region.height != expected.height)) {
        return false;
    }
    boring_display_cursor_move(core, dx, dy);
    if ((core->cursor_x == old_x) && (core->cursor_y == old_y)) {
        return true;
    }

    *old_region = damage->saved_region;
    cursor_underlay_copy(damage, core, output, old_region, true);
    counter_add(&damage->restored_pixels, region_pixels(old_region));

    *new_region = cursor_region(core);
    cursor_underlay_copy(damage, core, output, new_region, false);
    damage->saved_region = *new_region;
    counter_add(&damage->saved_pixels, region_pixels(new_region));
    counter_add(&damage->drawn_pixels, cursor_drawn_pixels(core));
    counter_add(&damage->moves, 1ULL);
    boring_display_compose_cursor(core, output);
    return true;
}

bool boring_display_compose(const struct boring_display_core *core,
                            uint8_t *output,
                            size_t output_size) {
    uint64_t pixel_count;
    uint64_t pixel;

    if ((core == NULL) || (output == NULL) ||
        (core->byte_size > (uint64_t)SIZE_MAX) ||
        (output_size != (size_t)core->byte_size) ||
        (core->live_surfaces > BORING_DISPLAY_SURFACE_MAX)) {
        return false;
    }
    pixel_count = (uint64_t)core->width * (uint64_t)core->height;
    for (pixel = 0ULL; pixel < pixel_count; ++pixel) {
        const size_t offset = (size_t)(pixel * 4ULL);
        output[offset] = 0x18U;
        output[offset + 1U] = 0x11U;
        output[offset + 2U] = 0x0bU;
        output[offset + 3U] = 0U;
    }
    if (!compose_surfaces(core, output)) {
        return false;
    }
    boring_display_compose_cursor(core, output);
    return true;
}
