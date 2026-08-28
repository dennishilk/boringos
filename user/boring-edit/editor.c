#include "editor.h"

static bool text_byte(char byte) {
    return byte == '\n' || (byte >= ' ' && byte <= '~');
}

bool boring_editor_load(struct boring_editor *editor, const char *bytes, size_t length) {
    size_t i;
    if (editor == NULL || length > BORING_EDIT_CAPACITY || (length != 0U && bytes == NULL)) {
        return false;
    }
    for (i = 0U; i < length; ++i) {
        if (!text_byte(bytes[i])) { return false; }
    }
    for (i = 0U; i < length; ++i) { editor->bytes[i] = bytes[i]; }
    editor->length = length;
    editor->cursor = length;
    editor->dirty = false;
    return true;
}

bool boring_editor_insert(struct boring_editor *editor, char byte) {
    size_t i;
    if (editor == NULL || editor->length >= BORING_EDIT_CAPACITY ||
        editor->cursor > editor->length || !text_byte(byte)) { return false; }
    for (i = editor->length; i > editor->cursor; --i) {
        editor->bytes[i] = editor->bytes[i - 1U];
    }
    editor->bytes[editor->cursor++] = byte;
    ++editor->length;
    editor->dirty = true;
    return true;
}

bool boring_editor_backspace(struct boring_editor *editor) {
    size_t i;
    if (editor == NULL || editor->length > BORING_EDIT_CAPACITY ||
        editor->cursor == 0U || editor->cursor > editor->length) { return false; }
    for (i = editor->cursor; i < editor->length; ++i) {
        editor->bytes[i - 1U] = editor->bytes[i];
    }
    --editor->cursor;
    --editor->length;
    editor->dirty = true;
    return true;
}

static void label(struct boring_terminal *view, uint32_t row, uint32_t col, const char *text) {
    while (text != NULL && *text != '\0' && col < view->cols && row < view->rows) {
        char byte = *text++;
        view->cells[row][col++] = (byte >= ' ' && byte <= '~') ? byte : '?';
    }
}

static void advance(char byte, uint32_t cols, uint32_t *row, uint32_t *col) {
    if (byte == '\n') { ++*row; *col = 0U; }
    else if (++*col == cols) { ++*row; *col = 0U; }
}

void boring_editor_view(const struct boring_editor *editor, struct boring_terminal *view,
                        const char *path, const char *status) {
    uint32_t row = 0U, col = 0U, cursor_row = 0U, cursor_col = 0U, first;
    size_t i;
    if (editor == NULL || view == NULL || view->cols == 0U || view->rows < 4U ||
        editor->length > BORING_EDIT_CAPACITY || editor->cursor > editor->length) { return; }
    boring_terminal_clear(view);
    for (i = 0U; i < editor->cursor; ++i) {
        advance(editor->bytes[i], view->cols, &cursor_row, &cursor_col);
    }
    first = cursor_row >= view->rows - 3U ? cursor_row - (view->rows - 4U) : 0U;
    label(view, 0U, 0U, editor->dirty ? "BoringEdit * " : "BoringEdit   ");
    label(view, 0U, 13U, path);
    for (i = 0U; i < editor->length; ++i) {
        if (editor->bytes[i] != '\n' && row >= first && row - first < view->rows - 3U) {
            view->cells[1U + row - first][col] = editor->bytes[i];
        }
        advance(editor->bytes[i], view->cols, &row, &col);
    }
    label(view, view->rows - 2U, 0U, status);
    label(view, view->rows - 1U, 0U, "Ctrl+S save | Super+Q close | Ctrl+Q discard");
    view->cursor_row = 1U + cursor_row - first;
    view->cursor_col = cursor_col;
}
