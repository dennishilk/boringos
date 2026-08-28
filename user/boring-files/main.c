#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/desktop_log.h>
#include <boring/display.h>
#include <boring/display_control.h>
#include <boring/event.h>
#include <boring/input_abi.h>
#include <boring/ipc.h>
#include <boring/syscall.h>
#include <boring/syscall_abi.h>
#include <boring/wm.h>

#include "../boring-terminal/render.h"
#include "files.h"

static uint32_t display;
static uint32_t manager;
static uint32_t surface;
static uint32_t window_token;
static uint32_t buffer_handle;
static struct boring_files files;
static struct boring_files *pending;
static const char *status_line = "Ready";
static char destination[BORING_SYSCALL_CWD_MAX + 1U];
static char previous_path[BORING_SYSCALL_CWD_MAX + 1U];
static uint8_t *pixels;
static uint32_t surface_width;
static uint32_t surface_height;
static uint32_t surface_stride;
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

static struct display_event control_rpc(const struct display_control *request) {
    struct display_event reply;
    struct boring_ipc_receive_result received;
    if ((boring_ipc_send(display, request, sizeof(*request), 0U) != 0L) ||
        (boring_ipc_receive(display, &reply, sizeof(reply), &received) != 0L) ||
        (received.payload_length != sizeof(reply)) ||
        (received.buffer_handle != 0U) ||
        (reply.version != BORING_DISPLAY_CONTROL_VERSION) ||
        (reply.type != DISPLAY_REPLY)) {
        files_fail("display control RPC");
    }
    return reply;
}

static struct boring_display_reply surface_rpc(
    const struct boring_display_request *request, uint32_t attachment) {
    struct boring_display_reply reply;
    struct boring_ipc_receive_result received;
    if ((boring_ipc_send(display, request, sizeof(*request), attachment) != 0L) ||
        (boring_ipc_receive(display, &reply, sizeof(reply), &received) != 0L) ||
        (received.payload_length != sizeof(reply)) ||
        (received.buffer_handle != 0U) ||
        (reply.version != BORING_DISPLAY_PROTOCOL_VERSION)) {
        files_fail("surface RPC");
    }
    return reply;
}

static struct boring_wm_message wm_rpc(const struct boring_wm_message *request) {
    struct boring_wm_message reply;
    struct boring_ipc_receive_result received;
    if (boring_ipc_send(manager, request, sizeof(*request), 0U) != 0L) {
        files_fail("WM send");
    }
    for (;;) {
        if ((boring_ipc_receive(manager, &reply, sizeof(reply), &received) != 0L) ||
            (received.payload_length != sizeof(reply)) ||
            (received.buffer_handle != 0U) ||
            (reply.version != BORING_WM_VERSION)) {
            files_fail("WM receive");
        }
        if (reply.type == BORING_WM_REPLY) {
            return reply;
        }
        if ((reply.type != BORING_WM_CONFIGURE) &&
            !((request->type == BORING_WM_UNREGISTER) &&
              ((reply.type == BORING_WM_KEY) || (reply.type == BORING_WM_CLOSE)))) {
            files_fail("unexpected WM RPC event");
        }
    }
}

