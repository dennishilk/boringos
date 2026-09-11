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
#include "../user/boringwm/core.h"

#define WIDTH 1920U
#define HEIGHT 1080U
#define LOGICAL_STRIDE (WIDTH * BORING_DISPLAY_BYTES_PER_PIXEL)
#define LOGICAL_BYTES ((uint64_t)LOGICAL_STRIDE * HEIGHT)
#define PADDED_STRIDE 8192U
#define PADDED_BYTES ((uint64_t)PADDED_STRIDE * HEIGHT)
#define MANAGER_ENDPOINT 90U
#define MANAGER_PID 900ULL
#define OWNER_A_ENDPOINT 11U
#define OWNER_B_ENDPOINT 12U
#define OWNER_A_PID 101ULL
#define OWNER_B_PID 102ULL

static unsigned int failures;

static void check(bool condition, const char *name) {
    if (!condition) {
        (void)fprintf(stderr, "m68-layout-host: FAIL: %s\n", name);
        ++failures;
    }
}

static struct boring_display_region rect_region(const struct wm_rect *rect) {
    return (struct boring_display_region){
        rect->x, rect->y, rect->width, rect->height
    };
}

static bool contains(const struct boring_display_region *outer,
                     const struct boring_display_region *inner) {
    return (inner->x >= outer->x) && (inner->y >= outer->y) &&
        ((uint64_t)inner->x + inner->width <=
         (uint64_t)outer->x + outer->width) &&
        ((uint64_t)inner->y + inner->height <=
         (uint64_t)outer->y + outer->height);
}

static bool regions_cover(const struct boring_display_region *regions,
                          size_t count,
                          const struct boring_display_region *target) {
    size_t index;
    for (index = 0U; index < count; ++index) {
        if (contains(&regions[index], target)) { return true; }
    }
    return false;
}

static bool regions_bounded(const struct boring_display_region *regions,
                            size_t count,
                            uint32_t width,
                            uint32_t height,
                            bool require_subframe) {
    size_t index;
    for (index = 0U; index < count; ++index) {
        const struct boring_display_region *region = &regions[index];
        const uint64_t pixels = (uint64_t)region->width * region->height;
        if ((region->width == 0U) || (region->height == 0U) ||
            (region->x >= width) || (region->y >= height) ||
            (region->width > width - region->x) ||
            (region->height > height - region->y) ||
            (require_subframe &&
             (pixels >= (uint64_t)width * (uint64_t)height))) {
            return false;
        }
    }
    return true;
}

static uint32_t layout_control(struct display_managed *managed,
                               struct display_layout_damage *damage,
                               const struct boring_display_core *core,
                               uint32_t endpoint,
                               uint64_t peer_pid,
                               const struct display_control *request) {
    return display_managed_layout_control(managed, damage, core,
                                          endpoint, peer_pid, request);
}

static bool delegate_surface(struct display_managed *managed,
                             struct display_layout_damage *damage,
                             const struct boring_display_core *core,
                             uint32_t endpoint,
                             uint64_t owner_pid,
                             uint32_t surface) {
    struct display_control request = {0};
    request.version = BORING_DISPLAY_CONTROL_VERSION;
    request.type = DISPLAY_DELEGATE;
    request.surface = surface;
    return layout_control(managed, damage, core, endpoint, owner_pid,
                          &request) == BORING_DISPLAY_STATUS_OK;
}

static bool bind_window(struct display_managed *managed,
                        struct display_layout_damage *damage,
                        const struct boring_display_core *core,
                        uint32_t surface,
                        uint32_t window,
                        uint64_t owner_pid) {
    struct display_control request = {0};
    request.version = BORING_DISPLAY_CONTROL_VERSION;
    request.type = DISPLAY_BIND;
    request.surface = surface;
    request.window = window;
    request.owner_pid = owner_pid;
    return layout_control(managed, damage, core, MANAGER_ENDPOINT,
                          MANAGER_PID, &request) == BORING_DISPLAY_STATUS_OK;
}

static bool place_layout(struct display_managed *managed,
                         struct display_layout_damage *damage,
                         const struct boring_display_core *core,
                         const struct wm_core *wm) {
    uint32_t order;
    for (order = 0U; order < wm->count; ++order) {
        const struct wm_client *client = &wm->clients[wm->order[order]];
        struct display_control request = {0};
        request.version = BORING_DISPLAY_CONTROL_VERSION;
        request.type = DISPLAY_PLACE;
        request.surface = client->surface;
        request.window = client->token;
        request.x = client->rect.x;
        request.y = client->rect.y;
        request.width = client->rect.width;
        request.height = client->rect.height;
        request.border = client->rect.border;
        request.order = order;
        request.color = wm->focus == client->token ?
            BORING_WM_FOCUSED : BORING_WM_UNFOCUSED;
        if (layout_control(managed, damage, core, MANAGER_ENDPOINT,
                           MANAGER_PID, &request) != BORING_DISPLAY_STATUS_OK) {
            return false;
        }
    }
    return true;
}

