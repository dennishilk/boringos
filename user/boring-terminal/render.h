#ifndef BORING_TERMINAL_RENDER_H
#define BORING_TERMINAL_RENDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "terminal.h"

#define BORING_TERMINAL_GLYPH_WIDTH 5U
#define BORING_TERMINAL_GLYPH_HEIGHT 7U
#define BORING_TERMINAL_CELL_WIDTH 6U
#define BORING_TERMINAL_CELL_HEIGHT 8U
#define BORING_TERMINAL_MARGIN_X 4U
#define BORING_TERMINAL_MARGIN_Y 4U
#define BORING_TERMINAL_BACKGROUND 0x001d2021U
#define BORING_TERMINAL_FOREGROUND 0x00ebdbb2U
#define BORING_TERMINAL_CURSOR 0x0083a598U

bool boring_terminal_geometry(uint32_t width, uint32_t height,
                              uint32_t *cols_out, uint32_t *rows_out);
bool boring_terminal_render(const struct boring_terminal *terminal,
                            uint8_t *pixels,
                            uint32_t surface_width,
                            uint32_t surface_height,
                            uint32_t stride,
                            uint32_t view_width,
                            uint32_t view_height);

#endif
