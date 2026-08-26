#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/framebuffer.h>
#include <boring/graphics.h>
#include <boring/pixel_font.h>

/*
 * BoringOS 5x7. Authored for this repository for Milestone 30.
 * Each byte stores one five-pixel row in bits 4..0.
 */
static void boring_pixel_font_glyph(unsigned char character, uint8_t rows[7]) {
#define GLYPH(a,b,c,d,e,f,g) do { \
    rows[0]=(a); rows[1]=(b); rows[2]=(c); rows[3]=(d); \
    rows[4]=(e); rows[5]=(f); rows[6]=(g); \
} while (0)
    switch (character) {
        case ' ': GLYPH(0,0,0,0,0,0,0); break;
        case '!': GLYPH(4,4,4,4,4,0,4); break;
        case '"': GLYPH(10,10,10,0,0,0,0); break;
        case '#': GLYPH(10,31,10,10,31,10,0); break;
        case '$': GLYPH(4,15,20,14,5,30,4); break;
        case '%': GLYPH(24,25,2,4,8,19,3); break;
        case '&': GLYPH(12,18,20,8,21,18,13); break;
        case '\'': GLYPH(4,4,8,0,0,0,0); break;
        case '(': GLYPH(2,4,8,8,8,4,2); break;
        case ')': GLYPH(8,4,2,2,2,4,8); break;
        case '*': GLYPH(0,21,14,31,14,21,0); break;
        case '+': GLYPH(0,4,4,31,4,4,0); break;
        case ',': GLYPH(0,0,0,0,4,4,8); break;
        case '-': GLYPH(0,0,0,31,0,0,0); break;
        case '.': GLYPH(0,0,0,0,0,0,4); break;
        case '/': GLYPH(1,2,2,4,8,8,16); break;
        case '0': GLYPH(14,17,19,21,25,17,14); break;
        case '1': GLYPH(4,12,4,4,4,4,14); break;
        case '2': GLYPH(14,17,1,2,4,8,31); break;
        case '3': GLYPH(30,1,1,14,1,1,30); break;
        case '4': GLYPH(2,6,10,18,31,2,2); break;
        case '5': GLYPH(31,16,16,30,1,1,30); break;
        case '6': GLYPH(14,16,16,30,17,17,14); break;
        case '7': GLYPH(31,1,2,4,8,8,8); break;
        case '8': GLYPH(14,17,17,14,17,17,14); break;
        case '9': GLYPH(14,17,17,15,1,1,14); break;
        case ':': GLYPH(0,4,4,0,4,4,0); break;
        case ';': GLYPH(0,4,4,0,4,4,8); break;
        case '<': GLYPH(1,2,4,8,4,2,1); break;
        case '=': GLYPH(0,0,31,0,31,0,0); break;
        case '>': GLYPH(16,8,4,2,4,8,16); break;
        case '?': GLYPH(14,17,1,2,4,0,4); break;
        case '@': GLYPH(14,17,23,21,23,16,14); break;
        case 'A': GLYPH(14,17,17,31,17,17,17); break;
        case 'B': GLYPH(30,17,17,30,17,17,30); break;
        case 'C': GLYPH(14,17,16,16,16,17,14); break;
        case 'D': GLYPH(28,18,17,17,17,18,28); break;
        case 'E': GLYPH(31,16,16,30,16,16,31); break;
        case 'F': GLYPH(31,16,16,30,16,16,16); break;
        case 'G': GLYPH(14,17,16,23,17,17,15); break;
        case 'H': GLYPH(17,17,17,31,17,17,17); break;
        case 'I': GLYPH(14,4,4,4,4,4,14); break;
        case 'J': GLYPH(7,2,2,2,2,18,12); break;
        case 'K': GLYPH(17,18,20,24,20,18,17); break;
        case 'L': GLYPH(16,16,16,16,16,16,31); break;
        case 'M': GLYPH(17,27,21,21,17,17,17); break;
        case 'N': GLYPH(17,25,21,19,17,17,17); break;
        case 'O': GLYPH(14,17,17,17,17,17,14); break;
        case 'P': GLYPH(30,17,17,30,16,16,16); break;
        case 'Q': GLYPH(14,17,17,17,21,18,13); break;
        case 'R': GLYPH(30,17,17,30,20,18,17); break;
        case 'S': GLYPH(15,16,16,14,1,1,30); break;
        case 'T': GLYPH(31,4,4,4,4,4,4); break;
        case 'U': GLYPH(17,17,17,17,17,17,14); break;
        case 'V': GLYPH(17,17,17,17,17,10,4); break;
        case 'W': GLYPH(17,17,17,21,21,21,10); break;
        case 'X': GLYPH(17,17,10,4,10,17,17); break;
        case 'Y': GLYPH(17,17,10,4,4,4,4); break;
        case 'Z': GLYPH(31,1,2,4,8,16,31); break;
        case '[': GLYPH(14,8,8,8,8,8,14); break;
        case '\\': GLYPH(16,8,8,4,2,2,1); break;
        case ']': GLYPH(14,2,2,2,2,2,14); break;
        case '^': GLYPH(4,10,17,0,0,0,0); break;
        case '_': GLYPH(0,0,0,0,0,0,31); break;
        case '`': GLYPH(8,4,0,0,0,0,0); break;
        case 'a': GLYPH(0,0,14,1,15,17,15); break;
        case 'b': GLYPH(16,16,22,25,17,17,30); break;
        case 'c': GLYPH(0,0,14,17,16,17,14); break;
        case 'd': GLYPH(1,1,13,19,17,17,15); break;
        case 'e': GLYPH(0,0,14,17,31,16,14); break;
        case 'f': GLYPH(6,8,8,30,8,8,8); break;
        case 'g': GLYPH(0,0,15,17,15,1,14); break;
        case 'h': GLYPH(16,16,22,25,17,17,17); break;
        case 'i': GLYPH(4,0,12,4,4,4,14); break;
        case 'j': GLYPH(2,0,6,2,2,18,12); break;
        case 'k': GLYPH(16,16,18,20,24,20,18); break;
        case 'l': GLYPH(12,4,4,4,4,4,14); break;
        case 'm': GLYPH(0,0,26,21,21,21,21); break;
        case 'n': GLYPH(0,0,22,25,17,17,17); break;
        case 'o': GLYPH(0,0,14,17,17,17,14); break;
        case 'p': GLYPH(0,0,30,17,30,16,16); break;
        case 'q': GLYPH(0,0,15,17,15,1,1); break;
        case 'r': GLYPH(0,0,22,25,16,16,16); break;
        case 's': GLYPH(0,0,15,16,14,1,30); break;
        case 't': GLYPH(8,8,30,8,8,9,6); break;
        case 'u': GLYPH(0,0,17,17,17,19,13); break;
        case 'v': GLYPH(0,0,17,17,17,10,4); break;
        case 'w': GLYPH(0,0,17,17,21,21,10); break;
        case 'x': GLYPH(0,0,17,10,4,10,17); break;
        case 'y': GLYPH(0,0,17,17,15,1,14); break;
        case 'z': GLYPH(0,0,31,2,4,8,31); break;
        case '{': GLYPH(2,4,4,8,4,4,2); break;
        case '|': GLYPH(4,4,4,4,4,4,4); break;
        case '}': GLYPH(8,4,4,2,4,4,8); break;
        case '~': GLYPH(0,0,9,22,0,0,0); break;
        default: GLYPH(14,17,1,2,4,0,4); break;
    }
