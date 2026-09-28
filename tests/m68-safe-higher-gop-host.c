#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <boring/display_abi.h>
#include <boring/framebuffer.h>

#define BORING_M61_PHYSICAL_BREADCRUMBS 1
#include <boring/vmm.h>
#undef BORING_M61_PHYSICAL_BREADCRUMBS

#include "../user/boring-display/core.h"
#include "../user/boringwm/core.h"

#define BASE_WIDTH 800U
#define BASE_HEIGHT 600U
#define BASE_PHYSICAL_PITCH 3328ULL
#define BASE_PHYSICAL_BYTES (BASE_PHYSICAL_PITCH * (uint64_t)BASE_HEIGHT)

#define HIGH_WIDTH 1920U
#define HIGH_HEIGHT 1080U
#define HIGH_LOGICAL_STRIDE (HIGH_WIDTH * BORING_DISPLAY_BYTES_PER_PIXEL)
#define HIGH_LOGICAL_BYTES ((uint64_t)HIGH_LOGICAL_STRIDE * HIGH_HEIGHT)
#define HIGH_PHYSICAL_PITCH 8192ULL
#define HIGH_PHYSICAL_BYTES (HIGH_PHYSICAL_PITCH * (uint64_t)HIGH_HEIGHT)

#define TEST_FB_PHYSICAL 0x30000080ULL
#define GUARD_BYTES 16U

static unsigned int failures;

static void check(bool condition, const char *name) {
    if (!condition) {
        (void)fprintf(stderr, "m68-safe-higher-gop-host: FAIL: %s\n", name);
        ++failures;
    }
}

static struct boring_display_scanout_info scanout(uint32_t width,
                                                   uint32_t height) {
    struct boring_display_scanout_info info;

    info.version = BORING_DISPLAY_SCANOUT_VERSION;
    info.width = width;
    info.height = height;
    info.stride = width * BORING_DISPLAY_BYTES_PER_PIXEL;
    info.byte_size = (uint64_t)info.stride * (uint64_t)height;
    return info;
}

static size_t page_span(uint64_t physical, uint64_t bytes) {
    const uint64_t mask = VMM_PAGE_SIZE - 1ULL;
    uint64_t first;
    uint64_t last;

    if ((bytes == 0ULL) || (physical > UINT64_MAX - (bytes - 1ULL))) {
        return 0U;
    }
    first = physical & ~mask;
    last = (physical + bytes - 1ULL) & ~mask;
    return (size_t)(((last - first) / VMM_PAGE_SIZE) + 1ULL);
}

static void framebuffer_geometry_tests(void) {
    struct boring_framebuffer surface;
    uint8_t sentinel = 0U;
    size_t pages;

    check(boring_framebuffer_surface_init(
              &surface, &sentinel,
              BASE_WIDTH, BASE_HEIGHT, BASE_PHYSICAL_PITCH,
              32U, BORING_FRAMEBUFFER_MEMORY_MODEL_RGB,
              8U, 16U, 8U, 8U, 8U, 0U),
          "800x600 baseline framebuffer accepted");
    check((surface.width == BASE_WIDTH) &&
          (surface.height == BASE_HEIGHT) &&
          (surface.pitch == BASE_PHYSICAL_PITCH) &&
          (surface.byte_size == BASE_PHYSICAL_BYTES),
          "800x600 runtime geometry preserved");
    check(BASE_PHYSICAL_PITCH >
              (uint64_t)BASE_WIDTH * BORING_DISPLAY_BYTES_PER_PIXEL,
          "800x600 physical pitch padding retained");

    check(boring_framebuffer_surface_init(
              &surface, &sentinel,
              HIGH_WIDTH, HIGH_HEIGHT, HIGH_PHYSICAL_PITCH,
              32U, BORING_FRAMEBUFFER_MEMORY_MODEL_RGB,
              8U, 16U, 8U, 8U, 8U, 0U),
          "1920x1080 padded framebuffer accepted");
    check((surface.width == HIGH_WIDTH) &&
          (surface.height == HIGH_HEIGHT) &&
          (surface.pitch == HIGH_PHYSICAL_PITCH) &&
          (surface.byte_size == HIGH_PHYSICAL_BYTES),
          "1920x1080 runtime geometry preserved");
    check(HIGH_PHYSICAL_PITCH > (uint64_t)HIGH_LOGICAL_STRIDE,
          "1920x1080 physical pitch exceeds visible row");
    check(HIGH_PHYSICAL_BYTES < (uint64_t)VMM_FRAMEBUFFER_WINDOW_SIZE,
          "1920x1080 padded framebuffer fits M61 mapping window");

    pages = page_span(TEST_FB_PHYSICAL, HIGH_PHYSICAL_BYTES);
    check((pages == 2161U) &&
          ((uint64_t)pages * VMM_PAGE_SIZE <=
           (uint64_t)VMM_FRAMEBUFFER_WINDOW_SIZE),
          "1920x1080 unaligned physical mapping page span bounded");
    check(page_span(TEST_FB_PHYSICAL, BASE_PHYSICAL_BYTES) == 488U,
          "800x600 unaligned physical mapping page span bounded");

    check(!boring_framebuffer_surface_init(
              &surface, &sentinel,
              HIGH_WIDTH, HIGH_HEIGHT,
              (uint64_t)HIGH_WIDTH * BORING_DISPLAY_BYTES_PER_PIXEL - 1ULL,
              32U, BORING_FRAMEBUFFER_MEMORY_MODEL_RGB,
              8U, 16U, 8U, 8U, 8U, 0U),
          "undersized physical pitch rejected");
    check(!boring_framebuffer_surface_init(
              &surface, &sentinel,
              1ULL, UINT64_MAX, 4ULL,
              32U, BORING_FRAMEBUFFER_MEMORY_MODEL_RGB,
              8U, 16U, 8U, 8U, 8U, 0U),
          "framebuffer byte-size overflow rejected");
}

