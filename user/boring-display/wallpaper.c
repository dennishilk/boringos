#include "wallpaper.h"

/* Faithful native rendering of the black textured boringwm wallpaper and its
 * "boring by design." mark. Visual source:
 * dennishilk/boringwm assets/wallpaper/boringwm-wallpaper.png
 * SHA256 d7055a1ba8cbf8462af4bd54ab14bb51c1bed7c070d9d12c3f89e8608adff416.
 * The deterministic integer renderer keeps all PNG parsing out of BoringOS. */
static uint8_t glyph_row(char character, uint32_t row) {
    static const uint8_t b[7] = {16U, 16U, 30U, 17U, 17U, 17U, 30U};
    static const uint8_t d[7] = {1U, 1U, 15U, 17U, 17U, 17U, 15U};
    static const uint8_t e[7] = {0U, 0U, 14U, 17U, 31U, 16U, 15U};
    static const uint8_t g[7] = {0U, 0U, 15U, 17U, 15U, 1U, 14U};
    static const uint8_t i[7] = {4U, 0U, 12U, 4U, 4U, 4U, 14U};
    static const uint8_t n[7] = {0U, 0U, 30U, 17U, 17U, 17U, 17U};
    static const uint8_t o[7] = {0U, 0U, 14U, 17U, 17U, 17U, 14U};
    static const uint8_t r[7] = {0U, 0U, 22U, 25U, 16U, 16U, 16U};
    static const uint8_t s[7] = {0U, 0U, 15U, 16U, 14U, 1U, 30U};
    static const uint8_t y[7] = {0U, 0U, 17U, 17U, 15U, 1U, 14U};
    const uint8_t *glyph = NULL;
    if (row >= 7U) { return 0U; }
    switch (character) {
        case 'b': glyph = b; break;
        case 'd': glyph = d; break;
        case 'e': glyph = e; break;
        case 'g': glyph = g; break;
        case 'i': glyph = i; break;
        case 'n': glyph = n; break;
        case 'o': glyph = o; break;
        case 'r': glyph = r; break;
        case 's': glyph = s; break;
        case 'y': glyph = y; break;
        case '.': return row == 6U ? 4U : 0U;
        default: return 0U;
    }
    return glyph[row];
}

static bool arguments_valid(const struct boring_display_core *core,
                            const uint8_t *output, size_t size) {
    uint64_t minimum_stride;
    if ((core == NULL) || (output == NULL) || (core->width == 0U) ||
        (core->height == 0U) || (core->byte_size > (uint64_t)SIZE_MAX)) {
        return false;
    }
    minimum_stride = (uint64_t)core->width *
                     (uint64_t)BORING_DISPLAY_BYTES_PER_PIXEL;
    return ((uint64_t)core->stride >= minimum_stride) &&
           (core->byte_size ==
            (uint64_t)core->stride * (uint64_t)core->height) &&
           (size == (size_t)core->byte_size);
}

static bool pixel(const struct boring_display_core *core,
                  uint8_t *output, size_t size,
                  uint32_t x, uint32_t y,
                  uint8_t red, uint8_t green, uint8_t blue) {
    size_t row_offset;
    size_t pixel_offset;

    if (!arguments_valid(core, output, size) ||
        (x >= core->width) || (y >= core->height) ||
        ((size_t)y > SIZE_MAX / (size_t)core->stride)) {
        return false;
    }
    row_offset = (size_t)y * (size_t)core->stride;
    if ((size_t)x > (SIZE_MAX - row_offset) / 4U) {
        return false;
    }
    pixel_offset = row_offset + ((size_t)x * 4U);
    if (pixel_offset > size - 4U) {
        return false;
    }
    output[pixel_offset] = blue;
    output[pixel_offset + 1U] = green;
    output[pixel_offset + 2U] = red;
    output[pixel_offset + 3U] = 0U;
    return true;
}

