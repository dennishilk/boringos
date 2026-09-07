#ifndef BORING_EDIT_H
#define BORING_EDIT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../boring-terminal/terminal.h"
#define BORING_EDIT_CAPACITY 4096U
struct boring_editor {
    char bytes[BORING_EDIT_CAPACITY];
    size_t length;
    size_t cursor;
    bool dirty;
};
bool boring_editor_load(struct boring_editor *editor, const char *bytes, size_t length);
bool boring_editor_insert(struct boring_editor *editor, char byte);
bool boring_editor_backspace(struct boring_editor *editor);
void boring_editor_view(const struct boring_editor *editor, struct boring_terminal *view,
                        const char *path, const char *status);
#endif
