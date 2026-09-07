#include "render.h"

static void terminal_glyph(unsigned char character, uint8_t rows[7]) {
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


static void put_pixel(uint8_t *pixels, uint32_t stride, uint32_t x, uint32_t y, uint32_t color) {
    const size_t offset = (size_t)y * (size_t)stride + (size_t)x * 4U;
    pixels[offset] = (uint8_t)color;
    pixels[offset + 1U] = (uint8_t)(color >> 8U);
    pixels[offset + 2U] = (uint8_t)(color >> 16U);
    pixels[offset + 3U] = 0U;
}

bool boring_terminal_geometry(uint32_t width, uint32_t height,
                              uint32_t *cols_out, uint32_t *rows_out) {
    uint32_t cols;
    uint32_t rows;
    if ((cols_out == NULL) || (rows_out == NULL) ||
        (width <= 2U * BORING_TERMINAL_MARGIN_X) ||
        (height <= 2U * BORING_TERMINAL_MARGIN_Y)) {
        return false;
    }
    cols = (width - 2U * BORING_TERMINAL_MARGIN_X) / BORING_TERMINAL_CELL_WIDTH;
    rows = (height - 2U * BORING_TERMINAL_MARGIN_Y) / BORING_TERMINAL_CELL_HEIGHT;
    if ((cols == 0U) || (rows == 0U)) {
        return false;
    }
    if (cols > BORING_TERMINAL_MAX_COLS) {
        cols = BORING_TERMINAL_MAX_COLS;
    }
    if (rows > BORING_TERMINAL_MAX_ROWS) {
        rows = BORING_TERMINAL_MAX_ROWS;
    }
    *cols_out = cols;
    *rows_out = rows;
    return true;
}

bool boring_terminal_render(const struct boring_terminal *terminal,
                            uint8_t *pixels,
                            uint32_t surface_width,
                            uint32_t surface_height,
                            uint32_t stride,
                            uint32_t view_width,
                            uint32_t view_height) {
    uint32_t row;
    uint32_t col;
    uint32_t expected_cols;
    uint32_t expected_rows;
    if ((terminal == NULL) || (pixels == NULL) || (surface_width == 0U) ||
        (surface_height == 0U) || (stride < surface_width * 4U) ||
        (view_width > surface_width) || (view_height > surface_height) ||
        !boring_terminal_geometry(view_width, view_height, &expected_cols, &expected_rows) ||
        (terminal->cols != expected_cols) || (terminal->rows != expected_rows)) {
        return false;
    }
    for (row = 0U; row < view_height; ++row) {
        for (col = 0U; col < view_width; ++col) {
            put_pixel(pixels, stride, col, row, BORING_TERMINAL_BACKGROUND);
        }
    }
    for (row = 0U; row < terminal->rows; ++row) {
        for (col = 0U; col < terminal->cols; ++col) {
            uint8_t glyph[7];
            uint32_t glyph_row;
            const uint32_t origin_x = BORING_TERMINAL_MARGIN_X +
                col * BORING_TERMINAL_CELL_WIDTH;
            const uint32_t origin_y = BORING_TERMINAL_MARGIN_Y +
                row * BORING_TERMINAL_CELL_HEIGHT;
            terminal_glyph((unsigned char)terminal->cells[row][col], glyph);
            for (glyph_row = 0U; glyph_row < BORING_TERMINAL_GLYPH_HEIGHT; ++glyph_row) {
                uint32_t glyph_col;
                for (glyph_col = 0U; glyph_col < BORING_TERMINAL_GLYPH_WIDTH; ++glyph_col) {
                    if ((glyph[glyph_row] & (uint8_t)(1U << (4U - glyph_col))) != 0U) {
                        const uint32_t x = origin_x + glyph_col;
                        const uint32_t y = origin_y + glyph_row;
                        if ((x < view_width) && (y < view_height)) {
                            put_pixel(pixels, stride, x, y, BORING_TERMINAL_FOREGROUND);
                        }
                    }
                }
            }
        }
    }
    if ((terminal->cursor_col < terminal->cols) && (terminal->cursor_row < terminal->rows)) {
        const uint32_t y = BORING_TERMINAL_MARGIN_Y +
            terminal->cursor_row * BORING_TERMINAL_CELL_HEIGHT + BORING_TERMINAL_GLYPH_HEIGHT;
        const uint32_t origin_x = BORING_TERMINAL_MARGIN_X +
            terminal->cursor_col * BORING_TERMINAL_CELL_WIDTH;
        for (col = 0U; col < BORING_TERMINAL_GLYPH_WIDTH; ++col) {
            if ((origin_x + col < view_width) && (y < view_height)) {
                put_pixel(pixels, stride, origin_x + col, y, BORING_TERMINAL_CURSOR);
            }
        }
    }
    return true;
}
