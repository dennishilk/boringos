#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/client.h>
#include <boring/desktop_log.h>
#include <boring/event.h>
#include <boring/input_abi.h>
#include <boring/syscall.h>
#include <boring/syscall_abi.h>
#include <boring/wm.h>

#include "input.h"
#include "render.h"
#include "terminal.h"

static uint32_t master_fd = UINT32_MAX;
static struct boring_client client;
static uint32_t view_width;
static uint32_t view_height;
static struct boring_terminal terminal;
static char previous_cells[BORING_TERMINAL_MAX_ROWS][BORING_TERMINAL_MAX_COLS];

int boring_main(int argc, char **argv);

static void term_fail(const char *reason) __attribute__((noreturn));
static void term_fail(const char *reason) {
    desktop_say("M36 terminal FAILED: ");
    desktop_say(reason);
    desktop_say("\n");
    if (master_fd != UINT32_MAX) {
        (void)boring_fd_close(master_fd);
        master_fd = UINT32_MAX;
        desktop_say("boring-terminal: failure closed PTY master\n");
    }
    boring_exit(90);
}

static void client_check(bool ok) {
    if (!ok) { term_fail(client.error); }
}

static void render(void) {
    if (!boring_terminal_render(&terminal, client.pixels, client.width, client.height,
                                client.stride, view_width, view_height)) {
        term_fail("render bounds");
    }
}

static void commit_view(void) {
    render();
    client_check(boring_client_commit_damage(&client, 0U, 0U,
                                             view_width, view_height));
}

static void snapshot_cells(void) {
    uint32_t row;
    for (row = 0U; row < terminal.rows; ++row) {
        uint32_t col;
        for (col = 0U; col < terminal.cols; ++col) {
            previous_cells[row][col] = terminal.cells[row][col];
        }
    }
}

static bool changed_cell_damage(uint32_t old_cursor_col,
                                uint32_t old_cursor_row,
                                uint32_t *x_out, uint32_t *y_out,
                                uint32_t *width_out, uint32_t *height_out) {
    uint32_t min_col = terminal.cols;
    uint32_t min_row = terminal.rows;
    uint32_t max_col = 0U;
    uint32_t max_row = 0U;
    uint32_t row;
    bool changed = false;
    for (row = 0U; row < terminal.rows; ++row) {
        uint32_t col;
        for (col = 0U; col < terminal.cols; ++col) {
            if (previous_cells[row][col] == terminal.cells[row][col]) { continue; }
            if (!changed || (col < min_col)) { min_col = col; }
            if (!changed || (col > max_col)) { max_col = col; }
            if (!changed || (row < min_row)) { min_row = row; }
            if (!changed || (row > max_row)) { max_row = row; }
            changed = true;
        }
    }
    if ((old_cursor_col < terminal.cols) && (old_cursor_row < terminal.rows)) {
        if (!changed || (old_cursor_col < min_col)) { min_col = old_cursor_col; }
        if (!changed || (old_cursor_col > max_col)) { max_col = old_cursor_col; }
        if (!changed || (old_cursor_row < min_row)) { min_row = old_cursor_row; }
        if (!changed || (old_cursor_row > max_row)) { max_row = old_cursor_row; }
        changed = true;
    }
    if ((terminal.cursor_col < terminal.cols) && (terminal.cursor_row < terminal.rows)) {
        if (!changed || (terminal.cursor_col < min_col)) { min_col = terminal.cursor_col; }
        if (!changed || (terminal.cursor_col > max_col)) { max_col = terminal.cursor_col; }
        if (!changed || (terminal.cursor_row < min_row)) { min_row = terminal.cursor_row; }
        if (!changed || (terminal.cursor_row > max_row)) { max_row = terminal.cursor_row; }
        changed = true;
    }
    if (!changed) { return false; }
    *x_out = BORING_TERMINAL_MARGIN_X + min_col * BORING_TERMINAL_CELL_WIDTH;
    *y_out = BORING_TERMINAL_MARGIN_Y + min_row * BORING_TERMINAL_CELL_HEIGHT;
    *width_out = (max_col - min_col + 1U) * BORING_TERMINAL_CELL_WIDTH;
    *height_out = (max_row - min_row + 1U) * BORING_TERMINAL_CELL_HEIGHT;
    if (*x_out >= view_width || *y_out >= view_height) { return false; }
    if (*width_out > view_width - *x_out) { *width_out = view_width - *x_out; }
    if (*height_out > view_height - *y_out) { *height_out = view_height - *y_out; }
    return (*width_out != 0U) && (*height_out != 0U);
}

static void configure(const struct boring_wm_message *message) {
    uint32_t width;
    uint32_t height;
    uint32_t cols;
    uint32_t rows;
    if ((message == NULL) || (message->width < 2U * message->border) ||
        (message->height < 2U * message->border)) {
        term_fail("WM configure geometry");
    }
    width = message->width - 2U * message->border;
    height = message->height - 2U * message->border;
    if (!boring_terminal_geometry(width, height, &cols, &rows) ||
        !boring_terminal_resize(&terminal, cols, rows)) {
        term_fail("terminal resize");
    }
    view_width = width;
    view_height = height;
    commit_view();
    desktop_say("boring-terminal: CONFIGURE/redraw\n");
}

