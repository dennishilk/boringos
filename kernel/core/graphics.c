#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/framebuffer.h>
#include <boring/graphics.h>
#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
#include <boring/io.h>
#endif

#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
#define M61_NORMAL_FRAMEBUFFER_POST_PORT 0x80U
#define M61_NORMAL_FRAMEBUFFER_PRE_POST 0x90U
#define M61_NORMAL_FRAMEBUFFER_POST_POST 0x91U
static bool m61_first_normal_framebuffer_store_pending = true;
#endif

static uint32_t boring_color_scale(uint8_t value, uint8_t bits) {
    uint32_t maximum;

    if ((bits == 0U) || (bits > 8U)) {
        return 0U;
    }
    maximum = (1U << bits) - 1U;
    return ((uint32_t)value * maximum + 127U) / 255U;
}

uint32_t boring_color_pack(const struct boring_framebuffer *surface,
                           uint8_t red,
                           uint8_t green,
                           uint8_t blue) {
    uint32_t packed = 0U;

    if (!boring_framebuffer_surface_valid(surface)) {
        return 0U;
    }

    packed |= boring_color_scale(red, surface->red_mask_size)
              << surface->red_mask_shift;
    packed |= boring_color_scale(green, surface->green_mask_size)
              << surface->green_mask_shift;
    packed |= boring_color_scale(blue, surface->blue_mask_size)
              << surface->blue_mask_shift;
    return packed;
}

static void boring_graphics_store_pixel(const struct boring_framebuffer *surface,
                                        uint64_t x,
                                        uint64_t y,
                                        uint32_t color) {
    uint64_t offset = (y * surface->pitch) +
                      (x * (uint64_t)surface->bytes_per_pixel);
    uint8_t byte_index;
#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
    bool first_normal_store = false;

    if (m61_first_normal_framebuffer_store_pending) {
        m61_first_normal_framebuffer_store_pending = false;
        first_normal_store = true;
        x86_64_out8((uint16_t)M61_NORMAL_FRAMEBUFFER_POST_PORT,
                    (uint8_t)M61_NORMAL_FRAMEBUFFER_PRE_POST);
    }
#endif

    for (byte_index = 0U; byte_index < surface->bytes_per_pixel; ++byte_index) {
        surface->address[offset + (uint64_t)byte_index] =
            (uint8_t)((color >> ((uint32_t)byte_index * 8U)) & 0xffU);
    }
#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
    if (first_normal_store) {
        x86_64_out8((uint16_t)M61_NORMAL_FRAMEBUFFER_POST_PORT,
                    (uint8_t)M61_NORMAL_FRAMEBUFFER_POST_POST);
    }
#endif
}

bool boring_graphics_put_pixel(const struct boring_framebuffer *surface,
                               uint64_t x,
                               uint64_t y,
                               uint32_t color) {
    if (!boring_framebuffer_surface_valid(surface) ||
        (x >= surface->width) || (y >= surface->height)) {
        return false;
    }
    boring_graphics_store_pixel(surface, x, y, color);
    return true;
}

bool boring_graphics_fill_rect(const struct boring_framebuffer *surface,
                               uint64_t x,
                               uint64_t y,
                               uint64_t width,
                               uint64_t height,
                               uint32_t color) {
    uint64_t end_x;
    uint64_t end_y;
    uint64_t row;
    uint64_t column;

    if (!boring_framebuffer_surface_valid(surface) ||
        (width == 0ULL) || (height == 0ULL) ||
        (x >= surface->width) || (y >= surface->height)) {
        return false;
    }

    end_x = (width > (surface->width - x)) ? surface->width : (x + width);
    end_y = (height > (surface->height - y)) ? surface->height : (y + height);

    for (row = y; row < end_y; ++row) {
        for (column = x; column < end_x; ++column) {
            boring_graphics_store_pixel(surface, column, row, color);
        }
    }
    return true;
}

bool boring_graphics_horizontal_line(const struct boring_framebuffer *surface,
                                     uint64_t x,
                                     uint64_t y,
                                     uint64_t length,
                                     uint32_t color) {
    return boring_graphics_fill_rect(surface, x, y, length, 1ULL, color);
}

bool boring_graphics_vertical_line(const struct boring_framebuffer *surface,
                                   uint64_t x,
                                   uint64_t y,
                                   uint64_t length,
                                   uint32_t color) {
    return boring_graphics_fill_rect(surface, x, y, 1ULL, length, color);
}

bool boring_graphics_stroke_rect(const struct boring_framebuffer *surface,
                                 uint64_t x,
                                 uint64_t y,
                                 uint64_t width,
                                 uint64_t height,
                                 uint32_t color) {
    uint64_t clipped_width;
    uint64_t clipped_height;
    uint64_t right;
    uint64_t bottom;
    bool wrote = false;

    if (!boring_framebuffer_surface_valid(surface) ||
        (width == 0ULL) || (height == 0ULL) ||
        (x >= surface->width) || (y >= surface->height)) {
        return false;
    }

    clipped_width = (width > (surface->width - x)) ?
                    (surface->width - x) : width;
    clipped_height = (height > (surface->height - y)) ?
                     (surface->height - y) : height;
    right = x + clipped_width - 1ULL;
    bottom = y + clipped_height - 1ULL;

    wrote = boring_graphics_horizontal_line(
                surface, x, y, clipped_width, color) || wrote;
    if (clipped_height > 1ULL) {
        wrote = boring_graphics_horizontal_line(
                    surface, x, bottom, clipped_width, color) || wrote;
    }
    wrote = boring_graphics_vertical_line(
                surface, x, y, clipped_height, color) || wrote;
    if (clipped_width > 1ULL) {
        wrote = boring_graphics_vertical_line(
                    surface, right, y, clipped_height, color) || wrote;
    }
    return wrote;
}

bool boring_graphics_clear(const struct boring_framebuffer *surface,
                           uint32_t color) {
    if (!boring_framebuffer_surface_valid(surface)) {
        return false;
    }
    return boring_graphics_fill_rect(
        surface, 0ULL, 0ULL, surface->width, surface->height, color);
}
