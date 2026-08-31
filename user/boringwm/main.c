#include <boring/desktop_log.h>
#include <boring/display_control.h>
#include <boring/event.h>
#include <boring/input_abi.h>
#include <boring/ipc.h>
#include "core.h"

#define WM_PEERS 8U
static struct wm_core wm;
static uint32_t display, peers[WM_PEERS], frame_number;
static uint64_t display_pid;
static struct display_event deferred_input;
static bool has_input, ever_managed;
int boring_main(void);

#if defined(BORING_M61_PHYSICAL_DESKTOP_WITNESS) && defined(BORING_WM_DEATH_ACCEPTANCE)
#error "M61 physical desktop witness must not alter the WM death acceptance binary"
#endif

static struct display_event display_rpc(const struct display_control *request) {
    struct display_event reply;
    struct boring_ipc_receive_result received;
    if (boring_ipc_send(display, request, sizeof(*request), 0U) != 0L) { desktop_fail("WM display send"); }
    for (;;) {
        if ((boring_ipc_receive(display, &reply, sizeof(reply), &received) != 0L) ||
            (received.payload_length != sizeof(reply)) || (received.buffer_handle != 0U) ||
            (reply.version != BORING_DISPLAY_CONTROL_VERSION)) { desktop_fail("WM display reply"); }
        if (reply.type == DISPLAY_REPLY) { return reply; }
        if ((reply.type != DISPLAY_INPUT) || has_input) { desktop_fail("WM input queue bound"); }
        deferred_input = reply; has_input = true;
    }
}

static void drop(uint32_t endpoint) {
    uint32_t index;
    for (index = 0U; index < BORING_WM_CLIENT_MAX; ++index) {
        if (wm.clients[index].active && (wm.clients[index].endpoint == endpoint)) {
            struct display_control unbind = {0};
            unbind.version = BORING_DISPLAY_CONTROL_VERSION; unbind.type = DISPLAY_UNBIND;
            unbind.surface = wm.clients[index].surface; unbind.window = wm.clients[index].token;
            (void)display_rpc(&unbind);
            (void)wm_remove(&wm, endpoint, unbind.window);
        }
    }
    for (index = 0U; index < WM_PEERS; ++index) { if (peers[index] == endpoint) { peers[index] = 0U; } }
    (void)boring_ipc_close(endpoint);
}

static bool app_send(uint32_t ep, const struct boring_wm_message *message) {
    if (boring_ipc_send(ep, message, sizeof(*message), 0U) == 0L) { return true; }
    drop(ep); return false;
}

static void snapshot(void) {
    char line[128] = "wm: frame=";
    size_t n = 10U; uint32_t index;
    n = desktop_number(line, n, ++frame_number);
    boring_memcpy(line + n, " count=", 7U); n += 7U; n = desktop_number(line, n, wm.count);
    boring_memcpy(line + n, " focus=", 7U); n += 7U; n = desktop_number(line, n, wm.focus);
    line[n++] = '\n'; line[n] = '\0'; desktop_say(line);
    for (index = 0U; index < wm.count; ++index) {
        const struct wm_client *c = &wm.clients[wm.order[index]];
        uint64_t values[8] = {c->token, c->surface, c->rect.x, c->rect.y,
                              c->rect.width, c->rect.height, c->rect.border, c->peer_pid};
        uint32_t field;
        boring_memcpy(line, "wm: tile ", 9U); n = 9U;
        for (field = 0U; field < 8U; ++field) { n = desktop_number(line, n, values[field]); line[n++] = ' '; }
        line[n - 1U] = '\n'; line[n] = '\0'; desktop_say(line);
    }
    desktop_say("wm: frame ready\n");
}

