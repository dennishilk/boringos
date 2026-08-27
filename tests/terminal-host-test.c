#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <boring/input_abi.h>

#include "../user/boring-terminal/terminal.h"
#include "../user/boring-terminal/input.h"

static void require(int condition, const char *message) {
    if (!condition) {
        (void)fprintf(stderr, "terminal-host-test: %s\n", message);
        exit(1);
    }
}

static void feed(struct boring_terminal *terminal, const char *text) {
    boring_terminal_feed(terminal, text, strlen(text));
}

static void test_input(void) {
    static const struct {
        uint32_t code;
        const char *plain;
        const char *shifted;
    } cases[] = {
        {BORING_KEY_ESCAPE, "\x1b", "\x1b"},
        {BORING_KEY_TAB, "\t", "\t"},
        {BORING_KEY_ENTER, "\r", "\r"},
        {BORING_KEY_BACKSPACE, "\x7f", "\x7f"},
        {BORING_KEY_SPACE, " ", " "},
        {BORING_KEY_MINUS, "-", "_"}, {BORING_KEY_EQUAL, "=", "+"},
        {BORING_KEY_LEFT_BRACKET, "[", "{"}, {BORING_KEY_RIGHT_BRACKET, "]", "}"},
        {BORING_KEY_BACKSLASH, "\\", "|"}, {BORING_KEY_SEMICOLON, ";", ":"},
        {BORING_KEY_APOSTROPHE, "'", "\""}, {BORING_KEY_GRAVE, "`", "~"},
        {BORING_KEY_COMMA, ",", "<"}, {BORING_KEY_DOT, ".", ">"},
        {BORING_KEY_SLASH, "/", "?"},
        {BORING_KEY_LEFT, "\x1b[D", "\x1b[D"}, {BORING_KEY_RIGHT, "\x1b[C", "\x1b[C"},
        {BORING_KEY_UP, "\x1b[A", "\x1b[A"}, {BORING_KEY_DOWN, "\x1b[B", "\x1b[B"},
        {BORING_KEY_HOME, "\x1b[H", "\x1b[H"}, {BORING_KEY_END, "\x1b[F", "\x1b[F"},
        {BORING_KEY_DELETE, "\x1b[3~", "\x1b[3~"}
    };
    char bytes[BORING_TERMINAL_KEY_BYTES_MAX + 1U];
    size_t index;
    uint32_t code;
    for (code = BORING_KEY_A; code <= BORING_KEY_Z; ++code) {
        require(boring_terminal_key_bytes(code, 0U, bytes) == 1U &&
                bytes[0] == (char)('a' + code - BORING_KEY_A), "lowercase key");
        require(boring_terminal_key_bytes(code, BORING_MOD_SHIFT, bytes) == 1U &&
                bytes[0] == (char)('A' + code - BORING_KEY_A), "uppercase key");
        require(boring_terminal_key_bytes(code, BORING_MOD_CTRL, bytes) == 1U &&
                bytes[0] == (char)(1U + code - BORING_KEY_A), "control letter");
    }
    for (code = BORING_KEY_0; code <= BORING_KEY_9; ++code) {
        require(boring_terminal_key_bytes(code, 0U, bytes) == 1U &&
                bytes[0] == (char)('0' + code - BORING_KEY_0), "digit key");
        require(boring_terminal_key_bytes(code, BORING_MOD_SHIFT, bytes) == 1U &&
                bytes[0] == ")!@#$%^&*("[code - BORING_KEY_0], "shifted digit");
    }
    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        uint32_t shifted;
        for (shifted = 0U; shifted < 2U; ++shifted) {
            const char *text = shifted != 0U ? cases[index].shifted : cases[index].plain;
            const size_t length = strlen(text);
            memset(bytes, 0x55, sizeof(bytes));
            require(boring_terminal_key_bytes(cases[index].code,
                    shifted != 0U ? BORING_MOD_SHIFT : 0U, bytes) == length &&
                    memcmp(bytes, text, length) == 0 && bytes[length] == 0x55,
                    "editing/punctuation exact bounded sequence");
        }
    }
    require(boring_terminal_key_bytes(BORING_KEY_ENTER, BORING_MOD_SUPER, bytes) == 0U &&
            boring_terminal_key_bytes(BORING_KEY_Q, BORING_MOD_SUPER, bytes) == 0U &&
            boring_terminal_key_bytes(BORING_KEY_A, BORING_MOD_ALT, bytes) == 0U,
            "window manager shortcuts never enter the PTY");
    require(boring_terminal_key_bytes(UINT32_MAX, 0U, bytes) == 0U &&
            boring_terminal_key_bytes(BORING_KEY_A, 0U, NULL) == 0U, "invalid key/output");
}

int main(void) {
    struct boring_terminal terminal;
    char printable[95];
    size_t index;

    test_input();

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

    (void)puts("boring-terminal parser/grid/key-translation host tests passed.");
    return 0;
}
