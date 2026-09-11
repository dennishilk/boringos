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
#include "../user/boring-terminal/render.h"
#include "../user/boringwm/core.h"

#define WIDTH 1920U
#define HEIGHT 1080U
#define LOGICAL_STRIDE (WIDTH * BORING_DISPLAY_BYTES_PER_PIXEL)
#define LOGICAL_BYTES ((uint64_t)LOGICAL_STRIDE * HEIGHT)
#define PHYSICAL_PITCH 8192U
#define PHYSICAL_BYTES ((uint64_t)PHYSICAL_PITCH * HEIGHT)
#define MANAGER_ENDPOINT 90U
#define MANAGER_PID 900ULL
#define OWNER_ENDPOINT 11U
#define OWNER_PID 101ULL
#define TERMINAL_UPDATES 100U

enum present_kind {
    PRESENT_KIND_SCENE,
    PRESENT_KIND_LAYOUT
};

struct measurement {
    uint64_t compose_operations;
    uint64_t composed_pixels;
    uint64_t framebuffer_copy_pixels;
    uint64_t presents;
    uint64_t full_presents;
    uint64_t bounded_presents;
    uint64_t scene_presents;
    uint64_t layout_presents;
    uint64_t layout_pixels;
};

struct fixture {
    struct boring_display_core core;
    struct display_managed managed;
    struct display_layout_damage layout_damage;
    struct boring_display_cursor_damage cursor_damage;
    struct wm_core wm;
    uint8_t *surface_pixels;
    uint8_t *composition;
    uint8_t *physical;
    uint32_t surface;
    uint32_t window;
};

static unsigned int failures;

static void check(bool condition, const char *name) {
    if (!condition) {
        (void)fprintf(stderr, "m68-performance-host: FAIL: %s\n", name);
        ++failures;
    }
}

static bool copy_region(const struct fixture *fixture,
                        const struct boring_display_region *region) {
    uint32_t row;
    if ((fixture == NULL) || (fixture->composition == NULL) ||
        (fixture->physical == NULL) || (region == NULL) ||
        (region->width == 0U) || (region->height == 0U) ||
        (region->x >= WIDTH) || (region->y >= HEIGHT) ||
        (region->width > WIDTH - region->x) ||
        (region->height > HEIGHT - region->y)) {
        return false;
    }
    for (row = 0U; row < region->height; ++row) {
        const size_t source =
            (size_t)(region->y + row) * LOGICAL_STRIDE +
            (size_t)region->x * BORING_DISPLAY_BYTES_PER_PIXEL;
        const size_t destination =
            (size_t)(region->y + row) * PHYSICAL_PITCH +
            (size_t)region->x * BORING_DISPLAY_BYTES_PER_PIXEL;
        (void)memcpy(fixture->physical + destination,
                     fixture->composition + source,
                     (size_t)region->width * BORING_DISPLAY_BYTES_PER_PIXEL);
    }
    return true;
}

static bool visible_scanout_matches(const struct fixture *fixture) {
    uint8_t *reference;
    uint32_t row;
    bool matches = true;
    reference = (uint8_t *)malloc((size_t)LOGICAL_BYTES);
    if (reference == NULL) { return false; }
    (void)memset(reference, 0xa5, (size_t)LOGICAL_BYTES);
    if (!display_managed_compose(&fixture->managed, &fixture->core,
                                 reference, (size_t)LOGICAL_BYTES)) {
        free(reference);
        return false;
    }
    for (row = 0U; row < HEIGHT; ++row) {
        if (memcmp(reference + (size_t)row * LOGICAL_STRIDE,
                   fixture->physical + (size_t)row * PHYSICAL_PITCH,
                   LOGICAL_STRIDE) != 0) {
            matches = false;
            break;
        }
    }
    free(reference);
    return matches;
}

static bool physical_padding_preserved(const struct fixture *fixture) {
    uint32_t row;
    for (row = 0U; row < HEIGHT; ++row) {
        size_t offset;
        for (offset = (size_t)row * PHYSICAL_PITCH + LOGICAL_STRIDE;
             offset < (size_t)(row + 1U) * PHYSICAL_PITCH; ++offset) {
            if (fixture->physical[offset] != 0xa5U) { return false; }
        }
    }
    return true;
}

static uint32_t layout_control(struct fixture *fixture,
                               uint32_t endpoint,
                               uint64_t peer_pid,
                               const struct display_control *request) {
    return display_managed_layout_control(&fixture->managed,
                                          &fixture->layout_damage,
                                          &fixture->core,
                                          endpoint, peer_pid, request);
}

