#include <stdint.h>
#include <stdio.h>
#include <time.h>

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
    uint64_t damage_present_pixels = 0ULL;
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
    return 0;
}
