#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../user/boring-edit/editor.h"

int main(void) {
    struct boring_editor e = {0};
    struct boring_terminal view;
    char bytes[BORING_EDIT_CAPACITY + 1U];
    size_t i;
    memset(bytes, 'a', sizeof(bytes));
    assert(!boring_editor_load(NULL, bytes, 1U));
    assert(boring_editor_load(&e, NULL, 0U));
    assert(!boring_editor_backspace(&e));
    assert(boring_editor_insert(&e, 'a'));
    assert(boring_editor_insert(&e, '\n'));
    assert(boring_editor_insert(&e, 'b'));
    assert(e.dirty && e.length == 3U && !memcmp(e.bytes, "a\nb", 3U));
    e.cursor = 1U;
    assert(boring_editor_insert(&e, ' '));
    assert(!memcmp(e.bytes, "a \nb", 4U));
    assert(boring_editor_backspace(&e));
    assert(!memcmp(e.bytes, "a\nb", 3U));
    assert(!boring_editor_load(&e, "bad\x1b", 4U));
    assert(e.length == 3U && !memcmp(e.bytes, "a\nb", 3U));
    assert(!boring_editor_load(&e, bytes, sizeof(bytes)));
    assert(boring_editor_load(&e, bytes, BORING_EDIT_CAPACITY));
    assert(!e.dirty && e.cursor == BORING_EDIT_CAPACITY);
    assert(!boring_editor_insert(&e, 'b'));
    assert(boring_editor_backspace(&e));
    assert(boring_editor_insert(&e, 'b'));
    assert(e.bytes[BORING_EDIT_CAPACITY - 1U] == 'b');
    assert(boring_terminal_init(&view, 20U, 8U));
    boring_editor_view(&e, &view, "/file", "Saved");
    assert(view.cursor_row > 0U && view.cursor_row < view.rows - 2U);
    assert(view.cursor_col < view.cols);
    assert(!memcmp(view.cells[0], "BoringEdit * ", 12U));
    assert(boring_editor_load(&e, NULL, 0U));
    for (i = 0U; i < 100U; ++i) { assert(boring_editor_insert(&e, '\n')); }
    boring_editor_view(&e, &view, "/file", "Modified");
    assert(view.cursor_row == 5U && view.cursor_col == 0U);
    e.cursor = 0U;
    boring_editor_view(&e, &view, "/file", "Modified");
    assert(view.cursor_row == 1U && view.cursor_col == 0U);
    puts("BoringEdit bounded model, insertion, newline, backspace, rejection and scroll tests passed.");
    return 0;
}
