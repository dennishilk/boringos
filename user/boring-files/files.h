#ifndef BORING_FILES_H
#define BORING_FILES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <boring/syscall_abi.h>
#include "../boring-terminal/terminal.h"
#define BORING_FILES_MAX 64U
struct boring_files {
    struct boring_dirent entries[BORING_FILES_MAX];
    size_t count;
    size_t selected;
    char path[BORING_SYSCALL_CWD_MAX + 1U];
};
bool boring_files_path(const char *directory, const char *name, char *out, size_t capacity);
void boring_files_move(struct boring_files *files, int direction);
void boring_files_view(const struct boring_files *files, struct boring_terminal *view,
                       const char *status);
#endif
