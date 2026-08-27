#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../user/boring-terminal/terminal.h"

static void require(int condition, const char *message) {
    if (!condition) {
        (void)fprintf(stderr, "terminal-host-test: %s\n", message);
        exit(1);
    }
}

static void feed(struct boring_terminal *terminal, const char *text) {
    boring_terminal_feed(terminal, text, strlen(text));
}

int main(void) {
    struct boring_terminal terminal;
    char printable[95];
    size_t index;

    require(!boring_terminal_init(NULL, 10U, 4U), "NULL init accepted");
    require(!boring_terminal_init(&terminal, 0U, 4U), "zero cols accepted");
    require(!boring_terminal_init(&terminal, 10U, 0U), "zero rows accepted");
    require(!boring_terminal_init(&terminal, BORING_TERMINAL_MAX_COLS + 1U, 4U),
            "oversize cols accepted");
    require(boring_terminal_init(&terminal, 10U, 4U), "init failed");

    feed(&terminal, "abc\rX\nY");
    require(boring_terminal_cell(&terminal, 0U, 0U) == 'X', "CR did not return");
    require(boring_terminal_cell(&terminal, 1U, 0U) == 'b', "CR clobbered tail");
    require(boring_terminal_cell(&terminal, 1U, 1U) == 'Y', "LF semantics wrong");

    boring_terminal_clear(&terminal);
    feed(&terminal, "a\tb");
    require(boring_terminal_cell(&terminal, 0U, 0U) == 'a', "tab prefix lost");
    require(boring_terminal_cell(&terminal, 4U, 0U) == 'b', "tab stop wrong");
    feed(&terminal, "\bZ");
    require(boring_terminal_cell(&terminal, 4U, 0U) == 'Z', "backspace wrong");

    require(boring_terminal_init(&terminal, 4U, 3U), "wrap init failed");
    feed(&terminal, "abcdefghijklmn");
    require(boring_terminal_cell(&terminal, 0U, 0U) == 'e', "scroll row0 wrong");
    require(boring_terminal_cell(&terminal, 0U, 1U) == 'i', "scroll row1 wrong");
    require(boring_terminal_cell(&terminal, 0U, 2U) == 'm', "scroll row2 wrong");
    require(terminal.cursor_col == 2U && terminal.cursor_row == 2U,
            "wrap cursor wrong");

    require(boring_terminal_init(&terminal, 20U, 5U), "ANSI init failed");
    feed(&terminal, "prompt> hello\x1b[3DXYZ\x1b[K");
    require(boring_terminal_cell(&terminal, 8U, 0U) == 'h', "ANSI left setup wrong");
    require(boring_terminal_cell(&terminal, 10U, 0U) == 'X' &&
            boring_terminal_cell(&terminal, 12U, 0U) == 'Z', "CSI D wrong");
    require(boring_terminal_cell(&terminal, 13U, 0U) == ' ', "CSI K wrong");
    feed(&terminal, "\x1b[2Jafter\x1b[H!");
    require(boring_terminal_cell(&terminal, 0U, 0U) == '!', "CSI H wrong");
    require(boring_terminal_cell(&terminal, 1U, 0U) == 'f', "CSI 2J/H content wrong");

    for (index = 0U; index < sizeof(printable); ++index) {
        printable[index] = (char)(0x20 + index);
    }
    require(boring_terminal_init(&terminal, 100U, 3U), "printable init failed");
    boring_terminal_feed(&terminal, printable, sizeof(printable));
    for (index = 0U; index < sizeof(printable); ++index) {
        require(boring_terminal_cell(&terminal, (uint32_t)index, 0U) == printable[index],
                "printable ASCII changed");
    }

    require(boring_terminal_resize(&terminal, 40U, 2U), "resize failed");
    require(terminal.cols == 40U && terminal.rows == 2U,
            "resize geometry wrong");
    require(!boring_terminal_resize(&terminal, 0U, 2U), "invalid resize accepted");

    (void)puts("boring-terminal parser/grid host tests passed.");
    return 0;
}
