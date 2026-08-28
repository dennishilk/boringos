#include <boring/desktop_log.h>
#include <boring/display.h>
#include <boring/event.h>
#include <boring/input_abi.h>
#include <boring/ipc.h>
#include <boring/wm.h>
#include "managed.h"

#define DISPLAY_PEERS 8U
static struct boring_display_core core;
static struct display_managed managed;
static uint32_t peers[DISPLAY_PEERS];
static uint32_t composition;
static uint8_t *pixels;
static bool input_pending, manager_seen;
int boring_main(void);

static void present(void) {
    if (!display_managed_compose(&managed, &core, pixels, (size_t)core.byte_size) ||
        (boring_framebuffer_present(composition) != 0L)) { desktop_fail("display present"); }
}

static void forget_peer(uint32_t endpoint) {
    uint32_t handles[BORING_DISPLAY_SURFACE_MAX];
    uint8_t *maps[BORING_DISPLAY_SURFACE_MAX];
    size_t index, count;
    for (index = 0U; index < BORING_DISPLAY_SURFACE_MAX; ++index) {
        if (core.surfaces[index].active && (core.surfaces[index].owner_endpoint == endpoint)) {
            display_managed_forget(&managed, core.surfaces[index].token);
        }
    }
    count = boring_display_peer_cleanup(&core, endpoint, handles, maps);
    for (index = 0U; index < count; ++index) {
        if ((boring_buffer_unmap(maps[index]) != 0L) ||
            (boring_buffer_close(handles[index]) != 0L)) { desktop_fail("display peer buffer cleanup"); }
    }
    if (managed.manager_endpoint == endpoint) {
        managed.manager_endpoint = 0U;
        input_pending = false;
        desktop_say("display: manager disconnected; display survives\n");
    }
    for (index = 0U; index < DISPLAY_PEERS; ++index) {
        if (peers[index] == endpoint) { peers[index] = 0U; }
    }
    (void)boring_ipc_close(endpoint);
    present();
}

static void send_message(uint32_t endpoint, const void *data, size_t size) {
    if (boring_ipc_send(endpoint, data, size, 0U) != 0L) { forget_peer(endpoint); }
}

static void control_reply(uint32_t endpoint, uint32_t status, uint32_t surface) {
    struct display_event reply = {0};
    reply.version = BORING_DISPLAY_CONTROL_VERSION; reply.type = DISPLAY_REPLY;
    reply.status = status; reply.surface = surface;
    reply.width = core.width; reply.height = core.height;
    reply.cursor_x = core.cursor_x; reply.cursor_y = core.cursor_y;
    send_message(endpoint, &reply, sizeof(reply));
}

static void old_request(uint32_t endpoint, const struct boring_display_request *r,
                         uint32_t attachment) {
    uint32_t status = BORING_DISPLAY_STATUS_INVALID, token = r->surface_token;
    struct boring_display_reply reply = {BORING_DISPLAY_PROTOCOL_VERSION, 0U, 0U, 0U};
    if ((r->type == BORING_DISPLAY_REQUEST_CREATE) && (attachment != 0U)) {
        long size = boring_buffer_info(attachment);
        if (size > 0L) { status = boring_display_validate_create(&core, r, (uint64_t)size); }
        if (status == BORING_DISPLAY_STATUS_OK) {
            uint8_t *map = boring_buffer_map(attachment);
            if (map != NULL) {
                status = boring_display_surface_add(&core, endpoint, r, attachment, map, &token);
                if (status == BORING_DISPLAY_STATUS_OK) { attachment = 0U; }
                else { (void)boring_buffer_unmap(map); }
            } else { status = BORING_DISPLAY_STATUS_NO_SPACE; }
        }
    } else if ((attachment == 0U) && (r->reserved == 0U) && (r->width == 0U) &&
               (r->height == 0U) && (r->stride == 0U) && (r->byte_size == 0ULL) &&
               (r->pixel_format == 0U)) {
        if (r->type == BORING_DISPLAY_REQUEST_COMMIT) {
            status = boring_display_surface_commit(&core, endpoint, token);
            if (status == BORING_DISPLAY_STATUS_OK) { present(); }
        } else if (r->type == BORING_DISPLAY_REQUEST_DESTROY) {
            uint32_t handle = 0U; uint8_t *map = NULL;
            status = boring_display_surface_destroy(&core, endpoint, token, &handle, &map);
            if (status == BORING_DISPLAY_STATUS_OK) {
                display_managed_forget(&managed, token);
                (void)boring_buffer_unmap(map); (void)boring_buffer_close(handle); present();
            }
        }
    }
    if (attachment != 0U) { (void)boring_buffer_close(attachment); }
    reply.status = status; reply.surface_token = status == BORING_DISPLAY_STATUS_OK ? token : 0U;
    send_message(endpoint, &reply, sizeof(reply));
}

static void control(uint32_t endpoint, const struct display_control *r) {
    uint32_t status = display_control_validate(r, sizeof(*r));
    long peer = boring_endpoint_peer(endpoint);
    if ((status == BORING_DISPLAY_STATUS_OK) && (peer <= 0L)) { forget_peer(endpoint); return; }
    if (status == BORING_DISPLAY_STATUS_OK) {
        if (r->type == DISPLAY_INFO) {
            /* Read-only scanout metadata is public, no management authority. */
        } else if (r->type == DISPLAY_MANAGER) {
            long probe = boring_service_connect(BORING_WM_SERVICE, BORING_WM_SERVICE_LENGTH);
            if ((managed.manager_endpoint != 0U) || manager_seen || (probe <= 0L)) {
                status = BORING_DISPLAY_STATUS_ACCESS;
            } else if (boring_endpoint_peer((uint32_t)probe) != peer) {
                status = BORING_DISPLAY_STATUS_ACCESS;
            } else {
                managed.manager_endpoint = endpoint; manager_seen = true;
                desktop_say("display: exact manager endpoint authenticated\n");
            }
            if (probe > 0L) { (void)boring_ipc_close((uint32_t)probe); }
        } else if (r->type == DISPLAY_INPUT_ACK) {
            if ((endpoint == managed.manager_endpoint) && input_pending) {
                input_pending = false;
                return; /* ACK itself has no reply. */
            }
            status = BORING_DISPLAY_STATUS_ACCESS;
        } else {
            status = display_managed_control(&managed, &core, endpoint, (uint64_t)peer, r);
            if ((status == BORING_DISPLAY_STATUS_OK) && (r->type == DISPLAY_PRESENT)) { present(); }
        }
    }
    control_reply(endpoint, status, r->surface);
}