#undef GLYPH
}

bool boring_pixel_font_draw_char(const struct boring_framebuffer *surface,
                                 uint64_t x,
                                 uint64_t y,
                                 char character,
                                 uint32_t color,
                                 uint8_t scale) {
    uint8_t rows[7];
    uint8_t row;
    uint8_t column;
    bool wrote = false;

    if (!boring_framebuffer_surface_valid(surface) ||
        (scale == 0U) || (scale > 4U)) {
        return false;
    }

    boring_pixel_font_glyph((unsigned char)character, rows);
    for (row = 0U; row < BORING_PIXEL_FONT_HEIGHT; ++row) {
        for (column = 0U; column < BORING_PIXEL_FONT_WIDTH; ++column) {
            if ((rows[row] & (uint8_t)(1U << (4U - column))) != 0U) {
                uint64_t x_offset = (uint64_t)column * (uint64_t)scale;
                uint64_t y_offset = (uint64_t)row * (uint64_t)scale;
                uint64_t pixel_x;
                uint64_t pixel_y;

                if ((x > UINT64_MAX - x_offset) ||
                    (y > UINT64_MAX - y_offset)) {
                    continue;
                }
                pixel_x = x + x_offset;
                pixel_y = y + y_offset;
                wrote = boring_graphics_fill_rect(surface,
                                                   pixel_x,
                                                   pixel_y,
                                                   (uint64_t)scale,
                                                   (uint64_t)scale,
                                                   color) || wrote;
            }
        }
    }
    return wrote || (character == ' ');
}

bool boring_pixel_font_draw_text_scaled(
    const struct boring_framebuffer *surface,
    uint64_t x,
    uint64_t y,
    const char *text,
    uint32_t color,
    uint8_t scale) {
    uint64_t cursor_x = x;
    uint64_t cursor_y = y;
    uint64_t advance;
    uint64_t line_height;
    size_t index = 0U;
    bool accepted = false;

    if (!boring_framebuffer_surface_valid(surface) ||
        (text == NULL) || (scale == 0U) || (scale > 4U)) {
        return false;
    }

    advance = (uint64_t)BORING_PIXEL_FONT_ADVANCE * (uint64_t)scale;
    line_height = (uint64_t)BORING_PIXEL_FONT_LINE_HEIGHT * (uint64_t)scale;
    while (text[index] != '\0') {
        if (text[index] == '\n') {
            cursor_x = x;
            if (cursor_y > UINT64_MAX - line_height) {
                return accepted;
            }
            cursor_y += line_height;
        } else {
            (void)boring_pixel_font_draw_char(
                surface, cursor_x, cursor_y, text[index], color, scale);
            accepted = true;
            if (cursor_x > UINT64_MAX - advance) {
                return accepted;
            }
            cursor_x += advance;
        }
        ++index;
    }
    return accepted;
}

bool boring_pixel_font_draw_text(const struct boring_framebuffer *surface,
                                 uint64_t x,
                                 uint64_t y,
                                 const char *text,
                                 uint32_t color) {
    return boring_pixel_font_draw_text_scaled(surface, x, y, text, color, 1U);
}