static void sync_layout(void) {
    uint32_t index = 0U;
    while (index < wm.count) {
        const struct wm_client *c = &wm.clients[wm.order[index]];
        struct display_control place = {0};
        struct display_event reply;
        struct boring_wm_message configure = {0};
        place.version = BORING_DISPLAY_CONTROL_VERSION; place.type = DISPLAY_PLACE;
        place.surface = c->surface; place.window = c->token;
        place.x = c->rect.x; place.y = c->rect.y; place.width = c->rect.width;
        place.height = c->rect.height; place.border = c->rect.border; place.order = index;
        place.color = wm.focus == c->token ? BORING_WM_FOCUSED : BORING_WM_UNFOCUSED;
        reply = display_rpc(&place);
        if (reply.status != BORING_DISPLAY_STATUS_OK) { drop(c->endpoint); index = 0U; continue; }
        configure.version = BORING_WM_VERSION; configure.type = BORING_WM_CONFIGURE;
        configure.token = c->token; configure.surface = c->surface;
        configure.x = place.x; configure.y = place.y; configure.width = place.width;
        configure.height = place.height; configure.border = place.border;
        configure.focused = wm.focus == c->token ? 1U : 0U;
        if (!app_send(c->endpoint, &configure)) { index = 0U; continue; }
        ++index;
    }
    {
        struct display_control present = {0};
        present.version = BORING_DISPLAY_CONTROL_VERSION; present.type = DISPLAY_PRESENT;
        present.background = BORING_WM_BACKGROUND;
        if (display_rpc(&present).status != BORING_DISPLAY_STATUS_OK) { desktop_fail("WM present"); }
    }
    snapshot();
}

static void request(uint32_t endpoint) {
    union { uint8_t bytes[BORING_IPC_INLINE_PAYLOAD_MAX]; struct boring_wm_message m; } data = {0};
    struct boring_ipc_receive_result received;
    struct boring_wm_message reply = {0};
    uint32_t status, token = 0U;
    bool changed = false;
    if (boring_ipc_receive(endpoint, &data, sizeof(data), &received) != 0L) { drop(endpoint); sync_layout(); return; }
    status = wm_validate_request(&data.m, (size_t)received.payload_length);
    if (received.buffer_handle != 0U) {
        /* Never map a grant. Reject and immediately close unexpected authority. */
        (void)boring_buffer_close(received.buffer_handle); status = BORING_WM_INVALID;
    }
    if ((status == BORING_WM_OK) && (data.m.type == BORING_WM_REGISTER)) {
        long pid = boring_endpoint_peer(endpoint);
        uint32_t old_focus = wm.focus;
        status = pid > 0L ? wm_add(&wm, endpoint, (uint64_t)pid, data.m.surface, &token) : BORING_WM_INVALID;
        if (status == BORING_WM_OK) {
            struct display_control bind = {0};
            bind.version = BORING_DISPLAY_CONTROL_VERSION; bind.type = DISPLAY_BIND;
            bind.surface = data.m.surface; bind.window = token; bind.owner_pid = (uint64_t)pid;
            if (display_rpc(&bind).status != BORING_DISPLAY_STATUS_OK) {
                (void)wm_remove(&wm, endpoint, token);
                wm.focus = wm_lookup(&wm, old_focus) != NULL ? old_focus : 0U;
                status = BORING_WM_ACCESS; token = 0U;
            } else { changed = true; ever_managed = true; }
        }
    } else if (status == BORING_WM_OK) {
        token = data.m.token; status = wm_authority(&wm, endpoint, token);
        if ((status == BORING_WM_OK) && (data.m.type == BORING_WM_UNREGISTER)) {
            const struct wm_client *client = wm_lookup(&wm, token);
            struct display_control unbind = {0};
            unbind.version = BORING_DISPLAY_CONTROL_VERSION; unbind.type = DISPLAY_UNBIND;
            unbind.surface = client->surface; unbind.window = token;
            (void)display_rpc(&unbind); (void)wm_remove(&wm, endpoint, token); changed = true;
        }
    }
    reply.version = BORING_WM_VERSION; reply.type = BORING_WM_REPLY; reply.status = status;
    reply.token = status == BORING_WM_OK ? token : 0U;
    if (!app_send(endpoint, &reply)) { changed = true; }
    if (changed) { desktop_say("wm: action registration\n"); sync_layout(); }
}

