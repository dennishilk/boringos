#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../user/boring-terminal/render.h"

#define WIDTH 80U
#define HEIGHT 48U
#define STRIDE (WIDTH * 4U)
#define GUARD 32U

static void require(int condition, const char *message) {
    if (!condition) {
        (void)fprintf(stderr, "terminal-render-host-test: %s\n", message);
        exit(1);
    }
}

static uint32_t pixel(const uint8_t *pixels, uint32_t x, uint32_t y) {
    const size_t offset = (size_t)y * STRIDE + (size_t)x * 4U;
    return (uint32_t)pixels[offset] |
           ((uint32_t)pixels[offset + 1U] << 8U) |
           ((uint32_t)pixels[offset + 2U] << 16U);
}

int main(void) {
    uint8_t storage[GUARD + STRIDE * HEIGHT + GUARD];
    uint8_t *pixels = &storage[GUARD];
    struct boring_terminal terminal;
    uint32_t cols = 0U;
    uint32_t rows = 0U;
    size_t index;

    (void)memset(storage, 0xa5, sizeof(storage));
    require(boring_terminal_geometry(WIDTH, HEIGHT, &cols, &rows), "geometry failed");
    require(cols == 12U && rows == 5U, "geometry result wrong");
    require(boring_terminal_init(&terminal, cols, rows), "terminal init failed");
    boring_terminal_feed(&terminal, "$Az09~", 6U);
    require(boring_terminal_render(&terminal, pixels, WIDTH, HEIGHT, STRIDE, WIDTH, HEIGHT),
            "render failed");
    for (index = 0U; index < GUARD; ++index) {
        require(storage[index] == 0xa5U, "leading guard overwritten");
        require(storage[GUARD + STRIDE * HEIGHT + index] == 0xa5U,
                "trailing guard overwritten");
    }
    require(pixel(pixels, 0U, 0U) == BORING_TERMINAL_BACKGROUND,
            "background wrong");
    require(pixel(pixels, BORING_TERMINAL_MARGIN_X + 2U,
                  BORING_TERMINAL_MARGIN_Y) == BORING_TERMINAL_FOREGROUND,
            "dollar glyph missing");
    require(pixel(pixels, BORING_TERMINAL_MARGIN_X + 6U * 6U,
                  BORING_TERMINAL_MARGIN_Y + 7U) == BORING_TERMINAL_CURSOR,
            "cursor missing");
    require(!boring_terminal_render(&terminal, pixels, WIDTH, HEIGHT, WIDTH * 4U - 1U,
                                    WIDTH, HEIGHT), "short stride accepted");
    require(!boring_terminal_geometry(4U, 4U, &cols, &rows), "tiny geometry accepted");
    (void)puts("boring-terminal bitmap renderer host tests passed.");
    return 0;
}
