#ifndef BORING_TERMINAL_ENGINE_H
#define BORING_TERMINAL_ENGINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BORING_TERMINAL_MAX_COLS 160U
#define BORING_TERMINAL_MAX_ROWS 96U
#define BORING_TERMINAL_TAB_WIDTH 4U

struct boring_terminal {
    char cells[BORING_TERMINAL_MAX_ROWS][BORING_TERMINAL_MAX_COLS];
    uint32_t cols;
    uint32_t rows;
    uint32_t cursor_col;
    uint32_t cursor_row;
    uint32_t csi_value;
    uint8_t parser_state;
    bool csi_has_value;
};

bool boring_terminal_init(struct boring_terminal *terminal,
                          uint32_t cols, uint32_t rows);
bool boring_terminal_resize(struct boring_terminal *terminal,
                            uint32_t cols, uint32_t rows);
void boring_terminal_clear(struct boring_terminal *terminal);
void boring_terminal_feed(struct boring_terminal *terminal,
                          const void *bytes, size_t length);
char boring_terminal_cell(const struct boring_terminal *terminal,
                          uint32_t col, uint32_t row);

#endif