static bool layout_present(struct display_managed *managed,
                           struct display_layout_damage *damage,
                           const struct boring_display_core *core) {
    struct display_control request = {0};
    request.version = BORING_DISPLAY_CONTROL_VERSION;
    request.type = DISPLAY_PRESENT;
    request.background = BORING_WM_BACKGROUND;
    return layout_control(managed, damage, core, MANAGER_ENDPOINT,
                          MANAGER_PID, &request) == BORING_DISPLAY_STATUS_OK;
}

static bool unbind_window(struct display_managed *managed,
                          struct display_layout_damage *damage,
                          const struct boring_display_core *core,
                          uint32_t surface,
                          uint32_t window) {
    struct display_control request = {0};
    request.version = BORING_DISPLAY_CONTROL_VERSION;
    request.type = DISPLAY_UNBIND;
    request.surface = surface;
    request.window = window;
    return layout_control(managed, damage, core, MANAGER_ENDPOINT,
                          MANAGER_PID, &request) == BORING_DISPLAY_STATUS_OK;
}

static bool compose_layout(struct display_managed *managed,
                           struct display_layout_damage *damage,
                           const struct boring_display_core *core,
                           uint8_t *output,
                           struct boring_display_region *regions,
                           size_t *count) {
    size_t index;
    if (!display_managed_layout_regions(managed, damage, core, regions,
                                        BORING_DISPLAY_LAYOUT_REGION_MAX,
                                        count)) {
        return false;
    }
    for (index = 0U; index < *count; ++index) {
        uint64_t pixels = 0ULL;
        if (!display_managed_compose_scene_region(managed, core, output,
                                                  (size_t)core->byte_size,
                                                  &regions[index], &pixels) ||
            (pixels != (uint64_t)regions[index].width * regions[index].height)) {
            return false;
        }
    }
    display_managed_layout_complete(damage);
    return true;
}

static bool matches_full_reference(const struct display_managed *managed,
                                   const struct boring_display_core *core,
                                   const uint8_t *working,
                                   uint8_t *reference) {
    (void)memset(reference, 0x5a, (size_t)core->byte_size);
    return display_managed_compose_scene(managed, core, reference,
                                         (size_t)core->byte_size) &&
        (memcmp(working, reference, (size_t)core->byte_size) == 0);
}

static void fill_surface(uint8_t *pixels, size_t size,
                         uint8_t blue, uint8_t green, uint8_t red) {
    size_t offset;
    for (offset = 0U; offset + 3U < size; offset += 4U) {
        pixels[offset] = blue;
        pixels[offset + 1U] = green;
        pixels[offset + 2U] = red;
        pixels[offset + 3U] = 0U;
    }
}

