#include "terminal.h"

#define TERM_PARSE_TEXT 0U
#define TERM_PARSE_ESC 1U
#define TERM_PARSE_CSI 2U

static bool dimensions_valid(uint32_t cols, uint32_t rows) {
    return (cols != 0U) && (rows != 0U) &&
           (cols <= BORING_TERMINAL_MAX_COLS) &&
           (rows <= BORING_TERMINAL_MAX_ROWS);
}

static void clear_row(struct boring_terminal *terminal, uint32_t row) {
    uint32_t col;
    for (col = 0U; col < terminal->cols; ++col) {
        terminal->cells[row][col] = ' ';
    }
}

static void scroll(struct boring_terminal *terminal) {
    uint32_t row;
    uint32_t col;
    for (row = 1U; row < terminal->rows; ++row) {
        for (col = 0U; col < terminal->cols; ++col) {
            terminal->cells[row - 1U][col] = terminal->cells[row][col];
        }
    }
    clear_row(terminal, terminal->rows - 1U);
    terminal->cursor_row = terminal->rows - 1U;
}

static void linefeed(struct boring_terminal *terminal) {
    if (terminal->cursor_row + 1U < terminal->rows) {
        ++terminal->cursor_row;
    } else {
        scroll(terminal);
    }
}

static void put_printable(struct boring_terminal *terminal, char character) {
    terminal->cells[terminal->cursor_row][terminal->cursor_col] = character;
    ++terminal->cursor_col;
    if (terminal->cursor_col >= terminal->cols) {
        terminal->cursor_col = 0U;
        linefeed(terminal);
    }
}

static void erase_line_tail(struct boring_terminal *terminal) {
    uint32_t col;
    for (col = terminal->cursor_col; col < terminal->cols; ++col) {
        terminal->cells[terminal->cursor_row][col] = ' ';
    }
}

static uint32_t csi_count(const struct boring_terminal *terminal) {
    return terminal->csi_has_value && (terminal->csi_value != 0U) ?
        terminal->csi_value : 1U;
}

static void csi_finish(struct boring_terminal *terminal, unsigned char final) {
    uint32_t amount = csi_count(terminal);
    if (final == (unsigned char)'D') {
        if (amount > terminal->cursor_col) {
            amount = terminal->cursor_col;
        }
        terminal->cursor_col -= amount;
    } else if (final == (unsigned char)'C') {
        const uint32_t room = terminal->cols - terminal->cursor_col - 1U;
        if (amount > room) {
            amount = room;
        }
        terminal->cursor_col += amount;
    } else if (final == (unsigned char)'K') {
        erase_line_tail(terminal);
    } else if (final == (unsigned char)'J') {
        if (terminal->csi_has_value && (terminal->csi_value == 2U)) {
            boring_terminal_clear(terminal);
        }
    } else if (final == (unsigned char)'H') {
        terminal->cursor_col = 0U;
        terminal->cursor_row = 0U;
    }
    terminal->parser_state = TERM_PARSE_TEXT;
    terminal->csi_value = 0U;
    terminal->csi_has_value = false;
}

