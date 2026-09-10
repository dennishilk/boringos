#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include <boring/wm.h>

#include "../user/boring-display/managed.h"

#define WIDTH 800U
#define HEIGHT 600U
#define ITERATIONS 100U

static uint8_t frame[WIDTH * HEIGHT * BORING_DISPLAY_BYTES_PER_PIXEL];

int main(void) {
    struct boring_display_core core;
    struct boring_display_cursor_damage damage;
    struct display_managed managed;
    const struct boring_display_scanout_info info = {
        BORING_DISPLAY_SCANOUT_VERSION,
        WIDTH,
        HEIGHT,
        WIDTH * BORING_DISPLAY_BYTES_PER_PIXEL,
        (uint64_t)WIDTH * HEIGHT * BORING_DISPLAY_BYTES_PER_PIXEL
    };
    clock_t start;
    clock_t finish;
    clock_t damage_start;
    clock_t damage_finish;
    clock_t focus_start;
    clock_t focus_finish;
    uint64_t damage_present_pixels = 0ULL;
    uint64_t focus_present_pixels = 0ULL;
    uint64_t focus_present_regions = 0ULL;
    uint32_t index;

    if (!boring_display_core_init(&core, &info)) {
        return 1;
    }
    display_managed_init(&managed);
    start = clock();
    for (index = 0U; index < ITERATIONS; ++index) {
        if (!display_managed_compose(&managed, &core, frame, sizeof(frame))) {
            return 1;
        }
    }
    finish = clock();
    if (!display_managed_compose_scene(
            &managed, &core, frame, sizeof(frame))) {
        return 1;
    }
    boring_display_cursor_damage_init(&damage);
    if (!boring_display_cursor_damage_reset(
            &damage, &core, frame, sizeof(frame))) {
        return 1;
    }
    damage_start = clock();
    for (index = 0U; index < ITERATIONS; ++index) {
        struct boring_display_region old_region;
        struct boring_display_region new_region;

        if (!boring_display_cursor_damage_move(
                &damage, &core, frame, sizeof(frame), 1, 0,
                &old_region, &new_region)) {
            return 1;
        }
        damage_present_pixels +=
            (uint64_t)old_region.width * (uint64_t)old_region.height +
            (uint64_t)new_region.width * (uint64_t)new_region.height;
    }
    damage_finish = clock();
    if ((damage.moves != ITERATIONS) ||
        (damage.restored_pixels != 7200ULL) ||
        (damage_present_pixels != 14400ULL) ||
        (core.cursor_x != 500U) || (core.cursor_y != 300U)) {
        return 1;
    }
    core.surfaces[0].active = true;
    core.surfaces[0].token = 1U;
    core.surfaces[0].pixels = frame;
    core.surfaces[1].active = true;
    core.surfaces[1].token = 2U;
    core.surfaces[1].pixels = frame;
    managed.placements[0] = (struct display_placement){
        1U, 257U, 4U, 4U, 470U, 592U, 3U,
        BORING_WM_FOCUSED, 0U, 1ULL, true, true
    };
    managed.placements[1] = (struct display_placement){
        2U, 258U, 482U, 4U, 314U, 592U, 3U,
        BORING_WM_UNFOCUSED, 1U, 2ULL, true, true
    };
    focus_start = clock();
    for (index = 0U; index < ITERATIONS; ++index) {
        struct boring_display_region regions[BORING_DISPLAY_FOCUS_REGION_MAX];
        size_t region_count = 0U;
        uint64_t pixel_count = 0ULL;

        managed.placements[0].color = (index & 1U) != 0U ?
            BORING_WM_FOCUSED : BORING_WM_UNFOCUSED;
        managed.placements[1].color = (index & 1U) != 0U ?
            BORING_WM_UNFOCUSED : BORING_WM_FOCUSED;
        if (!display_managed_compose_focus_borders(
                &managed, &core, frame, sizeof(frame), regions,
                BORING_DISPLAY_FOCUS_REGION_MAX, &region_count,
                &pixel_count) ||
            (region_count != 8U) || (pixel_count != 11736ULL)) {
            return 1;
        }
        focus_present_regions += region_count;
        focus_present_pixels += pixel_count;
    }
    focus_finish = clock();
    if ((focus_present_regions != 800ULL) ||
        (focus_present_pixels != 1173600ULL)) {
        return 1;
    }
    (void)printf("BASELINE_MOUSE_MOVES=%u\n", ITERATIONS);
    (void)printf("BASELINE_FULL_COMPOSITIONS=%u\n", ITERATIONS);
    (void)printf("BASELINE_COMPOSE_PIXELS=%llu\n",
                 (unsigned long long)WIDTH * HEIGHT * ITERATIONS);
    (void)printf("BASELINE_FRAMEBUFFER_COPY_PIXELS=%llu\n",
                 (unsigned long long)WIDTH * HEIGHT * ITERATIONS);
    (void)printf("BASELINE_HOST_COMPOSE_CLOCK_TICKS=%llu\n",
                 (unsigned long long)(finish - start));
    (void)printf("FIX_CURSOR_DAMAGE_MOVES=%llu\n",
                 (unsigned long long)damage.moves);
    (void)printf("FIX_CURSOR_RESTORE_PIXELS=%llu\n",
                 (unsigned long long)damage.restored_pixels);
    (void)printf("FIX_FRAMEBUFFER_REGION_PIXELS=%llu\n",
                 (unsigned long long)damage_present_pixels);
    (void)printf("FIX_HOST_DAMAGE_CLOCK_TICKS=%llu\n",
                 (unsigned long long)(damage_finish - damage_start));
    (void)printf("BASELINE_FOCUS_TRANSITIONS=%u\n", ITERATIONS);
    (void)printf("BASELINE_FOCUS_FULL_COMPOSITIONS=%u\n", ITERATIONS);
    (void)printf("BASELINE_FOCUS_COMPOSE_PIXELS=%llu\n",
                 (unsigned long long)WIDTH * HEIGHT * ITERATIONS);
    (void)printf("BASELINE_FOCUS_FRAMEBUFFER_COPY_PIXELS=%llu\n",
                 (unsigned long long)WIDTH * HEIGHT * ITERATIONS);
    (void)printf("FIX_FOCUS_BORDER_REGIONS=%llu\n",
                 (unsigned long long)focus_present_regions);
    (void)printf("FIX_FOCUS_BORDER_PIXELS=%llu\n",
                 (unsigned long long)focus_present_pixels);
    (void)printf("FIX_HOST_FOCUS_BORDER_CLOCK_TICKS=%llu\n",
                 (unsigned long long)(focus_finish - focus_start));
    return 0;
}