static bool fixture_init(struct fixture *fixture) {
    const struct boring_display_scanout_info info = {
        BORING_DISPLAY_SCANOUT_VERSION, WIDTH, HEIGHT,
        LOGICAL_STRIDE, LOGICAL_BYTES
    };
    const struct boring_display_request create = {
        BORING_DISPLAY_PROTOCOL_VERSION,
        BORING_DISPLAY_REQUEST_CREATE,
        BORING_DISPLAY_SURFACE_INVALID,
        WIDTH, HEIGHT, LOGICAL_STRIDE,
        BORING_DISPLAY_PIXEL_FORMAT_XRGB8888,
        0U, LOGICAL_BYTES
    };
    struct display_control request = {0};
    const struct wm_client *client;
    struct boring_display_region full = {0U, 0U, WIDTH, HEIGHT};
    size_t offset;

    (void)memset(fixture, 0, sizeof(*fixture));
    fixture->surface_pixels = (uint8_t *)malloc((size_t)LOGICAL_BYTES);
    fixture->composition = (uint8_t *)malloc((size_t)LOGICAL_BYTES);
    fixture->physical = (uint8_t *)malloc((size_t)PHYSICAL_BYTES);
    if ((fixture->surface_pixels == NULL) ||
        (fixture->composition == NULL) || (fixture->physical == NULL)) {
        return false;
    }
    for (offset = 0U; offset < (size_t)LOGICAL_BYTES; offset += 4U) {
        fixture->surface_pixels[offset] = 0x21U;
        fixture->surface_pixels[offset + 1U] = 0x20U;
        fixture->surface_pixels[offset + 2U] = 0x1dU;
        fixture->surface_pixels[offset + 3U] = 0U;
    }
    (void)memset(fixture->composition, 0xa5, (size_t)LOGICAL_BYTES);
    (void)memset(fixture->physical, 0xa5, (size_t)PHYSICAL_BYTES);
    if (!boring_display_core_init(&fixture->core, &info)) { return false; }
    display_managed_init(&fixture->managed);
    display_layout_damage_init(&fixture->layout_damage);
    fixture->managed.manager_endpoint = MANAGER_ENDPOINT;

    request.version = BORING_DISPLAY_CONTROL_VERSION;
    request.type = DISPLAY_PRESENT;
    request.background = BORING_WM_BACKGROUND;
    if (layout_control(fixture, MANAGER_ENDPOINT, MANAGER_PID, &request) !=
        BORING_DISPLAY_STATUS_OK) {
        return false;
    }
    if (!display_managed_compose_scene(&fixture->managed, &fixture->core,
                                       fixture->composition,
                                       (size_t)LOGICAL_BYTES)) {
        return false;
    }
    boring_display_cursor_damage_init(&fixture->cursor_damage);
    if (!boring_display_cursor_damage_reset(&fixture->cursor_damage,
                                            &fixture->core,
                                            fixture->composition,
                                            (size_t)LOGICAL_BYTES) ||
        !copy_region(fixture, &full)) {
        return false;
    }
    display_managed_layout_complete(&fixture->layout_damage);

    if (boring_display_surface_add(&fixture->core, OWNER_ENDPOINT, &create,
                                   1U, fixture->surface_pixels,
                                   &fixture->surface) !=
        BORING_DISPLAY_STATUS_OK) {
        return false;
    }
    request = (struct display_control){0};
    request.version = BORING_DISPLAY_CONTROL_VERSION;
    request.type = DISPLAY_DELEGATE;
    request.surface = fixture->surface;
    if (layout_control(fixture, OWNER_ENDPOINT, OWNER_PID, &request) !=
        BORING_DISPLAY_STATUS_OK) {
        return false;
    }
    if (!wm_init(&fixture->wm, WIDTH, HEIGHT) ||
        (wm_add(&fixture->wm, OWNER_ENDPOINT, OWNER_PID, fixture->surface,
                &fixture->window) != BORING_WM_OK)) {
        return false;
    }
    request = (struct display_control){0};
    request.version = BORING_DISPLAY_CONTROL_VERSION;
    request.type = DISPLAY_BIND;
    request.surface = fixture->surface;
    request.window = fixture->window;
    request.owner_pid = OWNER_PID;
    if (layout_control(fixture, MANAGER_ENDPOINT, MANAGER_PID, &request) !=
        BORING_DISPLAY_STATUS_OK) {
        return false;
    }
    client = wm_lookup(&fixture->wm, fixture->window);
    if (client == NULL) { return false; }
    request = (struct display_control){0};
    request.version = BORING_DISPLAY_CONTROL_VERSION;
    request.type = DISPLAY_PLACE;
    request.surface = fixture->surface;
    request.window = fixture->window;
    request.x = client->rect.x;
    request.y = client->rect.y;
    request.width = client->rect.width;
    request.height = client->rect.height;
    request.border = client->rect.border;
    request.color = BORING_WM_FOCUSED;
    request.order = 0U;
    if (layout_control(fixture, MANAGER_ENDPOINT, MANAGER_PID, &request) !=
        BORING_DISPLAY_STATUS_OK) {
        return false;
    }
    request = (struct display_control){0};
    request.version = BORING_DISPLAY_CONTROL_VERSION;
    request.type = DISPLAY_PRESENT;
    request.background = BORING_WM_BACKGROUND;
    return layout_control(fixture, MANAGER_ENDPOINT, MANAGER_PID, &request) ==
        BORING_DISPLAY_STATUS_OK;
}

