#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <boring/display_abi.h>
#include <boring/display_control.h>
#include <boring/wm.h>

#include "../user/boring-display/core.h"
#include "../user/boring-display/managed.h"

#define WIDTH 800U
#define HEIGHT 600U
#define LOGICAL_STRIDE (WIDTH * BORING_DISPLAY_BYTES_PER_PIXEL)
#define SURFACE_BYTES ((uint64_t)LOGICAL_STRIDE * HEIGHT)
#define PADDED_STRIDE 3328U
#define PADDED_BYTES ((uint64_t)PADDED_STRIDE * HEIGHT)
#define GUARD_BYTES 32U
#define MANAGER_ENDPOINT 99U

static unsigned int failures;

static void check(bool condition, const char *name) {
    if (!condition) {
        (void)fprintf(stderr, "m68-damage-matrix-host: FAIL: %s\n", name);
        ++failures;
    }
}

static void fill_surface(uint8_t *pixels, uint8_t blue,
                         uint8_t green, uint8_t red) {
    size_t offset;
    for (offset = 0U; offset < (size_t)SURFACE_BYTES; offset += 4U) {
        pixels[offset] = blue;
        pixels[offset + 1U] = green;
        pixels[offset + 2U] = red;
        pixels[offset + 3U] = 0U;
    }
}

static void fill_client_region(uint8_t *pixels,
                               const struct boring_display_region *region,
                               uint8_t blue, uint8_t green, uint8_t red) {
    uint32_t row;
    for (row = 0U; row < region->height; ++row) {
        uint32_t column;
        for (column = 0U; column < region->width; ++column) {
            const size_t offset =
                (size_t)(region->y + row) * LOGICAL_STRIDE +
                (size_t)(region->x + column) * BORING_DISPLAY_BYTES_PER_PIXEL;
            pixels[offset] = blue;
            pixels[offset + 1U] = green;
            pixels[offset + 2U] = red;
            pixels[offset + 3U] = 0U;
        }
    }
}

static bool guards_preserved(const uint8_t *guarded) {
    size_t index;
    for (index = 0U; index < GUARD_BYTES; ++index) {
        if ((guarded[index] != 0xa5U) ||
            (guarded[GUARD_BYTES + (size_t)PADDED_BYTES + index] != 0xa5U)) {
            return false;
        }
    }
    return true;
}

static bool exact_reference(const struct display_managed *managed,
                            const struct boring_display_core *core,
                            const uint8_t *actual,
                            uint8_t *reference) {
    uint32_t row;
    (void)memset(reference, 0x5a, (size_t)PADDED_BYTES);
    if (!display_managed_compose(managed, core, reference,
                                 (size_t)PADDED_BYTES)) {
        return false;
    }
    for (row = 0U; row < HEIGHT; ++row) {
        if (memcmp(actual + (size_t)row * PADDED_STRIDE,
                   reference + (size_t)row * PADDED_STRIDE,
                   LOGICAL_STRIDE) != 0) {
            return false;
        }
    }
    return true;
}

static bool region_overlap(const struct boring_display_region *first,
                           const struct boring_display_region *second) {
    return ((uint64_t)first->x < (uint64_t)second->x + second->width) &&
        ((uint64_t)second->x < (uint64_t)first->x + first->width) &&
        ((uint64_t)first->y < (uint64_t)second->y + second->height) &&
        ((uint64_t)second->y < (uint64_t)first->y + first->height);
}

static bool commit_damage(struct display_managed *managed,
                          struct boring_display_core *core,
                          struct boring_display_cursor_damage *cursor,
                          uint8_t *output,
                          uint32_t endpoint,
                          uint32_t surface,
                          const struct boring_display_region *client_damage,
                          struct boring_display_region *screen_damage) {
    uint64_t pixels = 0ULL;
    return (boring_display_surface_commit(core, endpoint, surface) ==
            BORING_DISPLAY_STATUS_OK) &&
        display_managed_damage_region(managed, core, surface,
                                      client_damage, screen_damage) &&
        (screen_damage->width != 0U) && (screen_damage->height != 0U) &&
        boring_display_cursor_damage_restore(cursor, core, output,
                                             (size_t)PADDED_BYTES) &&
        display_managed_compose_scene_region(managed, core, output,
                                             (size_t)PADDED_BYTES,
                                             screen_damage, &pixels) &&
        (pixels == (uint64_t)screen_damage->width * screen_damage->height) &&
        boring_display_cursor_damage_reset(cursor, core, output,
                                           (size_t)PADDED_BYTES);
}

