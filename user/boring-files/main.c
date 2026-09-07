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

#include "../boring-terminal/render.h"
#include "files.h"

static struct boring_files files;
static struct boring_files *pending;
static const char *status_line = "Ready";
static char destination[BORING_SYSCALL_CWD_MAX + 1U];
static char previous_path[BORING_SYSCALL_CWD_MAX + 1U];
static struct boring_client client;
static uint32_t view_width;
static uint32_t view_height;
static struct boring_terminal terminal;

int boring_main(int argc, char **argv);

static void files_fail(const char *reason) __attribute__((noreturn));
static void files_fail(const char *reason) {
    desktop_say("boring-files FAILED: ");
    desktop_say(reason);
    desktop_say("\n");
    boring_exit(90);
}

static void client_check(bool ok) {
    if (!ok) { files_fail(client.error); }
}

static void commit(void) {
    boring_files_view(&files, &terminal, status_line);
    if (!boring_terminal_render(&terminal, client.pixels, client.width, client.height,
                                client.stride, view_width, view_height)) {
        files_fail("render bounds");
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
        files_fail("WM configure geometry");
    }
    width = message->width - 2U * message->border;
    height = message->height - 2U * message->border;
    if (!boring_terminal_geometry(width, height, &cols, &rows) ||
        (rows < 4U) || !boring_terminal_resize(&terminal, cols, rows)) {
        files_fail("terminal resize");
    }
    view_width = width;
    view_height = height;
    commit();
    desktop_say("boring-files: CONFIGURE/redraw\n");
}


static void graceful_close(void) __attribute__((noreturn));

static bool reload_directory(const char *path) {
    size_t i;
    struct boring_dirent extra;
    if (boring_getcwd(previous_path, sizeof(previous_path)) < 0L ||
        boring_fs_chdir(path, boring_strlen(path)) != 0L) {
        status_line = "Cannot enter directory";
        return false;
    }
    boring_memset(pending, 0, sizeof(*pending));
    if (boring_getcwd(pending->path, sizeof(pending->path)) < 0L) {
        files_fail("getcwd");
    }
    for (i = 0U; i <= BORING_FILES_MAX; ++i) {
        struct boring_dirent *entry = i == BORING_FILES_MAX ? &extra : &pending->entries[i];
        const long result = boring_fs_readdir(".", 1U, (uint64_t)i, entry);
        if (result == 0L) { break; }
        if (result != 1L || entry->name_length == 0U ||
            entry->name_length >= BORING_DIRENT_NAME_CAPACITY ||
            entry->name[entry->name_length] != '\0' ||
            (entry->type != BORING_DIRENT_TYPE_DIRECTORY && entry->type != BORING_DIRENT_TYPE_REGULAR)) {
            if (boring_fs_chdir(previous_path, boring_strlen(previous_path)) != 0L) {
                files_fail("restore cwd");
            }
            status_line = "Directory read failed - previous view retained";
            return false;
        }
        if (i != BORING_FILES_MAX) { ++pending->count; }
    }
    boring_memcpy(&files, pending, sizeof(files));
    status_line = i > BORING_FILES_MAX ? "First 64 entries only - directory limit" : "BoringFS directory loaded";
    desktop_say("boring-files: real readdir complete\n");
    return true;
}

static void open_selection(void) {
    const char *args[2] = {"/bin/boring-edit", destination};
    const struct boring_spawn_stdio stdio_config = {
        BORING_FD_STDIN, BORING_FD_STDOUT, BORING_FD_STDERR, BORING_SPAWN_FLAG_DETACHED
    };
    if (files.count == 0U) { return; }
    if (!boring_files_path(files.path, files.entries[files.selected].name,
                           destination, sizeof(destination))) {
        status_line = "Path too long or invalid";
        return;
    }
    if (files.entries[files.selected].type == BORING_DIRENT_TYPE_DIRECTORY) {
        (void)reload_directory(destination);
    } else {
        const long child = boring_spawn(args[0], boring_strlen(args[0]), args, 2U, &stdio_config);
        status_line = child > 0L ? "Editor launched - R refresh after editing" : "Editor launch failed";
        if (child > 0L) { desktop_say("boring-files: spawned real boring-edit\n"); }
    }
}

static void handle_key(const struct boring_wm_message *message) {
    if (message->x != (uint32_t)BORING_KEY_DOWN_VALUE || message->y != 0U) { return; }
    if (message->surface == BORING_KEY_UP) { boring_files_move(&files, -1); }
    else if (message->surface == BORING_KEY_DOWN) { boring_files_move(&files, 1); }
    else if (message->surface == BORING_KEY_ENTER) { open_selection(); }
    else if (message->surface == BORING_KEY_BACKSPACE) { (void)reload_directory(".."); }
    else if (message->surface == BORING_KEY_R) { (void)reload_directory("."); }
    commit();
    desktop_say("boring-files: key/redraw\n");
}

static void graceful_close(void) __attribute__((noreturn));
static void graceful_close(void) {
    client_check(boring_client_unregister(&client));
    desktop_say("boring-files: WM unregister\n");
    client_check(boring_client_release(&client));
    if (pending != NULL && boring_memory_free(pending) != 0L) { files_fail("directory scratch free"); }
    pending = NULL;
    desktop_say("boring-files: graceful cleanup complete\n");
    boring_exit(0);
}

int boring_main(int argc, char **argv) {
    uint32_t initial_cols;
    uint32_t initial_rows;

    if (((argc != 1) && (argc != 2)) || (argv == NULL) || (argv[0] == NULL) || ((argc == 2) && (argv[1] == NULL))) {
        files_fail("argv");
    }
    pending = boring_memory_alloc(sizeof(*pending));
    if (pending == NULL) { files_fail("directory scratch allocation"); }
    if (!reload_directory(argc == 2 ? argv[1] : ".")) { files_fail("initial directory"); }
    client_check(boring_client_open(&client));
    if (!boring_terminal_geometry(client.width, client.height, &initial_cols,
                                  &initial_rows) ||
        !boring_terminal_init(&terminal, initial_cols, initial_rows)) {
        files_fail("display geometry");
    }
    view_width = client.width;
    view_height = client.height;
    boring_files_view(&files, &terminal, status_line);
    if (!boring_terminal_render(&terminal, client.pixels, client.width, client.height,
                                client.stride, view_width, view_height)) {
        files_fail("initial render");
    }
    client_check(boring_client_publish(&client));

    desktop_say("boring-files: Ring3 managed client ready\n");

    for (;;) {
        struct boring_event_watch watches[1] = {
            {BORING_EVENT_IPC, client.manager, 0U, 0U, 0ULL}
        };
        long ready = boring_event_wait(watches, 1U, 0U);
        if (ready <= 0L) {
            files_fail("EVENT_WAIT");
        }
        if (watches[0].events != 0U) {
            struct boring_wm_message event;
            if ((watches[0].events & BORING_EVENT_HUP) != 0U) {
                files_fail("WM disconnected");
            }
            client_check(boring_client_receive(&client, &event));
            if (event.type == BORING_WM_CONFIGURE) {
                configure(&event);
            } else if (event.type == BORING_WM_KEY) {
                handle_key(&event);
            } else if (event.type == BORING_WM_CLOSE) {
                desktop_say("boring-files: CLOSE received\n");
                graceful_close();
            } else {
                files_fail("WM event type");
            }
        }
    }
}
