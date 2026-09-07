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

#include "../boring-terminal/input.h"
#include "../boring-terminal/render.h"
#include "editor.h"

static struct boring_editor editor;
static char document_path[BORING_SYSCALL_EXEC_PATH_MAX + 1U];
static bool close_requested;
static const char *status_line = "Ready";
static char file_bytes[BORING_EDIT_CAPACITY + 1U];
static struct boring_client client;
static uint32_t view_width;
static uint32_t view_height;
static struct boring_terminal terminal;

int boring_main(int argc, char **argv);

static void term_fail(const char *reason) __attribute__((noreturn));
static void term_fail(const char *reason) {
    desktop_say("boring-edit FAILED: ");
    desktop_say(reason);
    desktop_say("\n");
    boring_exit(90);
}

static void client_check(bool ok) {
    if (!ok) { term_fail(client.error); }
}

static void commit(void) {
    boring_editor_view(&editor, &terminal, document_path, status_line);
    if (!boring_terminal_render(&terminal, client.pixels, client.width, client.height,
                                client.stride, view_width, view_height)) {
        term_fail("render bounds");
    }
    client_check(boring_client_commit(&client));
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
        (rows < 4U) || !boring_terminal_resize(&terminal, cols, rows)) {
        term_fail("terminal resize");
    }
    view_width = width;
    view_height = height;
    commit();
    desktop_say("boring-edit: CONFIGURE/redraw\n");
}


static void graceful_close(void) __attribute__((noreturn));

static void save_document(void) {
    const size_t path_length = boring_strlen(document_path);
    long result = boring_fs_touch(document_path, path_length);
    if (result == 0L || result == -(long)BORING_SYSCALL_EEXIST) {
        result = boring_fs_write(document_path, path_length, editor.bytes, editor.length);
    }
    if (result < 0L || (size_t)result != editor.length) {
        status_line = "SAVE FAILED - document retained; retry Ctrl+S";
        desktop_say("boring-edit: save failed; buffer retained\n");
    } else {
        editor.dirty = false;
        status_line = "Saved to BoringFS";
        desktop_say("boring-edit: saved document\n");
        if (close_requested) { graceful_close(); }
    }
}

static void handle_key(const struct boring_wm_message *message) {
    char bytes[BORING_TERMINAL_KEY_BYTES_MAX];
    size_t length;
    if (message->x != (uint32_t)BORING_KEY_DOWN_VALUE) { return; }
    if ((message->y & (BORING_MOD_ALT | BORING_MOD_SUPER)) != 0U) { return; }
    if ((message->y & BORING_MOD_CTRL) != 0U) {
        if (message->surface == BORING_KEY_S) { save_document(); }
        else if (message->surface == BORING_KEY_Q) { graceful_close(); }
    } else if (message->surface == BORING_KEY_LEFT) {
        if (editor.cursor != 0U) { --editor.cursor; }
    } else if (message->surface == BORING_KEY_RIGHT) {
        if (editor.cursor < editor.length) { ++editor.cursor; }
    } else if (message->surface == BORING_KEY_BACKSPACE) {
        (void)boring_editor_backspace(&editor);
    } else {
        length = boring_terminal_key_bytes(message->surface, message->y, bytes);
        if (length == 1U) {
            const char byte = bytes[0] == '\r' ? '\n' : bytes[0];
            if (byte == '\n' || (byte >= ' ' && byte <= '~')) {
                status_line = boring_editor_insert(&editor, byte) ?
                    "Modified" : "BUFFER FULL - 4096 byte limit";
            }
        }
    }
    commit();
    desktop_say("boring-edit: key/redraw\n");
}

static void graceful_close(void) __attribute__((noreturn));
static void graceful_close(void) {
    client_check(boring_client_unregister(&client));
    desktop_say("boring-edit: WM unregister\n");
    client_check(boring_client_release(&client));
    desktop_say("boring-edit: graceful cleanup complete\n");
    boring_exit(0);
}

static void open_document(int argc, char **argv) {
    const char *path = argc == 2 ? argv[1] : "/untitled.txt";
    size_t length = 0U, used = 0U;
    long fd;
    while (path[length] != '\0' && length < sizeof(document_path)) { ++length; }
    if (length == 0U || length >= sizeof(document_path)) { term_fail("path length"); }
    boring_memcpy(document_path, path, length + 1U);
    if (argc == 1) { return; }
    fd = boring_fd_open(document_path, length, BORING_FD_OPEN_READ);
    if (fd == -(long)BORING_SYSCALL_ENOENT) { status_line = "New file"; return; }
    if (fd < 0L) { term_fail("open document"); }
    while (used < sizeof(file_bytes)) {
        size_t capacity = sizeof(file_bytes) - used;
        long result;
        if (capacity > BORING_SYSCALL_FD_IO_MAX) { capacity = BORING_SYSCALL_FD_IO_MAX; }
        result = boring_fd_read((uint32_t)fd, file_bytes + used, capacity);
        if (result < 0L || (size_t)result > capacity) {
            (void)boring_fd_close((uint32_t)fd); term_fail("read document");
        }
        if (result == 0L) { break; }
        used += (size_t)result;
    }
    if (boring_fd_close((uint32_t)fd) != 0L) { term_fail("document FD close"); }
    if (!boring_editor_load(&editor, file_bytes, used)) {
        term_fail("document exceeds 4096 bytes or is not ASCII text; unchanged");
    }
    desktop_say("boring-edit: loaded real BoringFS document\n");
}

int boring_main(int argc, char **argv) {
    uint32_t initial_cols;
    uint32_t initial_rows;

    if (((argc != 1) && (argc != 2)) || (argv == NULL) || (argv[0] == NULL) || ((argc == 2) && (argv[1] == NULL))) {
        term_fail("argv");
    }
    open_document(argc, argv);
    client_check(boring_client_open(&client));
    if (!boring_terminal_geometry(client.width, client.height, &initial_cols,
                                  &initial_rows) ||
        !boring_terminal_init(&terminal, initial_cols, initial_rows)) {
        term_fail("display geometry");
    }
    view_width = client.width;
    view_height = client.height;
    boring_editor_view(&editor, &terminal, document_path, status_line);
    if (!boring_terminal_render(&terminal, client.pixels, client.width, client.height,
                                client.stride, view_width, view_height)) {
        term_fail("initial render");
    }
    client_check(boring_client_publish(&client));

    desktop_say("boring-edit: Ring3 managed client ready\n");

    for (;;) {
        struct boring_event_watch watches[1] = {
            {BORING_EVENT_IPC, client.manager, 0U, 0U, 0ULL}
        };
        long ready = boring_event_wait(watches, 1U, 0U);
        if (ready <= 0L) {
            term_fail("EVENT_WAIT");
        }
        if (watches[0].events != 0U) {
            struct boring_wm_message event;
            if ((watches[0].events & BORING_EVENT_HUP) != 0U) {
                term_fail("WM disconnected");
            }
            client_check(boring_client_receive(&client, &event));
            if (event.type == BORING_WM_CONFIGURE) {
                configure(&event);
            } else if (event.type == BORING_WM_KEY) {
                handle_key(&event);
            } else if (event.type == BORING_WM_CLOSE) {
                desktop_say("boring-edit: CLOSE received\n");
                if (!editor.dirty) { graceful_close(); }
                close_requested = true;
                status_line = "UNSAVED - Ctrl+S save/close or Ctrl+Q discard";
                commit();
            } else {
                term_fail("WM event type");
            }
        }
    }
}