static void write_master(const char *bytes, size_t length) {
    size_t offset = 0U;
    while (offset < length) {
        const long result = boring_fd_write(master_fd, &bytes[offset], length - offset);
        if ((result <= 0L) || ((size_t)result > length - offset)) {
            term_fail("PTY master write");
        }
        offset += (size_t)result;
    }
}

static void handle_key(const struct boring_wm_message *message) {
    char bytes[BORING_TERMINAL_KEY_BYTES_MAX];
    size_t length;
    if ((message == NULL) || (message->x != (uint32_t)BORING_KEY_DOWN_VALUE)) { return; }
#ifdef BORING_TERMINAL_DEATH_ACCEPTANCE
    if (message->surface == BORING_KEY_F12) {
        desktop_say("boring-terminal: test-only unexpected exit without unregister\n");
        boring_exit(71);
    }
#endif
    length = boring_terminal_key_bytes(message->surface, message->y, bytes);
    if (length != 0U) { write_master(bytes, length); }
}

static void graceful_close(void) __attribute__((noreturn));
static void graceful_close(void) {
    if (master_fd != UINT32_MAX) {
        if (boring_fd_close(master_fd) != 0L) { term_fail("PTY master close"); }
        master_fd = UINT32_MAX;
        desktop_say("boring-terminal: PTY master closed\n");
    }
    client_check(boring_client_unregister(&client));
    desktop_say("boring-terminal: WM unregister\n");
    client_check(boring_client_release(&client));
    desktop_say("boring-terminal: graceful cleanup complete\n");
    boring_exit(0);
}

static bool read_pty_output(void) {
    char bytes[BORING_SYSCALL_FD_IO_MAX];
    const long result = boring_fd_read(master_fd, bytes, sizeof(bytes));
    uint32_t damage_x;
    uint32_t damage_y;
    uint32_t damage_width;
    uint32_t damage_height;
    uint32_t old_cursor_col;
    uint32_t old_cursor_row;
    if (result < 0L) { term_fail("PTY master read"); }
    if (result == 0L) { return false; }
    snapshot_cells();
    old_cursor_col = terminal.cursor_col;
    old_cursor_row = terminal.cursor_row;
    boring_terminal_feed(&terminal, bytes, (size_t)result);
    if (changed_cell_damage(old_cursor_col, old_cursor_row,
                            &damage_x, &damage_y,
                            &damage_width, &damage_height)) {
        render();
        client_check(boring_client_commit_damage(&client,
                                                 damage_x, damage_y,
                                                 damage_width, damage_height));
    }
    return true;
}

int boring_main(int argc, char **argv) {
    static const char shell_path[] = "/bin/boring-shell";
    const char *shell_argv[1] = {shell_path};
    struct boring_spawn_stdio stdio_config;
    struct boring_pty_create_result pty;
    long shell_pid;
    uint32_t initial_cols;
    uint32_t initial_rows;
    if ((argc != 1) || (argv == NULL) || (argv[0] == NULL)) { term_fail("argv"); }
    master_fd = UINT32_MAX;
    client_check(boring_client_open(&client));
    if (!boring_terminal_geometry(client.width, client.height,
                                  &initial_cols, &initial_rows) ||
        !boring_terminal_init(&terminal, initial_cols, initial_rows)) {
        term_fail("display geometry");
    }
    view_width = client.width;
    view_height = client.height;
    render();
    client_check(boring_client_publish(&client));
    if (boring_pty_create(&pty) != 0L) { term_fail("PTY_CREATE"); }
    master_fd = pty.master_fd;
    stdio_config.stdin_fd = pty.slave_fd;
    stdio_config.stdout_fd = pty.slave_fd;
    stdio_config.stderr_fd = pty.slave_fd;
    stdio_config.flags = BORING_SPAWN_FLAG_DETACHED;
    shell_pid = boring_spawn(shell_path, sizeof(shell_path) - 1U,
                             shell_argv, 1U, &stdio_config);
    if (shell_pid <= 0L) { term_fail("spawn boring-shell"); }
    if (boring_fd_close(pty.slave_fd) != 0L) { term_fail("local slave close"); }
    desktop_say("boring-terminal: Ring3 managed client + PTY + boring-shell ready\n");
    for (;;) {
        struct boring_event_watch watches[2] = {
            {BORING_EVENT_IPC, client.manager, 0U, 0U, 0ULL},
            {BORING_EVENT_FD, master_fd, 0U, 0U, 0ULL}
        };
        long ready = boring_event_wait(watches, 2U, 0U);
        if (ready <= 0L) { term_fail("EVENT_WAIT"); }
        if (watches[0].events != 0U) {
            struct boring_wm_message event;
            if ((watches[0].events & BORING_EVENT_HUP) != 0U) { term_fail("WM disconnected"); }
            client_check(boring_client_receive(&client, &event));
            if (event.type == BORING_WM_CONFIGURE) { configure(&event); }
            else if (event.type == BORING_WM_KEY) { handle_key(&event); }
            else if (event.type == BORING_WM_CLOSE) {
                desktop_say("boring-terminal: CLOSE received\n");
                graceful_close();
            } else { term_fail("WM event type"); }
        }
        if (watches[1].events != 0U) {
            if ((watches[1].events & BORING_EVENT_READ) != 0U) {
                (void)read_pty_output();
            }
            if ((watches[1].events & BORING_EVENT_HUP) != 0U) {
                desktop_say("boring-terminal: shell PTY HUP/EOF\n");
                graceful_close();
            }
        }
    }
}