#ifndef BORING_WM_DEATH_ACCEPTANCE
static long launch_application(uint32_t key) {
    const char *path = wm_application_path(key);
    const char *argv[1] = { path };
    const struct boring_spawn_stdio stdio_config = {
        BORING_FD_STDIN, BORING_FD_STDOUT, BORING_FD_STDERR,
        BORING_SPAWN_FLAG_DETACHED
    };
    return path == NULL ? -1L : boring_spawn(path,
        boring_strlen(path), argv, 1U, &stdio_config);
}
#endif

static void handle_input(const struct display_event *message) {
    enum wm_action action;
#ifdef BORING_M38_WM_DEATH_ACCEPTANCE
    if ((message != NULL) &&
        (message->input.type == BORING_INPUT_EVENT_KEY) &&
        (message->input.code == BORING_KEY_F12) &&
        (message->input.value1 == BORING_KEY_DOWN_VALUE)) {
        desktop_say("wm: M38 test-only unexpected Ring3 exit\n");
        boring_exit(72);
    }
#endif
    action = wm_key(&wm, &message->input);
    if ((message->input.type == BORING_INPUT_EVENT_MOUSE_MOVE) &&
        wm_pointer(&wm, message->cursor_x, message->cursor_y)) { action = WM_FOCUS; }
    if ((action == WM_FOCUS) || (action == WM_REORDER)) {
        desktop_say(action == WM_FOCUS ? "wm: action focus\n" : "wm: action reorder\n"); sync_layout();
    } else if (action == WM_CLOSE) {
        const struct wm_client *client = wm_lookup(&wm, wm.focus);
        struct boring_wm_message close = {0};
        close.version = BORING_WM_VERSION; close.type = BORING_WM_CLOSE; close.token = client->token;
        desktop_say("wm: graceful close requested\n");
        if (!app_send(client->endpoint, &close)) { sync_layout(); }
    } else if (action == WM_NO_LAUNCHER) {
#ifdef BORING_WM_DEATH_ACCEPTANCE
        desktop_say("wm: terminal unavailable; no configured launcher\n");
        desktop_say("wm: dedicated negative acceptance exits WM\n"); boring_exit(0);
#else
        const long child_pid = launch_application(message->input.code);
        if (child_pid <= 0L) {
            /* Preserves the historical M35 no-launcher acceptance root. */
            desktop_say(message->input.code == BORING_KEY_ENTER ?
                "wm: terminal unavailable; no configured launcher\n" :
                "wm: requested application unavailable\n");
        } else if (message->input.code == BORING_KEY_ENTER) {
            desktop_say("wm: Super+Return spawned /bin/boring-terminal\n");
        } else if (message->input.code == BORING_KEY_E) {
            desktop_say("wm: Super+E spawned /bin/boring-edit\n");
        } else {
            desktop_say("wm: Super+F spawned /bin/boring-files\n");
        }
#endif
    } else if ((message->input.type == BORING_INPUT_EVENT_KEY) &&
               ((message->input.modifiers & BORING_MOD_SUPER) == 0U) &&
               (message->input.code != BORING_KEY_LEFT_SUPER) &&
               (message->input.code != BORING_KEY_RIGHT_SUPER)) {
        const struct wm_client *client = wm_lookup(&wm, wm.focus);
        if (client != NULL) {
            struct boring_wm_message key = {0};
            key.version = BORING_WM_VERSION; key.type = BORING_WM_KEY; key.token = client->token;
            key.surface = message->input.code; key.x = (uint32_t)message->input.value1;
            key.y = message->input.modifiers;
            if (!app_send(client->endpoint, &key)) { sync_layout(); }
        }
    }
    {
        struct display_control ack = {0}; ack.version = BORING_DISPLAY_CONTROL_VERSION; ack.type = DISPLAY_INPUT_ACK;
        if (boring_ipc_send(display, &ack, sizeof(ack), 0U) != 0L) { desktop_fail("WM input ACK"); }
    }
}