static void run_layout_transition_test(void) {
    struct boring_display_scanout_info info = {
        BORING_DISPLAY_SCANOUT_VERSION, WIDTH, HEIGHT,
        PADDED_STRIDE, PADDED_BYTES
    };
    struct boring_display_request create = {
        BORING_DISPLAY_PROTOCOL_VERSION,
        BORING_DISPLAY_REQUEST_CREATE,
        BORING_DISPLAY_SURFACE_INVALID,
        WIDTH, HEIGHT, LOGICAL_STRIDE,
        BORING_DISPLAY_PIXEL_FORMAT_XRGB8888,
        0U, LOGICAL_BYTES
    };
    struct boring_display_core core;
    struct display_managed managed;
    struct display_layout_damage damage;
    struct wm_core wm;
    struct boring_display_region regions[BORING_DISPLAY_LAYOUT_REGION_MAX];
    struct boring_display_region first_old;
    struct boring_display_region second_old;
    struct boring_display_region first_final;
    struct boring_display_region second_final;
    uint8_t *surface_a = NULL;
    uint8_t *surface_b = NULL;
    uint8_t *working = NULL;
    uint8_t *reference = NULL;
    uint32_t surface_a_token = 0U;
    uint32_t surface_b_token = 0U;
    uint32_t window_a = 0U;
    uint32_t window_b = 0U;
    size_t count = 0U;
    const size_t surface_bytes = (size_t)LOGICAL_BYTES;
    const size_t scanout_bytes = (size_t)PADDED_BYTES;
    bool ready = true;

    check(PADDED_STRIDE > LOGICAL_STRIDE,
          "1920x1080 layout test uses nontrivial padded scanout pitch");
    ready = boring_display_core_init(&core, &info);
    check(ready, "initialize 1920x1080 padded display core");
    if (!ready) { return; }
    display_managed_init(&managed);
    display_layout_damage_init(&damage);
    managed.manager_endpoint = MANAGER_ENDPOINT;
    ready = wm_init(&wm, WIDTH, HEIGHT);
    check(ready, "initialize 1920x1080 WM for layout transaction");
    if (!ready) { return; }

    surface_a = (uint8_t *)malloc(surface_bytes);
    surface_b = (uint8_t *)malloc(surface_bytes);
    working = (uint8_t *)malloc(scanout_bytes);
    reference = (uint8_t *)malloc(scanout_bytes);
    ready = (surface_a != NULL) && (surface_b != NULL) &&
        (working != NULL) && (reference != NULL);
    check(ready, "allocate padded layout/reference buffers");
    if (!ready) { goto cleanup; }
    fill_surface(surface_a, surface_bytes, 0x20U, 0x40U, 0x90U);
    fill_surface(surface_b, surface_bytes, 0x80U, 0x30U, 0x20U);
    (void)memset(working, 0x5a, scanout_bytes);
    (void)memset(reference, 0x5a, scanout_bytes);

    ready = boring_display_surface_add(&core, OWNER_A_ENDPOINT, &create,
                                       1U, surface_a, &surface_a_token) ==
            BORING_DISPLAY_STATUS_OK &&
        boring_display_surface_add(&core, OWNER_B_ENDPOINT, &create,
                                   2U, surface_b, &surface_b_token) ==
            BORING_DISPLAY_STATUS_OK;
    check(ready, "create two 1920x1080 managed client surfaces");
    if (!ready) { goto cleanup; }
    ready = delegate_surface(&managed, &damage, &core, OWNER_A_ENDPOINT,
                             OWNER_A_PID, surface_a_token) &&
        delegate_surface(&managed, &damage, &core, OWNER_B_ENDPOINT,
                         OWNER_B_PID, surface_b_token);
    check(ready, "delegate both surfaces through managed display control");
    if (!ready) { goto cleanup; }

    ready = layout_present(&managed, &damage, &core) &&
        compose_layout(&managed, &damage, &core, working, regions, &count);
    check(ready, "initial wallpaper layout transaction composes by region");
    check(ready && (count == 1U) && (regions[0].x == 0U) &&
          (regions[0].y == 0U) && (regions[0].width == WIDTH) &&
          (regions[0].height == HEIGHT),
          "initial wallpaper activation is one full screen region");
    check(ready && matches_full_reference(&managed, &core, working, reference),
          "initial regional wallpaper equals full reference with padded pitch");
    if (!ready) { goto cleanup; }

    ready = wm_add(&wm, OWNER_A_ENDPOINT, OWNER_A_PID,
                   surface_a_token, &window_a) == BORING_WM_OK &&
        bind_window(&managed, &damage, &core, surface_a_token,
                    window_a, OWNER_A_PID) &&
        place_layout(&managed, &damage, &core, &wm) &&
        layout_present(&managed, &damage, &core) &&
        compose_layout(&managed, &damage, &core, working, regions, &count);
    check(ready, "first managed window uses PLACE to bounded PRESENT transaction");
    check(ready && regions_bounded(regions, count, WIDTH, HEIGHT, true),
          "first window transition remains sub-frame and screen-bounded");
    check(ready && matches_full_reference(&managed, &core, working, reference),
          "first bounded layout equals final full-scene reference");
    if (!ready) { goto cleanup; }
    first_old = rect_region(&wm_lookup(&wm, window_a)->rect);

    ready = wm_add(&wm, OWNER_B_ENDPOINT, OWNER_B_PID,
                   surface_b_token, &window_b) == BORING_WM_OK &&
        bind_window(&managed, &damage, &core, surface_b_token,
                    window_b, OWNER_B_PID) &&
        place_layout(&managed, &damage, &core, &wm) &&
        layout_present(&managed, &damage, &core);
    check(ready, "first-to-second window retile uses actual managed controls");
    if (!ready) { goto cleanup; }
    first_final = rect_region(&wm_lookup(&wm, window_a)->rect);
    second_final = rect_region(&wm_lookup(&wm, window_b)->rect);
    ready = compose_layout(&managed, &damage, &core, working, regions, &count);
    check(ready, "compose real 1920x1080 two-window layout damage");
    check(ready && (count != 0U) &&
          regions_cover(regions, count, &first_old),
          "1920x1080 damage includes authoritative OLD window geometry");
    check(ready && regions_cover(regions, count, &first_final) &&
          regions_cover(regions, count, &second_final),
          "1920x1080 damage includes FINAL retiled window geometry");
    check(ready && regions_bounded(regions, count, WIDTH, HEIGHT, true),
          "1920x1080 retile needs no full-frame present and stays bounded");
    check(ready && matches_full_reference(&managed, &core, working, reference),
          "regional retile composes against final placements exactly");
    if (!ready) { goto cleanup; }

    first_old = first_final;
    second_old = second_final;
    ready = wm_reorder(&wm, -1) &&
        place_layout(&managed, &damage, &core, &wm) &&
        layout_present(&managed, &damage, &core) &&
        compose_layout(&managed, &damage, &core, working, regions, &count);
    check(ready, "reorder retile uses bounded OLD/FINAL layout transaction");
    if (!ready) { goto cleanup; }
    first_final = rect_region(&wm_lookup(&wm, window_a)->rect);
    second_final = rect_region(&wm_lookup(&wm, window_b)->rect);
    check(regions_cover(regions, count, &first_old) &&
          regions_cover(regions, count, &second_old),
          "reorder damage covers both OLD z-order geometries");
    check(regions_cover(regions, count, &first_final) &&
          regions_cover(regions, count, &second_final),
          "reorder damage covers both FINAL z-order geometries");
    check(regions_bounded(regions, count, WIDTH, HEIGHT, true),
          "reorder transition stays sub-frame and screen-bounded");
    check(matches_full_reference(&managed, &core, working, reference),
          "regional reorder has correct final z-order with no stale pixels");

    ready = unbind_window(&managed, &damage, &core,
                          surface_b_token, window_b) &&
        (wm_remove(&wm, OWNER_B_ENDPOINT, window_b) == BORING_WM_OK) &&
        place_layout(&managed, &damage, &core, &wm) &&
        layout_present(&managed, &damage, &core) &&
        compose_layout(&managed, &damage, &core, working, regions, &count);
    check(ready, "close/retile keeps removed-window OLD damage through PRESENT");
    check(ready && regions_bounded(regions, count, WIDTH, HEIGHT, true),
          "close-to-one transition remains bounded");
    check(ready && matches_full_reference(&managed, &core, working, reference),
          "close-to-one regional compose matches final scene");
    if (!ready) { goto cleanup; }

    first_old = rect_region(&wm_lookup(&wm, window_a)->rect);
    ready = unbind_window(&managed, &damage, &core,
                          surface_a_token, window_a) &&
        (wm_remove(&wm, OWNER_A_ENDPOINT, window_a) == BORING_WM_OK) &&
        layout_present(&managed, &damage, &core) &&
        compose_layout(&managed, &damage, &core, working, regions, &count);
    check(ready, "last-window UNBIND preserves exposed OLD area");
    check(ready && regions_cover(regions, count, &first_old),
          "last-window OLD rectangle drives exposed wallpaper damage");
    check(ready && regions_bounded(regions, count, WIDTH, HEIGHT, true),
          "last-window wallpaper reconstruction remains bounded");
    check(ready && matches_full_reference(&managed, &core, working, reference),
          "exposed 1920x1080 wallpaper matches full final reference");
    if (ready) {
        const uint32_t sample_x = first_old.x + first_old.width / 2U;
        const uint32_t sample_y = first_old.y + first_old.height / 2U;
        const size_t sample = (size_t)sample_y * PADDED_STRIDE +
            (size_t)sample_x * BORING_DISPLAY_BYTES_PER_PIXEL;
        check(memcmp(working + sample, reference + sample,
                     BORING_DISPLAY_BYTES_PER_PIXEL) == 0,
              "removed-window sample reconstructs wallpaper/background");
    }

cleanup:
    free(reference);
    free(working);
    free(surface_b);
    free(surface_a);
}

int main(void) {
    run_layout_transition_test();
    if (failures != 0U) {
        (void)fprintf(stderr, "m68-layout-host: %u failure(s)\n", failures);
        return 1;
    }
    (void)puts("M68 bounded 1920x1080 layout transition tests passed.");
    return 0;
}