static void fixture_destroy(struct fixture *fixture) {
    free(fixture->physical);
    free(fixture->composition);
    free(fixture->surface_pixels);
}

static bool present_full(struct fixture *fixture,
                         struct measurement *measurement) {
    const struct boring_display_region full = {0U, 0U, WIDTH, HEIGHT};
    if (!display_managed_compose_scene(&fixture->managed, &fixture->core,
                                       fixture->composition,
                                       (size_t)LOGICAL_BYTES) ||
        !boring_display_cursor_damage_reset(&fixture->cursor_damage,
                                            &fixture->core,
                                            fixture->composition,
                                            (size_t)LOGICAL_BYTES) ||
        !copy_region(fixture, &full)) {
        return false;
    }
    ++measurement->compose_operations;
    measurement->composed_pixels += (uint64_t)WIDTH * HEIGHT;
    measurement->framebuffer_copy_pixels += (uint64_t)WIDTH * HEIGHT;
    ++measurement->presents;
    ++measurement->full_presents;
    return true;
}

static bool present_regions(struct fixture *fixture,
                            const struct boring_display_region *regions,
                            size_t count,
                            enum present_kind kind,
                            struct measurement *measurement) {
    size_t index;
    if (!boring_display_cursor_damage_restore(&fixture->cursor_damage,
                                              &fixture->core,
                                              fixture->composition,
                                              (size_t)LOGICAL_BYTES)) {
        return false;
    }
    for (index = 0U; index < count; ++index) {
        uint64_t pixels = 0ULL;
        if (!display_managed_compose_scene_region(
                &fixture->managed, &fixture->core, fixture->composition,
                (size_t)LOGICAL_BYTES, &regions[index], &pixels)) {
            return false;
        }
        ++measurement->compose_operations;
        measurement->composed_pixels += pixels;
        if (kind == PRESENT_KIND_LAYOUT) {
            measurement->layout_pixels += pixels;
        }
    }
    if (!boring_display_cursor_damage_reset(&fixture->cursor_damage,
                                            &fixture->core,
                                            fixture->composition,
                                            (size_t)LOGICAL_BYTES)) {
        return false;
    }
    for (index = 0U; index < count; ++index) {
        const uint64_t pixels =
            (uint64_t)regions[index].width * regions[index].height;
        if (!copy_region(fixture, &regions[index])) { return false; }
        measurement->framebuffer_copy_pixels += pixels;
        ++measurement->presents;
        ++measurement->bounded_presents;
        if (kind == PRESENT_KIND_LAYOUT) {
            ++measurement->layout_presents;
        } else {
            ++measurement->scene_presents;
        }
    }
    return true;
}

static bool present_terminal_initial_damage(struct fixture *fixture,
                                            struct measurement *measurement) {
    const struct wm_client *client = wm_lookup(&fixture->wm, fixture->window);
    struct boring_display_region client_damage;
    struct boring_display_region screen_damage;
    if (client == NULL) { return false; }
    client_damage = (struct boring_display_region){
        0U, 0U,
        client->rect.width - 2U * client->rect.border,
        client->rect.height - 2U * client->rect.border
    };
    if (!display_managed_damage_region(&fixture->managed, &fixture->core,
                                       fixture->surface, &client_damage,
                                       &screen_damage)) {
        return false;
    }
    return present_regions(fixture, &screen_damage, 1U,
                           PRESENT_KIND_SCENE, measurement);
}