static void feed_byte(struct boring_terminal *terminal, unsigned char byte) {
    if (terminal->parser_state == TERM_PARSE_ESC) {
        if (byte == (unsigned char)'[') {
            terminal->parser_state = TERM_PARSE_CSI;
            terminal->csi_value = 0U;
            terminal->csi_has_value = false;
        } else {
            terminal->parser_state = TERM_PARSE_TEXT;
        }
        return;
    }
    if (terminal->parser_state == TERM_PARSE_CSI) {
        if ((byte >= (unsigned char)'0') && (byte <= (unsigned char)'9')) {
            uint32_t digit = (uint32_t)(byte - (unsigned char)'0');
            terminal->csi_has_value = true;
            if (terminal->csi_value <= 999U) {
                terminal->csi_value = terminal->csi_value * 10U + digit;
            }
            return;
        }
        if ((byte >= 0x40U) && (byte <= 0x7eU)) {
            csi_finish(terminal, byte);
            return;
        }
        if ((byte == (unsigned char)';') || (byte == (unsigned char)'?')) {
            /* Unsupported parameters stay bounded and are ignored. */
            return;
        }
        terminal->parser_state = TERM_PARSE_TEXT;
        return;
    }

    if (byte == 0x1bU) {
        terminal->parser_state = TERM_PARSE_ESC;
    } else if (byte == (unsigned char)'\n') {
        linefeed(terminal);
    } else if (byte == (unsigned char)'\r') {
        terminal->cursor_col = 0U;
    } else if (byte == (unsigned char)'\b') {
        if (terminal->cursor_col != 0U) {
            --terminal->cursor_col;
        }
    } else if (byte == (unsigned char)'\t') {
        uint32_t target = ((terminal->cursor_col / BORING_TERMINAL_TAB_WIDTH) + 1U) *
                          BORING_TERMINAL_TAB_WIDTH;
        if (target >= terminal->cols) {
            terminal->cursor_col = 0U;
            linefeed(terminal);
        } else {
            terminal->cursor_col = target;
        }
    } else if ((byte >= 0x20U) && (byte <= 0x7eU)) {
        put_printable(terminal, (char)byte);
    } else {
        /* BEL and all other unsupported controls are deliberately ignored. */
    }
}

bool boring_terminal_init(struct boring_terminal *terminal,
                          uint32_t cols, uint32_t rows) {
    uint32_t row;
    uint32_t col;
    if ((terminal == NULL) || !dimensions_valid(cols, rows)) {
        return false;
    }
    terminal->cols = cols;
    terminal->rows = rows;
    terminal->cursor_col = 0U;
    terminal->cursor_row = 0U;
    terminal->csi_value = 0U;
    terminal->parser_state = TERM_PARSE_TEXT;
    terminal->csi_has_value = false;
    for (row = 0U; row < BORING_TERMINAL_MAX_ROWS; ++row) {
        for (col = 0U; col < BORING_TERMINAL_MAX_COLS; ++col) {
            terminal->cells[row][col] = ' ';
        }
    }
    return true;
}

bool boring_terminal_resize(struct boring_terminal *terminal,
                            uint32_t cols, uint32_t rows) {
    uint32_t row;
    uint32_t col;
    uint32_t old_cols;
    uint32_t old_rows;
    if ((terminal == NULL) || !dimensions_valid(cols, rows) ||
        !dimensions_valid(terminal->cols, terminal->rows)) {
        return false;
    }
    old_cols = terminal->cols;
    old_rows = terminal->rows;
    terminal->cols = cols;
    terminal->rows = rows;
    if (cols > old_cols) {
        for (row = 0U; row < rows && row < old_rows; ++row) {
            for (col = old_cols; col < cols; ++col) {
                terminal->cells[row][col] = ' ';
            }
        }
    }
    if (rows > old_rows) {
        for (row = old_rows; row < rows; ++row) {
            clear_row(terminal, row);
        }
    }
    if (terminal->cursor_col >= cols) {
        terminal->cursor_col = cols - 1U;
    }
    if (terminal->cursor_row >= rows) {
        terminal->cursor_row = rows - 1U;
    }
    return true;
}

void boring_terminal_clear(struct boring_terminal *terminal) {
    uint32_t row;
    if ((terminal == NULL) || !dimensions_valid(terminal->cols, terminal->rows)) {
        return;
    }
    for (row = 0U; row < terminal->rows; ++row) {
        clear_row(terminal, row);
    }
    terminal->cursor_col = 0U;
    terminal->cursor_row = 0U;
}

void boring_terminal_feed(struct boring_terminal *terminal,
                          const void *bytes, size_t length) {
    const unsigned char *input = (const unsigned char *)bytes;
    size_t index;
    if ((terminal == NULL) || ((bytes == NULL) && (length != 0U)) ||
        !dimensions_valid(terminal->cols, terminal->rows)) {
        return;
    }
    for (index = 0U; index < length; ++index) {
        feed_byte(terminal, input[index]);
    }
}

char boring_terminal_cell(const struct boring_terminal *terminal,
                          uint32_t col, uint32_t row) {
    if ((terminal == NULL) || (col >= terminal->cols) || (row >= terminal->rows)) {
        return '\0';
    }
    return terminal->cells[row][col];
}