static void corrupt_region(uint8_t *output,
                           const struct boring_display_region *region) {
    uint32_t row;
    for (row = 0U; row < region->height; ++row) {
        (void)memset(output +
                     (size_t)(region->y + row) * PADDED_STRIDE +
                     (size_t)region->x * BORING_DISPLAY_BYTES_PER_PIXEL,
                     0xee,
                     (size_t)region->width * BORING_DISPLAY_BYTES_PER_PIXEL);
    }
}

static void run_matrix(void) {
    const struct boring_display_scanout_info info = {
        BORING_DISPLAY_SCANOUT_VERSION, WIDTH, HEIGHT,
        PADDED_STRIDE, PADDED_BYTES
    };
    const struct boring_display_request create = {
        BORING_DISPLAY_PROTOCOL_VERSION,
        BORING_DISPLAY_REQUEST_CREATE,
        BORING_DISPLAY_SURFACE_INVALID,
        WIDTH, HEIGHT, LOGICAL_STRIDE,
        BORING_DISPLAY_PIXEL_FORMAT_XRGB8888,
        0U, SURFACE_BYTES
    };
    struct boring_display_core core;
    struct boring_display_core moved_core;
    struct display_managed managed;
    struct boring_display_cursor_damage cursor;
    struct boring_display_region client_damage;
    struct boring_display_region screen_damage;
    struct boring_display_region old_cursor;
    struct boring_display_region new_cursor;
    struct boring_display_region focus_regions[BORING_DISPLAY_FOCUS_REGION_MAX];
    struct boring_display_region edge_regions[4] = {
        {0U, 0U, WIDTH, 1U},
        {0U, HEIGHT - 1U, WIDTH, 1U},
        {0U, 0U, 1U, HEIGHT},
        {WIDTH - 1U, 0U, 1U, HEIGHT}
    };
    struct display_control focus = {0};
    uint8_t *surface_a = NULL;
    uint8_t *surface_b = NULL;
    uint8_t *guarded = NULL;
    uint8_t *output;
    uint8_t *reference = NULL;
    uint32_t surface_a_token = 0U;
    uint32_t surface_b_token = 0U;
    size_t focus_count = 0U;
    uint64_t focus_pixels = 0ULL;
    uint64_t composed_pixels = 0ULL;
    size_t index;
    bool focus_intersects_cursor = false;
    bool ready;

    surface_a = (uint8_t *)malloc((size_t)SURFACE_BYTES);
    surface_b = (uint8_t *)malloc((size_t)SURFACE_BYTES);
    guarded = (uint8_t *)malloc((size_t)PADDED_BYTES + 2U * GUARD_BYTES);
    reference = (uint8_t *)malloc((size_t)PADDED_BYTES);
    ready = (surface_a != NULL) && (surface_b != NULL) &&
        (guarded != NULL) && (reference != NULL);
    check(ready, "allocate deterministic 800x600 padded-pitch matrix");
    if (!ready) { goto cleanup; }
    output = guarded + GUARD_BYTES;
    fill_surface(surface_a, 0x22U, 0x44U, 0x66U);
    fill_surface(surface_b, 0x88U, 0x99U, 0xaaU);
    (void)memset(guarded, 0xa5,
                 (size_t)PADDED_BYTES + 2U * GUARD_BYTES);
    (void)memset(reference, 0x5a, (size_t)PADDED_BYTES);

    ready = boring_display_core_init(&core, &info) &&
        (boring_display_surface_add(&core, 10U, &create, 1U,
                                    surface_a, &surface_a_token) ==
         BORING_DISPLAY_STATUS_OK) &&
        (boring_display_surface_add(&core, 11U, &create, 2U,
                                    surface_b, &surface_b_token) ==
         BORING_DISPLAY_STATUS_OK);
    check(ready, "initialize two managed surfaces at padded 800x600");
    if (!ready) { goto cleanup; }
    display_managed_init(&managed);
    managed.manager_endpoint = MANAGER_ENDPOINT;
    managed.wallpaper = true;
    managed.background = BORING_WM_BACKGROUND;
    managed.placements[0] = (struct display_placement){
        surface_a_token, 257U, 10U, 10U, 300U, 220U, 3U,
        BORING_WM_FOCUSED, 0U, 10ULL, true, true
    };
    managed.placements[1] = (struct display_placement){
        surface_b_token, 258U, 200U, 100U, 300U, 220U, 3U,
        BORING_WM_UNFOCUSED, 1U, 11ULL, true, true
    };
    core.cursor_x = 240U;
    core.cursor_y = 140U;
    check(boring_display_surface_commit(&core, 10U, surface_a_token) ==
          BORING_DISPLAY_STATUS_OK,
          "legacy full COMMIT accepts owner and retains full-present path");
    check(display_managed_compose(&managed, &core, output,
                                  (size_t)PADDED_BYTES),
          "legacy COMMIT full composition succeeds");
    check((output[(size_t)120U * PADDED_STRIDE + 220U * 4U] == 0x88U) &&
          (output[(size_t)120U * PADDED_STRIDE + 220U * 4U + 1U] == 0x99U),
          "overlap uses final top-window z-order");
    boring_display_cursor_damage_init(&cursor);
    check(boring_display_cursor_damage_reset(&cursor, &core, output,
                                             (size_t)PADDED_BYTES),
          "establish cursor underlay for damage matrix");

    client_damage = (struct boring_display_region){220U, 120U, 30U, 30U};
    fill_client_region(surface_a, &client_damage, 0x0fU, 0x1fU, 0x2fU);
    check(commit_damage(&managed, &core, &cursor, output, 10U,
                        surface_a_token, &client_damage, &screen_damage),
          "COMMIT_DAMAGE composes cursor-intersecting scene region");
    check(region_overlap(&screen_damage, &cursor.saved_region),
          "scene damage explicitly intersects cursor");
    check(exact_reference(&managed, &core, output, reference),
          "cursor-intersecting overlap damage has no stale z-order pixels");

    client_damage = (struct boring_display_region){0U, 0U, 6U, 8U};
    fill_client_region(surface_a, &client_damage, 0x70U, 0x60U, 0x50U);
    check(commit_damage(&managed, &core, &cursor, output, 10U,
                        surface_a_token, &client_damage, &screen_damage),
          "COMMIT_DAMAGE composes cursor-disjoint scene region");
    check(!region_overlap(&screen_damage, &cursor.saved_region),
          "scene damage explicitly does not intersect cursor");
    check(exact_reference(&managed, &core, output, reference),
          "cursor-disjoint damage preserves cursor and scene exactly");

    moved_core = core;
    boring_display_cursor_move(&moved_core, 7, 5);
    check(boring_display_cursor_damage_move(&cursor, &core, output,
                                            (size_t)PADDED_BYTES, 7, 5,
                                            &old_cursor, &new_cursor),
          "cursor move restores and rebuilds saved underlay");
    check(exact_reference(&managed, &core, output, reference) &&
          (core.cursor_x == moved_core.cursor_x) &&
          (core.cursor_y == moved_core.cursor_y),
          "rebuilt cursor underlay equals full reference without trails");

    check(boring_display_cursor_damage_restore(&cursor, &core, output,
                                               (size_t)PADDED_BYTES),
          "restore cursor before non-overlapping focus layout");
    managed.placements[0] = (struct display_placement){
        surface_a_token, 257U, 4U, 4U, 470U, 592U, 3U,
        BORING_WM_FOCUSED, 0U, 10ULL, true, true
    };
    managed.placements[1] = (struct display_placement){
        surface_b_token, 258U, 482U, 4U, 314U, 592U, 3U,
        BORING_WM_UNFOCUSED, 1U, 11ULL, true, true
    };
    core.cursor_x = 482U;
    core.cursor_y = 100U;
    check(display_managed_compose_scene(&managed, &core, output,
                                        (size_t)PADDED_BYTES),
          "compose non-overlapping focus scene");
    boring_display_cursor_damage_init(&cursor);
    check(boring_display_cursor_damage_reset(&cursor, &core, output,
                                             (size_t)PADDED_BYTES),
          "establish cursor on focus-border damage");
    focus.version = BORING_DISPLAY_CONTROL_VERSION;
    focus.type = DISPLAY_PRESENT_FOCUS;
    focus.window = 258U;
    focus.color = BORING_WM_FOCUSED;
    focus.background = BORING_WM_UNFOCUSED;
    check(display_managed_control(&managed, &core, MANAGER_ENDPOINT, 1ULL,
                                  &focus) == BORING_DISPLAY_STATUS_OK,
          "atomic focus colors update without geometry mutation");
    check(boring_display_cursor_damage_restore(&cursor, &core, output,
                                               (size_t)PADDED_BYTES) &&
          display_managed_compose_focus_borders(
              &managed, &core, output, (size_t)PADDED_BYTES,
              focus_regions, BORING_DISPLAY_FOCUS_REGION_MAX,
              &focus_count, &focus_pixels),
          "compose deferred focus-border damage");
    for (index = 0U; index < focus_count; ++index) {
        if (region_overlap(&focus_regions[index], &cursor.saved_region)) {
            focus_intersects_cursor = true;
        }
    }
    check(focus_intersects_cursor,
          "focus-border damage explicitly intersects cursor underlay");
    check(boring_display_cursor_damage_reset(&cursor, &core, output,
                                             (size_t)PADDED_BYTES),
          "rebuild cursor after focus-border damage");
    check((focus_count == 8U) && (focus_pixels == 11736ULL) &&
          exact_reference(&managed, &core, output, reference),
          "bounded focus regions equal atomic full-scene reference");

    for (index = 0U; index < 4U; ++index) {
        corrupt_region(output, &edge_regions[index]);
        check(display_managed_compose_scene_region(
                  &managed, &core, output, (size_t)PADDED_BYTES,
                  &edge_regions[index], &composed_pixels) &&
              (composed_pixels ==
               (uint64_t)edge_regions[index].width *
               edge_regions[index].height),
              "top/bottom/left/right edge regional composition bounded");
    }
    check(exact_reference(&managed, &core, output, reference),
          "all four screen edges recompose without stale pixels");

    client_damage = (struct boring_display_region){0U, 0U, 0U, 1U};
    check(!display_managed_damage_region(&managed, &core, surface_a_token,
                                         &client_damage, &screen_damage),
          "zero-width client damage rejected");
    client_damage = (struct boring_display_region){WIDTH, 0U, 1U, 1U};
    check(!display_managed_damage_region(&managed, &core, surface_a_token,
                                         &client_damage, &screen_damage),
          "out-of-surface client damage rejected");
    client_damage = (struct boring_display_region){464U, 0U, 1U, 1U};
    check(display_managed_damage_region(&managed, &core, surface_a_token,
                                        &client_damage, &screen_damage) &&
          (screen_damage.width == 0U) && (screen_damage.height == 0U),
          "damage outside clipped window content becomes a no-op");
    client_damage = (struct boring_display_region){463U, 585U, 99U, 99U};
    check(display_managed_damage_region(&managed, &core, surface_a_token,
                                        &client_damage, &screen_damage) &&
          (screen_damage.x == 470U) && (screen_damage.y == 592U) &&
          (screen_damage.width == 1U) && (screen_damage.height == 1U),
          "right/bottom client damage clips to final content pixel");
    client_damage = (struct boring_display_region){0U, 0U, 1U, 1U};
    check(!display_managed_damage_region(&managed, &core, 0x12345678U,
                                         &client_damage, &screen_damage),
          "unknown surface damage rejected");
    screen_damage = (struct boring_display_region){0U, 0U, 0U, 1U};
    check(!display_managed_compose_scene_region(
              &managed, &core, output, (size_t)PADDED_BYTES,
              &screen_damage, &composed_pixels),
          "zero screen region rejected");
    screen_damage = (struct boring_display_region){WIDTH - 1U, 0U, 2U, 1U};
    check(!display_managed_compose_scene_region(
              &managed, &core, output, (size_t)PADDED_BYTES,
              &screen_damage, &composed_pixels),
          "framebuffer-right overflow region rejected");
    screen_damage = (struct boring_display_region){0U, HEIGHT, 1U, 1U};
    check(!display_managed_compose_scene_region(
              &managed, &core, output, (size_t)PADDED_BYTES,
              &screen_damage, &composed_pixels),
          "framebuffer-bottom overflow region rejected");
    check(guards_preserved(guarded),
          "all matrix paths preserve scanout guard bytes");

cleanup:
    free(reference);
    free(guarded);
    free(surface_b);
    free(surface_a);
}

int main(void) {
    run_matrix();
    if (failures != 0U) {
        (void)fprintf(stderr, "m68-damage-matrix-host: %u failure(s)\n",
                      failures);
        return 1;
    }
    (void)puts("M68 damage matrix: legacy COMMIT + COMMIT_DAMAGE PASS");
    (void)puts("M68 damage matrix: overlap/z-order + cursor/focus PASS");
    (void)puts("M68 damage matrix: all-edge clipping + framebuffer bounds PASS");
    return 0;
}