static void display_geometry_tests(void) {
    struct boring_display_core core;
    struct boring_display_scanout_info info;
    uint8_t *guarded;
    uint8_t *pixels;
    size_t allocation;
    size_t index;

    info = scanout(BASE_WIDTH, BASE_HEIGHT);
    check(boring_display_core_init(&core, &info),
          "800x600 logical scanout accepted");
    check((core.width == BASE_WIDTH) && (core.height == BASE_HEIGHT) &&
          (core.stride == BASE_WIDTH * BORING_DISPLAY_BYTES_PER_PIXEL),
          "800x600 logical scanout geometry preserved");

    info = scanout(HIGH_WIDTH, HIGH_HEIGHT);
    check(boring_display_core_init(&core, &info),
          "1920x1080 logical scanout accepted");
    check((core.width == HIGH_WIDTH) && (core.height == HIGH_HEIGHT) &&
          (core.stride == HIGH_LOGICAL_STRIDE) &&
          (core.byte_size == HIGH_LOGICAL_BYTES),
          "1920x1080 logical composition sizing");
    check(HIGH_LOGICAL_BYTES <= BORING_DISPLAY_MAX_SCANOUT_BYTES,
          "1920x1080 composition fits display ABI limit");

    allocation = (size_t)core.byte_size + 2U * GUARD_BYTES;
    guarded = (uint8_t *)malloc(allocation);
    check(guarded != NULL, "allocate exact higher-geometry composition guard");
    if (guarded == NULL) {
        return;
    }
    (void)memset(guarded, 0xa5, allocation);
    pixels = guarded + GUARD_BYTES;
    check(boring_display_compose(&core, pixels, (size_t)core.byte_size),
          "1920x1080 full logical composition bounded");
    for (index = 0U; index < GUARD_BYTES; ++index) {
        check((guarded[index] == 0xa5U) &&
              (guarded[GUARD_BYTES + (size_t)core.byte_size + index] == 0xa5U),
              "1920x1080 composition guard preserved");
    }

    boring_display_cursor_move(&core, INT32_MAX, INT32_MAX);
    check((core.cursor_x == HIGH_WIDTH - 1U) &&
          (core.cursor_y == HIGH_HEIGHT - 1U),
          "cursor clamps to bottom-right at higher geometry");
    (void)memset(pixels, 0, (size_t)core.byte_size);
    boring_display_compose_cursor(&core, pixels);
    check(pixels[(size_t)core.byte_size - 4U] != 0U ||
          pixels[(size_t)core.byte_size - 3U] != 0U ||
          pixels[(size_t)core.byte_size - 2U] != 0U,
          "cursor draws final visible pixel without overrun");
    for (index = 0U; index < GUARD_BYTES; ++index) {
        check((guarded[index] == 0xa5U) &&
              (guarded[GUARD_BYTES + (size_t)core.byte_size + index] == 0xa5U),
              "bottom-right cursor guard preserved");
    }

    boring_display_cursor_move(&core, INT32_MIN, INT32_MIN);
    check((core.cursor_x == 0U) && (core.cursor_y == 0U),
          "cursor clamps to top-left at higher geometry");
    free(guarded);
}

static void wm_geometry_tests(void) {
    struct wm_core wm;
    uint32_t first = 0U;
    uint32_t second = 0U;
    uint32_t third = 0U;
    const struct wm_client *target;
    uint32_t x;
    uint32_t y;
    uint32_t index;

    check(wm_init(&wm, HIGH_WIDTH, HIGH_HEIGHT),
          "BoringWM accepts 1920x1080 runtime geometry");
    check(wm_add(&wm, 1U, 1ULL, 101U, &first) == BORING_WM_OK,
          "higher geometry add first window");
    check(wm_add(&wm, 2U, 2ULL, 102U, &second) == BORING_WM_OK,
          "higher geometry add second window");
    check(wm_add(&wm, 3U, 3ULL, 103U, &third) == BORING_WM_OK,
          "higher geometry add third window");
    check((first != 0U) && (second != 0U) && (third != 0U),
          "higher geometry window identities valid");

    for (index = 0U; index < wm.count; ++index) {
        const struct wm_client *client = &wm.clients[wm.order[index]];
        const struct wm_rect *rect = &client->rect;
        check((rect->x <= wm.width) && (rect->y <= wm.height) &&
              (rect->width <= wm.width - rect->x) &&
              (rect->height <= wm.height - rect->y),
              "higher geometry tile remains screen-bounded");
    }

    target = wm_lookup(&wm, second);
    check(target != NULL, "higher geometry secondary tile lookup");
    if (target == NULL) {
        return;
    }
    x = target->rect.x + target->rect.width / 2U;
    y = target->rect.y + target->rect.height / 2U;
    check(x > BASE_WIDTH,
          "higher geometry pointer test crosses former 800-pixel boundary");
    check(wm_pointer(&wm, x, y) && (wm.focus == second),
          "higher geometry pointer hit testing changes focus");
    check(!wm_pointer(&wm, HIGH_WIDTH, HIGH_HEIGHT),
          "pointer outside higher screen remains rejected");
}

int main(void) {
    framebuffer_geometry_tests();
    display_geometry_tests();
    wm_geometry_tests();

    if (failures != 0U) {
        (void)fprintf(stderr,
                      "m68-safe-higher-gop-host: %u failure(s)\n",
                      failures);
        return 1;
    }
    (void)puts("M68 safe higher GOP host geometry tests passed.");
    return 0;
}
