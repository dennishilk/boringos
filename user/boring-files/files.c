#include "files.h"

bool boring_files_path(const char *directory, const char *name, char *out, size_t capacity) {
    size_t d = 0U, n = 0U, i, separator;
    if (directory == NULL || name == NULL || out == NULL || directory[0] != '/') { return false; }
    while (d <= BORING_SYSCALL_CWD_MAX && directory[d] != '\0') { ++d; }
    while (n < BORING_DIRENT_NAME_CAPACITY && name[n] != '\0') {
        if (name[n] == '/') { return false; }
        ++n;
    }
    if (d == 0U || d > BORING_SYSCALL_CWD_MAX || n == 0U ||
        n >= BORING_DIRENT_NAME_CAPACITY ||
        (n == 1U && name[0] == '.') ||
        (n == 2U && name[0] == '.' && name[1] == '.')) { return false; }
    separator = directory[d - 1U] == '/' ? 0U : 1U;
    if (d + separator + n >= capacity) { return false; }
    for (i = 0U; i < d; ++i) { out[i] = directory[i]; }
    if (separator != 0U) { out[d++] = '/'; }
    for (i = 0U; i < n; ++i) { out[d + i] = name[i]; }
    out[d + n] = '\0';
    return true;
}

void boring_files_move(struct boring_files *files, int direction) {
    if (files == NULL || files->count == 0U || files->count > BORING_FILES_MAX ||
        files->selected >= files->count) { return; }
    if (direction < 0 && files->selected != 0U) { --files->selected; }
    if (direction > 0 && files->selected + 1U < files->count) { ++files->selected; }
}

static void label(struct boring_terminal *view, uint32_t row, uint32_t col, const char *text) {
    while (text != NULL && *text != '\0' && col < view->cols && row < view->rows) {
        char ch = *text++;
        view->cells[row][col++] = ch >= ' ' && ch <= '~' ? ch : '?';
    }
}

void boring_files_view(const struct boring_files *files, struct boring_terminal *view,
                       const char *status) {
    size_t first, i;
    if (files == NULL || view == NULL || view->cols == 0U || view->rows < 4U ||
        files->count > BORING_FILES_MAX || (files->count != 0U && files->selected >= files->count)) { return; }
    boring_terminal_clear(view);
    label(view, 0U, 0U, "BoringFiles ");
    label(view, 0U, 12U, files->path);
    first = files->selected >= view->rows - 3U ? files->selected - (view->rows - 4U) : 0U;
    for (i = first; i < files->count && i - first < view->rows - 3U; ++i) {
        const uint32_t row = 1U + (uint32_t)(i - first);
        label(view, row, 0U, i == files->selected ? "> " : "  ");
        label(view, row, 2U, files->entries[i].type == BORING_DIRENT_TYPE_DIRECTORY ? "[D] " : "[F] ");
        label(view, row, 6U, files->entries[i].name);
    }
    if (files->count == 0U) { label(view, 1U, 0U, "(empty directory)"); }
    label(view, view->rows - 2U, 0U, status);
    label(view, view->rows - 1U, 0U, "Enter open | Backspace parent | R refresh");
    view->cursor_row = 1U + (uint32_t)(files->selected - first);
    view->cursor_col = 0U;
}
