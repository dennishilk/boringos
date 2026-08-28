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

#include "input.h"
#include "render.h"
#include "terminal.h"

static uint32_t display;
static uint32_t manager;
static uint32_t surface;
static uint32_t window_token;
static uint32_t buffer_handle;
static uint32_t master_fd = UINT32_MAX;
static uint8_t *pixels;
static uint32_t surface_width;
static uint32_t surface_height;
static uint32_t surface_stride;
static uint32_t view_width;
static uint32_t view_height;
static struct boring_terminal terminal;

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

static struct display_event control_rpc(const struct display_control *request) {
    struct display_event reply;
    struct boring_ipc_receive_result received;
    if ((boring_ipc_send(display, request, sizeof(*request), 0U) != 0L) ||
        (boring_ipc_receive(display, &reply, sizeof(reply), &received) != 0L) ||
        (received.payload_length != sizeof(reply)) ||
        (received.buffer_handle != 0U) ||
        (reply.version != BORING_DISPLAY_CONTROL_VERSION) ||
        (reply.type != DISPLAY_REPLY)) {
        term_fail("display control RPC");
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
        term_fail("surface RPC");
    }
    return reply;
}

static struct boring_wm_message wm_rpc(const struct boring_wm_message *request) {
    struct boring_wm_message reply;
    struct boring_ipc_receive_result received;
    if (boring_ipc_send(manager, request, sizeof(*request), 0U) != 0L) {
        term_fail("WM send");
    }
    for (;;) {
        if ((boring_ipc_receive(manager, &reply, sizeof(reply), &received) != 0L) ||
            (received.payload_length != sizeof(reply)) ||
            (received.buffer_handle != 0U) ||
            (reply.version != BORING_WM_VERSION)) {
            term_fail("WM receive");
        }
        if (reply.type == BORING_WM_REPLY) {
            return reply;
        }
        if ((reply.type != BORING_WM_CONFIGURE) &&
            !((request->type == BORING_WM_UNREGISTER) &&
              ((reply.type == BORING_WM_KEY) || (reply.type == BORING_WM_CLOSE)))) {
            term_fail("unexpected WM RPC event");
        }
    }
}

static void commit(void) {
    struct boring_display_request request = {0};
    struct boring_display_reply reply;
    if (!boring_terminal_render(&terminal, pixels, surface_width, surface_height,
                                surface_stride, view_width, view_height)) {
        term_fail("render bounds");
    }
    request.version = BORING_DISPLAY_PROTOCOL_VERSION;
    request.type = BORING_DISPLAY_REQUEST_COMMIT;
    request.surface_token = surface;
    reply = surface_rpc(&request, 0U);
    if (reply.status != BORING_DISPLAY_STATUS_OK) {
        term_fail("surface commit");
    }
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
    commit();
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
    if ((message == NULL) || (message->x != (uint32_t)BORING_KEY_DOWN_VALUE)) {
        return;
    }
#ifdef BORING_TERMINAL_DEATH_ACCEPTANCE
    if (message->surface == BORING_KEY_F12) {
        desktop_say("boring-terminal: test-only unexpected exit without unregister\n");
        boring_exit(71);
    }
#endif
    length = boring_terminal_key_bytes(message->surface, message->y, bytes);
    if (length != 0U) {
        write_master(bytes, length);
    }
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
        term_fail("WM unregister");
    }
    window_token = 0U;
    desktop_say("boring-terminal: WM unregister\n");
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
        term_fail("surface destroy");
    }
    surface = 0U;
}