int boring_main(void) {
    long listener = boring_service_register(BORING_WM_SERVICE, BORING_WM_SERVICE_LENGTH);
    long ep = boring_service_connect(BORING_DISPLAY_SERVICE_NAME, BORING_DISPLAY_SERVICE_NAME_LENGTH);
    struct display_control hello = {0}; struct display_event info;
    if ((listener <= 0L) || (ep <= 0L)) { desktop_fail("WM service connect"); }
    display = (uint32_t)ep; ep = boring_endpoint_peer(display);
    if (ep <= 0L) { desktop_fail("WM display identity"); } display_pid = (uint64_t)ep;
    hello.version = BORING_DISPLAY_CONTROL_VERSION; hello.type = DISPLAY_MANAGER;
    info = display_rpc(&hello);
    if ((info.status != BORING_DISPLAY_STATUS_OK) || !wm_init(&wm, info.width, info.height)) {
        desktop_fail("WM manager binding");
    }
    desktop_say("wm: boring.wm Ring3 policy ready; no pixel mappings\n");
#ifdef BORING_M61_PHYSICAL_DESKTOP_WITNESS
    {
        static const char prefix[] = "M61 PHYSICAL: automatic terminal spawn pid=";
        char line[96];
        size_t n = sizeof(prefix) - 1U;
        long child_pid;
        desktop_say("M61 PHYSICAL: automatic terminal spawn requested\n");
        child_pid = launch_application(BORING_KEY_ENTER);
        if (child_pid <= 0L) {
            desktop_say("M61 PHYSICAL: automatic terminal spawn FAILED\n");
        } else {
            boring_memcpy(line, prefix, n);
            n = desktop_number(line, n, (uint64_t)child_pid);
            line[n++] = '\n'; line[n] = '\0';
            desktop_say(line);
        }
    }
#endif
    for (;;) {
        struct boring_event_watch watches[WM_PEERS + 2U] = {0};
        size_t count = 0U, index;
        if (has_input) { struct display_event message = deferred_input; has_input = false; handle_input(&message); }
        if (ever_managed && (wm.count == 0U)) { desktop_say("wm: session empty; clean exit\n"); boring_exit(0); }
        watches[count++] = (struct boring_event_watch){BORING_EVENT_IPC, (uint32_t)listener, 0U, 0U, 0ULL};
        watches[count++] = (struct boring_event_watch){BORING_EVENT_IPC, display, 0U, 0U, 0ULL};
        for (index = 0U; index < WM_PEERS; ++index) {
            if (peers[index] != 0U) { watches[count++] = (struct boring_event_watch){BORING_EVENT_IPC, peers[index], 0U, 0U, 0ULL}; }
        }
        if (boring_event_wait(watches, count, 0U) <= 0L) { desktop_fail("WM event wait"); }
        for (index = 0U; index < count; ++index) {
            uint32_t handle = watches[index].handle;
            if (watches[index].events == 0U) { continue; }
            if (handle == (uint32_t)listener) {
                long accepted = boring_service_accept((uint32_t)listener); size_t slot;
                if (accepted <= 0L) { desktop_fail("WM accept"); }
                ep = boring_endpoint_peer((uint32_t)accepted);
                if ((ep <= 0L) || ((uint64_t)ep == display_pid)) { (void)boring_ipc_close((uint32_t)accepted); continue; }
                for (slot = 0U; slot < WM_PEERS; ++slot) { if (peers[slot] == 0U) { break; } }
                if (slot == WM_PEERS) { (void)boring_ipc_close((uint32_t)accepted); }
                else { peers[slot] = (uint32_t)accepted; }
            } else if (handle == display) {
                struct display_event message; struct boring_ipc_receive_result received;
                if ((boring_ipc_receive(display, &message, sizeof(message), &received) != 0L) ||
                    (received.payload_length != sizeof(message)) || (received.buffer_handle != 0U) ||
                    (message.version != BORING_DISPLAY_CONTROL_VERSION) || (message.type != DISPLAY_INPUT)) {
                    desktop_fail("WM display input");
                }
                handle_input(&message);
            } else if ((watches[index].events & BORING_EVENT_HUP) != 0U) {
                drop(handle); desktop_say("wm: action peer cleanup\n"); sync_layout();
            } else { request(handle); }
        }
    }
}