static void receive(uint32_t endpoint) {
    union {
        uint8_t bytes[BORING_IPC_INLINE_PAYLOAD_MAX];
        struct display_control control;
        struct boring_display_request old;
    } payload = {0};
    struct boring_ipc_receive_result result;
    long status = boring_ipc_receive(endpoint, &payload, sizeof(payload), &result);
    if (status != 0L) { forget_peer(endpoint); return; }
    if ((result.payload_length == sizeof(payload.old)) &&
        (payload.old.version == BORING_DISPLAY_PROTOCOL_VERSION)) {
        old_request(endpoint, &payload.old, result.buffer_handle);
        return;
    }
    if (result.buffer_handle != 0U) { (void)boring_buffer_close(result.buffer_handle); }
    if ((result.payload_length == sizeof(payload.control)) && (result.buffer_handle == 0U)) {
        control(endpoint, &payload.control);
    } else { control_reply(endpoint, BORING_DISPLAY_STATUS_INVALID, 0U); }
}

static void input(void) {
    struct boring_input_event event;
    struct display_event message = {0};
    if (boring_input_read(&event, 1U) != 1L) { desktop_fail("display M31 read"); }
#ifdef BORING_M38_DISPLAY_DEATH_ACCEPTANCE
    if ((event.type == BORING_INPUT_EVENT_KEY) &&
        (event.code == BORING_KEY_F11) &&
        (event.value1 == BORING_KEY_DOWN_VALUE)) {
        desktop_say("display: M38 test-only unexpected Ring3 exit\n");
        boring_exit(73);
    }
#endif
    if (event.type == BORING_INPUT_EVENT_MOUSE_MOVE) {
        boring_display_cursor_move(&core, event.value1, event.value2); present();
    }
    if (managed.manager_endpoint == 0U) { return; }
    message.version = BORING_DISPLAY_CONTROL_VERSION; message.type = DISPLAY_INPUT;
    message.input = event; message.cursor_x = core.cursor_x; message.cursor_y = core.cursor_y;
    input_pending = true;
    send_message(managed.manager_endpoint, &message, sizeof(message));
}

int boring_main(void) {
    struct boring_display_scanout_info info;
    long listener, buffer;
    if ((boring_framebuffer_claim(&info) != 0L) || !boring_display_core_init(&core, &info)) {
        desktop_fail("display claim/init");
    }
    display_managed_init(&managed);
    buffer = boring_buffer_create((size_t)info.byte_size);
    if (buffer <= 0L) { desktop_fail("display composition buffer"); }
    composition = (uint32_t)buffer; pixels = boring_buffer_map(composition);
    if (pixels == NULL) { desktop_fail("display composition mapping"); }
    listener = boring_service_register(BORING_DISPLAY_SERVICE_NAME, BORING_DISPLAY_SERVICE_NAME_LENGTH);
    if ((listener <= 0L) || (boring_input_claim() != 0L)) { desktop_fail("display service/input claim"); }
    present(); desktop_say("display: M35 service and M31 input ready\n");
    for (;;) {
        struct boring_event_watch watches[DISPLAY_PEERS + 2U] = {0};
        size_t count = 0U, index, live = 0U;
        watches[count++] = (struct boring_event_watch){BORING_EVENT_IPC, (uint32_t)listener, 0U, 0U, 0ULL};
        if (!input_pending) { watches[count++] = (struct boring_event_watch){BORING_EVENT_INPUT, 0U, 0U, 0U, 0ULL}; }
        for (index = 0U; index < DISPLAY_PEERS; ++index) {
            if (peers[index] != 0U) {
                watches[count++] = (struct boring_event_watch){BORING_EVENT_IPC, peers[index], 0U, 0U, 0ULL}; ++live;
            }
        }
        if (manager_seen && (managed.manager_endpoint == 0U) && (live == 0U)) {
            desktop_say("display: session drained; exiting with claims\n"); boring_exit(0);
        }
        if (boring_event_wait(watches, count, 0U) <= 0L) { desktop_fail("display event wait"); }
        for (index = 0U; index < count; ++index) {
            if (watches[index].events == 0U) { continue; }
            if (watches[index].kind == BORING_EVENT_INPUT) { input(); }
            else if (watches[index].handle == (uint32_t)listener) {
                long ep = boring_service_accept((uint32_t)listener); size_t slot;
                if (ep <= 0L) { desktop_fail("display accept"); }
                for (slot = 0U; slot < DISPLAY_PEERS; ++slot) { if (peers[slot] == 0U) { break; } }
                if (slot == DISPLAY_PEERS) { (void)boring_ipc_close((uint32_t)ep); }
                else { peers[slot] = (uint32_t)ep; }
            } else if ((watches[index].events & BORING_EVENT_HUP) != 0U) { forget_peer(watches[index].handle); }
            else { receive(watches[index].handle); }
        }
    }
}