static void graceful_close(void) __attribute__((noreturn));
static void graceful_close(void) {
    if (master_fd != UINT32_MAX) {
        if (boring_fd_close(master_fd) != 0L) {
            term_fail("PTY master close");
        }
        master_fd = UINT32_MAX;
        desktop_say("boring-terminal: PTY master closed\n");
    }
    unregister_window();
    destroy_surface();
    if ((pixels != NULL) && (boring_buffer_unmap(pixels) != 0L)) {
        term_fail("buffer unmap");
    }
    pixels = NULL;
    if ((buffer_handle != 0U) && (boring_buffer_close(buffer_handle) != 0L)) {
        term_fail("buffer close");
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
    desktop_say("boring-terminal: graceful cleanup complete\n");
    boring_exit(0);
}

static bool read_pty_output(void) {
    char bytes[BORING_SYSCALL_FD_IO_MAX];
    const long result = boring_fd_read(master_fd, bytes, sizeof(bytes));
    if (result < 0L) {
        term_fail("PTY master read");
    }
    if (result == 0L) {
        return false;
    }
    boring_terminal_feed(&terminal, bytes, (size_t)result);
    commit();
    return true;
}

int boring_main(int argc, char **argv) {
    static const char shell_path[] = "/bin/boring-shell";
    const char *shell_argv[1] = {shell_path};
    struct boring_spawn_stdio stdio_config;
    struct boring_pty_create_result pty;
    struct display_control control = {0};
    struct display_event info;
    struct boring_display_request create = {0};
    struct boring_display_reply created;
    struct boring_wm_message registration = {0};
    struct boring_wm_message registered;
    long endpoint;
    long shell_pid;
    uint32_t initial_cols;
    uint32_t initial_rows;

    if ((argc != 1) || (argv == NULL) || (argv[0] == NULL)) {
        term_fail("argv");
    }
    master_fd = UINT32_MAX;
    endpoint = boring_service_connect(BORING_DISPLAY_SERVICE_NAME,
                                      BORING_DISPLAY_SERVICE_NAME_LENGTH);
    if (endpoint <= 0L) {
        term_fail("display connect");
    }
    display = (uint32_t)endpoint;
    endpoint = boring_service_connect(BORING_WM_SERVICE, BORING_WM_SERVICE_LENGTH);
    if (endpoint <= 0L) {
        term_fail("WM connect");
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
        term_fail("display geometry");
    }
    surface_width = info.width;
    surface_height = info.height;
    surface_stride = surface_width * 4U;
    view_width = surface_width;
    view_height = surface_height;
    endpoint = boring_buffer_create((size_t)surface_stride * surface_height);
    if (endpoint <= 0L) {
        term_fail("M32 buffer create");
    }
    buffer_handle = (uint32_t)endpoint;
    pixels = boring_buffer_map(buffer_handle);
    if (pixels == NULL) {
        term_fail("M32 buffer map");
    }
    if (!boring_terminal_render(&terminal, pixels, surface_width, surface_height,
                                surface_stride, view_width, view_height)) {
        term_fail("initial render");
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
        term_fail("surface create");
    }
    surface = created.surface_token;
    control = (struct display_control){0};
    control.version = BORING_DISPLAY_CONTROL_VERSION;
    control.type = DISPLAY_DELEGATE;
    control.surface = surface;
    if (control_rpc(&control).status != BORING_DISPLAY_STATUS_OK) {
        term_fail("surface delegate");
    }
    registration.version = BORING_WM_VERSION;
    registration.type = BORING_WM_REGISTER;
    registration.surface = surface;
    registered = wm_rpc(&registration);
    if ((registered.status != BORING_WM_OK) || (registered.token == 0U)) {
        term_fail("WM REGISTER");
    }
    window_token = registered.token;

    if (boring_pty_create(&pty) != 0L) {
        term_fail("PTY_CREATE");
    }
    master_fd = pty.master_fd;
    stdio_config.stdin_fd = pty.slave_fd;
    stdio_config.stdout_fd = pty.slave_fd;
    stdio_config.stderr_fd = pty.slave_fd;
    stdio_config.flags = BORING_SPAWN_FLAG_DETACHED;
    shell_pid = boring_spawn(shell_path, sizeof(shell_path) - 1U,
                             shell_argv, 1U, &stdio_config);
    if (shell_pid <= 0L) {
        term_fail("spawn boring-shell");
    }
    if (boring_fd_close(pty.slave_fd) != 0L) {
        term_fail("local slave close");
    }
    desktop_say("boring-terminal: Ring3 managed client + PTY + boring-shell ready\n");

    for (;;) {
        struct boring_event_watch watches[2] = {
            {BORING_EVENT_IPC, manager, 0U, 0U, 0ULL},
            {BORING_EVENT_FD, master_fd, 0U, 0U, 0ULL}
        };
        long ready = boring_event_wait(watches, 2U, 0U);
        if (ready <= 0L) {
            term_fail("EVENT_WAIT");
        }
        if (watches[0].events != 0U) {
            struct boring_wm_message event;
            struct boring_ipc_receive_result received;
            if ((watches[0].events & BORING_EVENT_HUP) != 0U) {
                term_fail("WM disconnected");
            }
            if ((boring_ipc_receive(manager, &event, sizeof(event), &received) != 0L) ||
                (received.payload_length != sizeof(event)) ||
                (received.buffer_handle != 0U) ||
                (event.version != BORING_WM_VERSION) ||
                (event.token != window_token)) {
                term_fail("WM event envelope");
            }
            if (event.type == BORING_WM_CONFIGURE) {
                configure(&event);
            } else if (event.type == BORING_WM_KEY) {
                handle_key(&event);
            } else if (event.type == BORING_WM_CLOSE) {
                desktop_say("boring-terminal: CLOSE received\n");
                graceful_close();
            } else {
                term_fail("WM event type");
            }
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
