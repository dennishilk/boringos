#ifndef BORING_GRAPHICS_H
#define BORING_GRAPHICS_H

#include <stdbool.h>
#include <stdint.h>

#include <boring/framebuffer.h>

uint32_t boring_color_pack(const struct boring_framebuffer *surface,
                           uint8_t red,
                           uint8_t green,
                           uint8_t blue);

bool boring_graphics_clear(const struct boring_framebuffer *surface,
                           uint32_t color);
bool boring_graphics_put_pixel(const struct boring_framebuffer *surface,
                               uint64_t x,
                               uint64_t y,
                               uint32_t color);
bool boring_graphics_fill_rect(const struct boring_framebuffer *surface,
                               uint64_t x,
                               uint64_t y,
                               uint64_t width,
                               uint64_t height,
                               uint32_t color);
bool boring_graphics_stroke_rect(const struct boring_framebuffer *surface,
                                 uint64_t x,
                                 uint64_t y,
                                 uint64_t width,
                                 uint64_t height,
                                 uint32_t color);
bool boring_graphics_horizontal_line(const struct boring_framebuffer *surface,
                                     uint64_t x,
                                     uint64_t y,
                                     uint64_t length,
                                     uint32_t color);
bool boring_graphics_vertical_line(const struct boring_framebuffer *surface,
                                   uint64_t x,
                                   uint64_t y,
                                   uint64_t length,
                                   uint32_t color);

#endif
