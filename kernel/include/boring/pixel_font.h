#ifndef BORING_PIXEL_FONT_H
#define BORING_PIXEL_FONT_H

#include <stdbool.h>
#include <stdint.h>

#include <boring/framebuffer.h>

#define BORING_PIXEL_FONT_WIDTH 5U
#define BORING_PIXEL_FONT_HEIGHT 7U
#define BORING_PIXEL_FONT_ADVANCE 6U
#define BORING_PIXEL_FONT_LINE_HEIGHT 8U

bool boring_pixel_font_draw_char(const struct boring_framebuffer *surface,
                                 uint64_t x,
                                 uint64_t y,
                                 char character,
                                 uint32_t color,
                                 uint8_t scale);
bool boring_pixel_font_draw_text(const struct boring_framebuffer *surface,
                                 uint64_t x,
                                 uint64_t y,
                                 const char *text,
                                 uint32_t color);
bool boring_pixel_font_draw_text_scaled(
    const struct boring_framebuffer *surface,
    uint64_t x,
    uint64_t y,
    const char *text,
    uint32_t color,
    uint8_t scale);

#endif