static bool measure_win_return(bool bounded_layout,
                               struct measurement *measurement) {
    struct fixture fixture;
    struct boring_display_region regions[BORING_DISPLAY_LAYOUT_REGION_MAX];
    size_t count = 0U;
    bool ok;
    if (!fixture_init(&fixture)) {
        fixture_destroy(&fixture);
        return false;
    }
    if (bounded_layout) {
        ok = display_managed_layout_regions(
                 &fixture.managed, &fixture.layout_damage, &fixture.core,
                 regions, BORING_DISPLAY_LAYOUT_REGION_MAX, &count) &&
            (count != 0U) &&
            present_regions(&fixture, regions, count,
                            PRESENT_KIND_LAYOUT, measurement);
    } else {
        ok = present_full(&fixture, measurement);
    }
    if (ok) {
        display_managed_layout_complete(&fixture.layout_damage);
        ok = present_terminal_initial_damage(&fixture, measurement) &&
            visible_scanout_matches(&fixture) &&
            physical_padding_preserved(&fixture);
    }
    fixture_destroy(&fixture);
    return ok;
}

static void surface_fill_region(struct fixture *fixture,
                                const struct boring_display_region *region,
                                uint8_t value) {
    uint32_t row;
    for (row = 0U; row < region->height; ++row) {
        uint32_t column;
        for (column = 0U; column < region->width; ++column) {
            const size_t offset =
                (size_t)(region->y + row) * LOGICAL_STRIDE +
                (size_t)(region->x + column) * BORING_DISPLAY_BYTES_PER_PIXEL;
            fixture->surface_pixels[offset] = value;
            fixture->surface_pixels[offset + 1U] = (uint8_t)(value + 1U);
            fixture->surface_pixels[offset + 2U] = (uint8_t)(value + 2U);
            fixture->surface_pixels[offset + 3U] = 0U;
        }
    }
}

static bool measure_terminal_updates(struct measurement *measurement) {
    struct fixture fixture;
    struct boring_display_region layout_regions[BORING_DISPLAY_LAYOUT_REGION_MAX];
    size_t layout_count = 0U;
    uint32_t update;
    bool ok;
    if (!fixture_init(&fixture)) {
        fixture_destroy(&fixture);
        return false;
    }
    ok = display_managed_layout_regions(
             &fixture.managed, &fixture.layout_damage, &fixture.core,
             layout_regions, BORING_DISPLAY_LAYOUT_REGION_MAX,
             &layout_count) &&
        present_regions(&fixture, layout_regions, layout_count,
                        PRESENT_KIND_LAYOUT, measurement) &&
        present_terminal_initial_damage(&fixture, measurement);
    display_managed_layout_complete(&fixture.layout_damage);
    if (!ok) {
        fixture_destroy(&fixture);
        return false;
    }
    (void)memset(measurement, 0, sizeof(*measurement));
    for (update = 0U; update < TERMINAL_UPDATES; ++update) {
        struct boring_display_region client_damage = {
            BORING_TERMINAL_MARGIN_X +
                update * BORING_TERMINAL_CELL_WIDTH,
            BORING_TERMINAL_MARGIN_Y,
            2U * BORING_TERMINAL_CELL_WIDTH,
            BORING_TERMINAL_CELL_HEIGHT
        };
        struct boring_display_region screen_damage;
        surface_fill_region(&fixture, &client_damage,
                            (uint8_t)(0x30U + (update & 0x3fU)));
        if (!display_managed_damage_region(
                &fixture.managed, &fixture.core, fixture.surface,
                &client_damage, &screen_damage) ||
            (screen_damage.width != 2U * BORING_TERMINAL_CELL_WIDTH) ||
            (screen_damage.height != BORING_TERMINAL_CELL_HEIGHT) ||
            !present_regions(&fixture, &screen_damage, 1U,
                             PRESENT_KIND_SCENE, measurement)) {
            fixture_destroy(&fixture);
            return false;
        }
    }
    ok = visible_scanout_matches(&fixture) &&
        physical_padding_preserved(&fixture);
    fixture_destroy(&fixture);
    return ok;
}

