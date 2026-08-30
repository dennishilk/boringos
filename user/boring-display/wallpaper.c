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

static void pixel(uint8_t *output, size_t offset, uint8_t red,
                  uint8_t green, uint8_t blue) {
    output[offset] = blue;
    output[offset + 1U] = green;
    output[offset + 2U] = red;
    output[offset + 3U] = 0U;
}

static void background(uint8_t *output, uint32_t stride) {
    uint32_t y;
    for (y = 0U; y < BORING_WALLPAPER_HEIGHT; ++y) {
        uint32_t x;
        const uint32_t vertical_distance = y > 365U ? y - 365U : 365U - y;
        for (x = 0U; x < BORING_WALLPAPER_WIDTH; ++x) {
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
            pixel(output, (size_t)y * stride + (size_t)x * 4U,
                  (uint8_t)(glow + noise),
                  (uint8_t)(glow + noise + 1U),
                  (uint8_t)(glow + noise + 3U));
        }
    }
}

static void logo(uint8_t *output, uint32_t stride) {
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
                            const size_t offset = (size_t)(529U + row * 2U + yy) * stride +
                                (size_t)(x + column * 2U + xx) * 4U;
                            if (index < 6U) { pixel(output, offset, 98U, 96U, 100U); }
                            else { pixel(output, offset, 76U, 75U, 80U); }
                        }
                    }
                }
            }
        }
        x += character == ' ' ? 8U : 12U;
    }
}

bool display_wallpaper_compose(const struct boring_display_core *core,
                               uint8_t *output, size_t size) {
    if ((core == NULL) || (output == NULL) || (size != core->byte_size) ||
        (core->width != BORING_WALLPAPER_WIDTH) ||
        (core->height != BORING_WALLPAPER_HEIGHT) ||
        (core->stride < BORING_WALLPAPER_WIDTH * 4U)) {
        return false;
    }
    background(output, core->stride);
    logo(output, core->stride);
    return true;
}
