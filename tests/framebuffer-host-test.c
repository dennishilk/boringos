#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <boring/framebuffer.h>
#include <boring/graphics.h>
#include <boring/pixel_font.h>

#define TEST_WIDTH 8ULL
#define TEST_HEIGHT 6ULL
#define TEST_PITCH 40ULL
#define TEST_GUARD 32U
#define TEST_ACTIVE_BYTES ((size_t)(TEST_PITCH * TEST_HEIGHT))
#define TEST_STORAGE_BYTES (TEST_GUARD + TEST_ACTIVE_BYTES + TEST_GUARD)

static uint8_t storage[TEST_STORAGE_BYTES];

static void fail(const char *name) {
    (void)fprintf(stderr, "framebuffer-host-test: FAIL: %s\n", name);
}

static void fill_storage(uint8_t value) {
    size_t index;
    for (index = 0U; index < sizeof(storage); ++index) {
        storage[index] = value;
    }
}

static bool guards_equal(uint8_t value) {
    size_t index;
    for (index = 0U; index < TEST_GUARD; ++index) {
        if (storage[index] != value ||
            storage[TEST_GUARD + TEST_ACTIVE_BYTES + index] != value) {
            return false;
        }
    }
    return true;
}

static bool padding_equal(uint8_t value) {
    size_t row;
    size_t byte;
    for (row = 0U; row < (size_t)TEST_HEIGHT; ++row) {
        size_t base = TEST_GUARD + (row * (size_t)TEST_PITCH);
        for (byte = (size_t)(TEST_WIDTH * 4ULL);
             byte < (size_t)TEST_PITCH; ++byte) {
            if (storage[base + byte] != value) {
                return false;
            }
        }
    }
    return true;
}

static uint32_t read_pixel32(uint64_t x, uint64_t y) {
    size_t offset = TEST_GUARD + (size_t)(y * TEST_PITCH) + (size_t)(x * 4ULL);
    return (uint32_t)storage[offset] |
           ((uint32_t)storage[offset + 1U] << 8U) |
           ((uint32_t)storage[offset + 2U] << 16U) |
           ((uint32_t)storage[offset + 3U] << 24U);
}

static bool test_surface_validation(void) {
    struct boring_framebuffer surface;

    if (!boring_framebuffer_surface_init(
            &surface, &storage[TEST_GUARD], TEST_WIDTH, TEST_HEIGHT, TEST_PITCH,
            32U, BORING_FRAMEBUFFER_MEMORY_MODEL_RGB,
            8U, 16U, 8U, 8U, 8U, 0U)) {
        return false;
    }
    if (!boring_framebuffer_surface_valid(&surface)) {
        return false;
    }
    if (boring_framebuffer_surface_init(
            &surface, &storage[TEST_GUARD], 0ULL, TEST_HEIGHT, TEST_PITCH,
            32U, BORING_FRAMEBUFFER_MEMORY_MODEL_RGB,
            8U, 16U, 8U, 8U, 8U, 0U)) {
        return false;
    }
    if (boring_framebuffer_surface_init(
            &surface, &storage[TEST_GUARD], TEST_WIDTH, 0ULL, TEST_PITCH,
            32U, BORING_FRAMEBUFFER_MEMORY_MODEL_RGB,
            8U, 16U, 8U, 8U, 8U, 0U)) {
        return false;
    }
    if (boring_framebuffer_surface_init(
            &surface, &storage[TEST_GUARD], TEST_WIDTH, TEST_HEIGHT, TEST_PITCH,
            16U, BORING_FRAMEBUFFER_MEMORY_MODEL_RGB,
            5U, 11U, 6U, 5U, 5U, 0U)) {
        return false;
    }
    if (boring_framebuffer_surface_init(
            &surface, &storage[TEST_GUARD], TEST_WIDTH, TEST_HEIGHT, 31ULL,
            32U, BORING_FRAMEBUFFER_MEMORY_MODEL_RGB,
            8U, 16U, 8U, 8U, 8U, 0U)) {
        return false;
    }
    if (boring_framebuffer_surface_init(
            &surface, &storage[TEST_GUARD], UINT64_MAX, 2ULL, UINT64_MAX,
            32U, BORING_FRAMEBUFFER_MEMORY_MODEL_RGB,
            8U, 16U, 8U, 8U, 8U, 0U)) {
        return false;
    }
    if (boring_framebuffer_surface_init(
            &surface, &storage[TEST_GUARD], 1ULL, UINT64_MAX, 4ULL,
            32U, BORING_FRAMEBUFFER_MEMORY_MODEL_RGB,
            8U, 16U, 8U, 8U, 8U, 0U)) {
        return false;
    }
    return true;
}

static bool test_rgb_packing(void) {
    struct boring_framebuffer surface32;
    struct boring_framebuffer surface24;
    uint8_t tiny32[4];
    uint8_t tiny24[3];

    if (!boring_framebuffer_surface_init(
            &surface32, tiny32, 1ULL, 1ULL, 4ULL, 32U,
            BORING_FRAMEBUFFER_MEMORY_MODEL_RGB,
            8U, 16U, 8U, 8U, 8U, 0U) ||
        boring_color_pack(&surface32, 0x3aU, 0xcdU, 0xdcU) != 0x003acddcU) {
        return false;
    }
    if (!boring_framebuffer_surface_init(
            &surface24, tiny24, 1ULL, 1ULL, 3ULL, 24U,
            BORING_FRAMEBUFFER_MEMORY_MODEL_RGB,
            8U, 16U, 8U, 8U, 8U, 0U) ||
        boring_color_pack(&surface24, 0x72U, 0xd6U, 0x8aU) != 0x0072d68aU) {
        return false;
    }
    return true;
}