static void commit(void) {
    struct boring_display_request request = {0};
    struct boring_display_reply reply;
    boring_files_view(&files, &terminal, status_line);
    if (!boring_terminal_render(&terminal, pixels, surface_width, surface_height,
                                surface_stride, view_width, view_height)) {
        files_fail("render bounds");
    }
    request.version = BORING_DISPLAY_PROTOCOL_VERSION;
    request.type = BORING_DISPLAY_REQUEST_COMMIT;
    request.surface_token = surface;
    reply = surface_rpc(&request, 0U);
    if (reply.status != BORING_DISPLAY_STATUS_OK) {
        files_fail("surface commit");
    }
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

static void unregister_window(void) {
    struct boring_wm_message request = {0};
    struct boring_wm_message reply;
    if ((manager == 0U) || (window_token == 0U)) {
        return;
    }
    request.version = BORING_WM_VERSION;
    request.type = BORING_WM_UNREGISTER;
    request.token = window_token;
    reply = wm_rpc(&request);
    if (reply.status != BORING_WM_OK) {
        files_fail("WM unregister");
    }
    window_token = 0U;
    desktop_say("boring-files: WM unregister\n");
}

static void destroy_surface(void) {
    struct boring_display_request request = {0};
    struct boring_display_reply reply;
    if ((display == 0U) || (surface == 0U)) {
        return;
    }
    request.version = BORING_DISPLAY_PROTOCOL_VERSION;
    request.type = BORING_DISPLAY_REQUEST_DESTROY;
    request.surface_token = surface;
    reply = surface_rpc(&request, 0U);
    if (reply.status != BORING_DISPLAY_STATUS_OK) {
        files_fail("surface destroy");
    }
    surface = 0U;
}

static void graceful_close(void) __attribute__((noreturn));
static void graceful_close(void) {
    unregister_window();
    destroy_surface();
    if ((pixels != NULL) && (boring_buffer_unmap(pixels) != 0L)) {
        files_fail("buffer unmap");
    }
    pixels = NULL;
    if ((buffer_handle != 0U) && (boring_buffer_close(buffer_handle) != 0L)) {
        files_fail("buffer close");
    }
    buffer_handle = 0U;
    if (manager != 0U) {
        (void)boring_ipc_close(manager);
        manager = 0U;
    }
    if (display != 0U) {
        (void)boring_ipc_close(display);
        display = 0U;
    }
    if (pending != NULL && boring_memory_free(pending) != 0L) { files_fail("directory scratch free"); }
    pending = NULL;
    desktop_say("boring-files: graceful cleanup complete\n");
    boring_exit(0);
}

int boring_main(int argc, char **argv) {
    struct display_control control = {0};
    struct display_event info;
    struct boring_display_request create = {0};
    struct boring_display_reply created;
    struct boring_wm_message registration = {0};
    struct boring_wm_message registered;
    long endpoint;
    uint32_t initial_cols;
    uint32_t initial_rows;

    if (((argc != 1) && (argc != 2)) || (argv == NULL) || (argv[0] == NULL) || ((argc == 2) && (argv[1] == NULL))) {
        files_fail("argv");
    }
    pending = boring_memory_alloc(sizeof(*pending));
    if (pending == NULL) { files_fail("directory scratch allocation"); }
    if (!reload_directory(argc == 2 ? argv[1] : ".")) { files_fail("initial directory"); }
    endpoint = boring_service_connect(BORING_DISPLAY_SERVICE_NAME,
                                      BORING_DISPLAY_SERVICE_NAME_LENGTH);
    if (endpoint <= 0L) {
        files_fail("display connect");
    }
    display = (uint32_t)endpoint;
    endpoint = boring_service_connect(BORING_WM_SERVICE, BORING_WM_SERVICE_LENGTH);
    if (endpoint <= 0L) {
        files_fail("WM connect");
    }
    manager = (uint32_t)endpoint;

    control.version = BORING_DISPLAY_CONTROL_VERSION;
    control.type = DISPLAY_INFO;
    info = control_rpc(&control);
    if ((info.status != BORING_DISPLAY_STATUS_OK) || (info.width == 0U) ||
        (info.height == 0U) || (info.width > BORING_DISPLAY_MAX_WIDTH) ||
        (info.height > BORING_DISPLAY_MAX_HEIGHT) ||
        !boring_terminal_geometry(info.width, info.height, &initial_cols,
                                  &initial_rows) ||
        !boring_terminal_init(&terminal, initial_cols, initial_rows)) {
        files_fail("display geometry");
    }
    surface_width = info.width;
    surface_height = info.height;
    surface_stride = surface_width * 4U;
    view_width = surface_width;
    view_height = surface_height;
    endpoint = boring_buffer_create((size_t)surface_stride * surface_height);
    if (endpoint <= 0L) {
        files_fail("M32 buffer create");
    }
    buffer_handle = (uint32_t)endpoint;
    pixels = boring_buffer_map(buffer_handle);
    if (pixels == NULL) {
        files_fail("M32 buffer map");
    }
    boring_files_view(&files, &terminal, status_line);
    if (!boring_terminal_render(&terminal, pixels, surface_width, surface_height,
                                surface_stride, view_width, view_height)) {
        files_fail("initial render");
    }
    create.version = BORING_DISPLAY_PROTOCOL_VERSION;
    create.type = BORING_DISPLAY_REQUEST_CREATE;
    create.width = surface_width;
    create.height = surface_height;
    create.stride = surface_stride;
    create.pixel_format = BORING_DISPLAY_PIXEL_FORMAT_XRGB8888;
    create.byte_size = (uint64_t)surface_stride * surface_height;
    created = surface_rpc(&create, buffer_handle);
    if ((created.status != BORING_DISPLAY_STATUS_OK) ||
        (created.surface_token == 0U)) {
        files_fail("surface create");
    }
    surface = created.surface_token;
    control = (struct display_control){0};
    control.version = BORING_DISPLAY_CONTROL_VERSION;
    control.type = DISPLAY_DELEGATE;
    control.surface = surface;
    if (control_rpc(&control).status != BORING_DISPLAY_STATUS_OK) {
        files_fail("surface delegate");
    }
    registration.version = BORING_WM_VERSION;
    registration.type = BORING_WM_REGISTER;
    registration.surface = surface;
    registered = wm_rpc(&registration);
    if ((registered.status != BORING_WM_OK) || (registered.token == 0U)) {
        files_fail("WM REGISTER");
    }
    window_token = registered.token;

    desktop_say("boring-files: Ring3 managed client ready\n");

    for (;;) {
        struct boring_event_watch watches[1] = {
            {BORING_EVENT_IPC, manager, 0U, 0U, 0ULL}
        };
        long ready = boring_event_wait(watches, 1U, 0U);
        if (ready <= 0L) {
            files_fail("EVENT_WAIT");
        }
        if (watches[0].events != 0U) {
            struct boring_wm_message event;
            struct boring_ipc_receive_result received;
            if ((watches[0].events & BORING_EVENT_HUP) != 0U) {
                files_fail("WM disconnected");
            }
            if ((boring_ipc_receive(manager, &event, sizeof(event), &received) != 0L) ||
                (received.payload_length != sizeof(event)) ||
                (received.buffer_handle != 0U) ||
                (event.version != BORING_WM_VERSION) ||
                (event.token != window_token)) {
                files_fail("WM event envelope");
            }
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