static bool point_in_region(const struct boring_display_region *region,
                            uint32_t x, uint32_t y) {
    return (x >= region->x) && (y >= region->y) &&
           ((uint64_t)x < (uint64_t)region->x + region->width) &&
           ((uint64_t)y < (uint64_t)region->y + region->height);
}

static bool background_region(const struct boring_display_core *core,
                              uint8_t *output, size_t size,
                              const struct boring_display_region *region) {
    uint32_t y;
    for (y = region->y; y < region->y + region->height; ++y) {
        uint32_t x;
        const uint32_t vertical_distance = y > 365U ? y - 365U : 365U - y;
        for (x = region->x; x < region->x + region->width; ++x) {
            uint32_t glow = 0U;
            uint32_t noise;
            uint32_t hash = x * 0x45d9f3bU;
            hash ^= y * 0x119de1f3U;
            hash ^= hash >> 16U;
            if ((x < 520U) && (vertical_distance < 300U)) {
                glow = ((520U - x) * (300U - vertical_distance) * 18U) /
                    (520U * 300U);
            }
            noise = (hash >> 29U) & 3U;
            if ((hash & 0x1fffU) == 0U) { noise += 9U; }
            if (glow + noise > 29U) { noise = 29U - glow; }
            if (!pixel(core, output, size, x, y,
                       (uint8_t)(glow + noise),
                       (uint8_t)(glow + noise + 1U),
                       (uint8_t)(glow + noise + 3U))) {
                return false;
            }
        }
    }
    return true;
}

static bool logo_region(const struct boring_display_core *core,
                        uint8_t *output, size_t size,
                        const struct boring_display_region *region) {
    static const char mark[] = "boring by design.";
    uint32_t x = 596U;
    size_t index;
    for (index = 0U; index < sizeof(mark) - 1U; ++index) {
        const char character = mark[index];
        uint32_t row;
        for (row = 0U; row < 7U; ++row) {
            const uint8_t bits = glyph_row(character, row);
            uint32_t column;
            for (column = 0U; column < 5U; ++column) {
                if ((bits & (uint8_t)(1U << (4U - column))) != 0U) {
                    uint32_t yy;
                    for (yy = 0U; yy < 2U; ++yy) {
                        uint32_t xx;
                        for (xx = 0U; xx < 2U; ++xx) {
                            const uint32_t pixel_x = x + column * 2U + xx;
                            const uint32_t pixel_y = 529U + row * 2U + yy;
                            bool ready = true;
                            if (!point_in_region(region, pixel_x, pixel_y)) {
                                continue;
                            }
                            if ((pixel_x >= core->width) ||
                                (pixel_y >= core->height)) {
                                continue;
                            }
                            ready = index < 6U ?
                                pixel(core, output, size, pixel_x, pixel_y,
                                      98U, 96U, 100U) :
                                pixel(core, output, size, pixel_x, pixel_y,
                                      76U, 75U, 80U);
                            if (!ready) { return false; }
                        }
                    }
                }
            }
        }
        x += character == ' ' ? 8U : 12U;
    }
    return true;
}

bool display_wallpaper_compose_region(
    const struct boring_display_core *core,
    uint8_t *output, size_t size,
    const struct boring_display_region *region) {
    if (!arguments_valid(core, output, size) || (region == NULL) ||
        (region->width == 0U) || (region->height == 0U) ||
        (region->x >= core->width) || (region->y >= core->height) ||
        (region->width > core->width - region->x) ||
        (region->height > core->height - region->y)) {
        return false;
    }
    return background_region(core, output, size, region) &&
           logo_region(core, output, size, region);
}

bool display_wallpaper_compose(const struct boring_display_core *core,
                               uint8_t *output, size_t size) {
    struct boring_display_region region;
    if (!arguments_valid(core, output, size)) { return false; }
    region = (struct boring_display_region){0U, 0U, core->width, core->height};
    return display_wallpaper_compose_region(core, output, size, &region);
}