static bool test_primitives_and_canaries(void) {
    struct boring_framebuffer surface;
    uint32_t background;
    uint32_t accent;
    uint32_t text;
    uint8_t snapshot[TEST_STORAGE_BYTES];
    size_t index;

    fill_storage(0xa5U);
    if (!boring_framebuffer_surface_init(
            &surface, &storage[TEST_GUARD], TEST_WIDTH, TEST_HEIGHT, TEST_PITCH,
            32U, BORING_FRAMEBUFFER_MEMORY_MODEL_RGB,
            8U, 16U, 8U, 8U, 8U, 0U)) {
        return false;
    }
    background = boring_color_pack(&surface, 8U, 12U, 16U);
    accent = boring_color_pack(&surface, 58U, 205U, 220U);
    text = boring_color_pack(&surface, 232U, 239U, 242U);

    if (!boring_graphics_clear(&surface, background) ||
        read_pixel32(0ULL, 0ULL) != background ||
        read_pixel32(TEST_WIDTH - 1ULL, TEST_HEIGHT - 1ULL) != background ||
        !guards_equal(0xa5U) || !padding_equal(0xa5U)) {
        return false;
    }
    if (!boring_graphics_put_pixel(&surface, 0ULL, 0ULL, accent) ||
        !boring_graphics_put_pixel(&surface, TEST_WIDTH - 1ULL,
                                   TEST_HEIGHT - 1ULL, accent) ||
        read_pixel32(0ULL, 0ULL) != accent ||
        read_pixel32(TEST_WIDTH - 1ULL, TEST_HEIGHT - 1ULL) != accent) {
        return false;
    }
    if (boring_graphics_put_pixel(&surface, TEST_WIDTH, 0ULL, accent) ||
        boring_graphics_put_pixel(&surface, 0ULL, TEST_HEIGHT, accent)) {
        return false;
    }
    if (!boring_graphics_fill_rect(&surface, 6ULL, 4ULL, 10ULL, 10ULL, text) ||
        read_pixel32(6ULL, 4ULL) != text ||
        read_pixel32(7ULL, 5ULL) != text ||
        !guards_equal(0xa5U) || !padding_equal(0xa5U)) {
        return false;
    }
    for (index = 0U; index < sizeof(storage); ++index) {
        snapshot[index] = storage[index];
    }
    if (boring_graphics_fill_rect(&surface, 100ULL, 100ULL, 4ULL, 4ULL, text)) {
        return false;
    }
    for (index = 0U; index < sizeof(storage); ++index) {
        if (snapshot[index] != storage[index]) {
            return false;
        }
    }
    if (!boring_graphics_stroke_rect(&surface, 1ULL, 1ULL, 5ULL, 4ULL, accent) ||
        read_pixel32(1ULL, 1ULL) != accent ||
        read_pixel32(5ULL, 4ULL) != accent ||
        !boring_graphics_horizontal_line(&surface, 0ULL, 3ULL, TEST_WIDTH, text) ||
        !boring_graphics_vertical_line(&surface, 4ULL, 0ULL, TEST_HEIGHT, text) ||
        !guards_equal(0xa5U) || !padding_equal(0xa5U)) {
        return false;
    }
    return true;
}

static bool test_font(void) {
    enum { FONT_WIDTH = 48, FONT_HEIGHT = 24, FONT_PITCH = FONT_WIDTH * 4 };
    uint8_t guarded[16U + (size_t)FONT_PITCH * FONT_HEIGHT + 16U];
    struct boring_framebuffer surface;
    uint32_t foreground;
    size_t index;
    bool changed = false;

    for (index = 0U; index < sizeof(guarded); ++index) {
        guarded[index] = 0x5aU;
    }
    if (!boring_framebuffer_surface_init(
            &surface, &guarded[16], FONT_WIDTH, FONT_HEIGHT, FONT_PITCH,
            32U, BORING_FRAMEBUFFER_MEMORY_MODEL_RGB,
            8U, 16U, 8U, 8U, 8U, 0U)) {
        return false;
    }
    foreground = boring_color_pack(&surface, 232U, 239U, 242U);
    if (!boring_pixel_font_draw_char(&surface, 0ULL, 0ULL, 'A', foreground, 1U) ||
        !boring_pixel_font_draw_text(&surface, 8ULL, 0ULL, "aZ09", foreground) ||
        !boring_pixel_font_draw_text_scaled(&surface, 0ULL, 10ULL, "B/", foreground, 2U)) {
        return false;
    }
    for (index = 16U; index < sizeof(guarded) - 16U; ++index) {
        if (guarded[index] != 0x5aU) {
            changed = true;
            break;
        }
    }
    if (!changed) {
        return false;
    }
    for (index = 0U; index < 16U; ++index) {
        if (guarded[index] != 0x5aU || guarded[sizeof(guarded) - 1U - index] != 0x5aU) {
            return false;
        }
    }
    return true;
}

int main(void) {
    if (!test_surface_validation()) {
        fail("surface-validation");
        return 1;
    }
    if (!test_rgb_packing()) {
        fail("rgb-packing");
        return 1;
    }
    if (!test_primitives_and_canaries()) {
        fail("primitives-canaries");
        return 1;
    }
    if (!test_font()) {
        fail("font-clipping");
        return 1;
    }

    (void)puts("Native framebuffer renderer host tests passed.");
    return 0;
}
