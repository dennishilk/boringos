#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../user/boring-files/files.h"
int main(void) {
    struct boring_files files = {0};
    struct boring_terminal view;
    char path[32];
    assert(boring_files_path("/", "docs", path, sizeof(path)));
    assert(strcmp(path, "/docs") == 0);
    assert(boring_files_path("/docs", "note.txt", path, sizeof(path)));
    assert(strcmp(path, "/docs/note.txt") == 0);
    assert(!boring_files_path("relative", "x", path, sizeof(path)));
    assert(!boring_files_path("/", "../x", path, sizeof(path)));
    assert(!boring_files_path("/", "..", path, sizeof(path)));
    assert(!boring_files_path("/", "", path, sizeof(path)));
    assert(!boring_files_path("/docs", "note.txt", path, 5U));
    files.count = 2U;
    strcpy(files.path, "/");
    strcpy(files.entries[0].name, "docs");
    files.entries[0].type = BORING_DIRENT_TYPE_DIRECTORY;
    strcpy(files.entries[1].name, "note.txt");
    files.entries[1].type = BORING_DIRENT_TYPE_REGULAR;
    boring_files_move(&files, -1); assert(files.selected == 0U);
    boring_files_move(&files, 1); boring_files_move(&files, 1); assert(files.selected == 1U);
    assert(boring_terminal_init(&view, 40U, 8U));
    boring_files_view(&files, &view, "Ready");
    assert(!memcmp(view.cells[1], "  [D] docs", 10U));
    assert(!memcmp(view.cells[2], "> [F] note.txt", 14U));
    assert(view.cursor_row == 2U);
    files.count = 64U; files.selected = 63U;
    boring_files_view(&files, &view, "Ready");
    assert(view.cursor_row == 5U);
    files.count = 0U; files.selected = 0U;
    boring_files_view(&files, &view, "Ready");
    assert(!memcmp(view.cells[1], "(empty directory)", 17U));
    puts("BoringFiles bounded paths, selection, type labels and scrolling passed.");
    return 0;
}