static void print_measurement(const char *prefix,
                              const struct measurement *measurement) {
    (void)printf("%s_COMPOSE_OPERATIONS=%llu\n", prefix,
                 (unsigned long long)measurement->compose_operations);
    (void)printf("%s_COMPOSED_PIXELS=%llu\n", prefix,
                 (unsigned long long)measurement->composed_pixels);
    (void)printf("%s_FRAMEBUFFER_COPY_PIXELS=%llu\n", prefix,
                 (unsigned long long)measurement->framebuffer_copy_pixels);
    (void)printf("%s_TOTAL_PRESENTS=%llu\n", prefix,
                 (unsigned long long)measurement->presents);
    (void)printf("%s_FULL_PRESENTS=%llu\n", prefix,
                 (unsigned long long)measurement->full_presents);
    (void)printf("%s_BOUNDED_PRESENTS=%llu\n", prefix,
                 (unsigned long long)measurement->bounded_presents);
    (void)printf("%s_SCENE_PRESENTS=%llu\n", prefix,
                 (unsigned long long)measurement->scene_presents);
    (void)printf("%s_LAYOUT_PRESENTS=%llu\n", prefix,
                 (unsigned long long)measurement->layout_presents);
    (void)printf("%s_LAYOUT_PIXELS=%llu\n", prefix,
                 (unsigned long long)measurement->layout_pixels);
}

int main(void) {
    struct measurement terminal = {0};
    struct measurement before = {0};
    struct measurement after = {0};
    const uint64_t full_pixels = (uint64_t)WIDTH * HEIGHT;
    const uint64_t terminal_pixels =
        (uint64_t)TERMINAL_UPDATES * 2ULL *
        BORING_TERMINAL_CELL_WIDTH * BORING_TERMINAL_CELL_HEIGHT;

    check(PHYSICAL_PITCH > LOGICAL_STRIDE,
          "measurement uses 1920x1080 padded physical pitch");
    check(measure_terminal_updates(&terminal),
          "measure 100 production Terminal-style damage updates");
    check((terminal.compose_operations == TERMINAL_UPDATES) &&
          (terminal.composed_pixels == terminal_pixels) &&
          (terminal.framebuffer_copy_pixels == terminal_pixels) &&
          (terminal.presents == TERMINAL_UPDATES) &&
          (terminal.full_presents == 0ULL) &&
          (terminal.bounded_presents == TERMINAL_UPDATES) &&
          (terminal.scene_presents == TERMINAL_UPDATES) &&
          (terminal.layout_presents == 0ULL),
          "Terminal updates remain exact 96-pixel bounded scene presents");

    check(measure_win_return(false, &before),
          "measure historical full-layout Win+Return sequence");
    check(measure_win_return(true, &after),
          "measure final bounded-layout Win+Return sequence");
    check((before.compose_operations == 2ULL) &&
          (before.composed_pixels == 4105396ULL) &&
          (before.framebuffer_copy_pixels == 4105396ULL) &&
          (before.full_presents == 1ULL) &&
          (before.bounded_presents == 1ULL) &&
          (before.layout_pixels == 0ULL),
          "historical Win+Return measurement is exact");
    check((after.compose_operations == 2ULL) &&
          (after.composed_pixels == 4081460ULL) &&
          (after.framebuffer_copy_pixels == 4081460ULL) &&
          (after.full_presents == 0ULL) &&
          (after.bounded_presents == 2ULL) &&
          (after.layout_presents == 1ULL) &&
          (after.layout_pixels == 2049664ULL),
          "final Win+Return removes global layout present");
    check((before.composed_pixels - after.composed_pixels == 23936ULL) &&
          (before.framebuffer_copy_pixels -
           after.framebuffer_copy_pixels == 23936ULL) &&
          (before.full_presents - after.full_presents == 1ULL),
          "Win+Return measured delta proves redundant global present gone");

    (void)printf("TERMINAL_BEFORE_COMPOSED_PIXELS=%llu\n",
                 (unsigned long long)(full_pixels * TERMINAL_UPDATES));
    (void)printf("TERMINAL_BEFORE_FRAMEBUFFER_COPY_PIXELS=%llu\n",
                 (unsigned long long)(full_pixels * TERMINAL_UPDATES));
    (void)printf("TERMINAL_BEFORE_TOTAL_PRESENTS=%u\n", TERMINAL_UPDATES);
    print_measurement("TERMINAL_AFTER", &terminal);
    (void)printf("TERMINAL_AFTER_AVERAGE_DAMAGED_PIXELS=%llu\n",
                 (unsigned long long)(terminal.composed_pixels /
                                      TERMINAL_UPDATES));
    print_measurement("WIN_RETURN_BEFORE", &before);
    print_measurement("WIN_RETURN_AFTER", &after);

    if (failures != 0U) {
        (void)fprintf(stderr, "m68-performance-host: %u failure(s)\n",
                      failures);
        return 1;
    }
    (void)puts("M68 deterministic 1920x1080 performance measurements passed.");
    return 0;
}
