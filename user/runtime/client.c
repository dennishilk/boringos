#include <stddef.h>
#include <boring/client.h>
#include <boring/display.h>
#include <boring/display_control.h>
#include <boring/ipc.h>
#include <boring/syscall.h>

static bool fail(struct boring_client *c, const char *reason) {
    c->error = reason;
    return false;
}

static bool control_rpc(struct boring_client *c, const struct display_control *request,
                        struct display_event *reply) {
    struct boring_ipc_receive_result received;
    if (boring_ipc_send(c->display, request, sizeof(*request), 0U) != 0L ||
        boring_ipc_receive(c->display, reply, sizeof(*reply), &received) != 0L ||
        received.payload_length != sizeof(*reply) || received.buffer_handle != 0U ||
        reply->version != BORING_DISPLAY_CONTROL_VERSION || reply->type != DISPLAY_REPLY) {
        return fail(c, "display control RPC");
    }
    return true;
}

static bool surface_rpc(struct boring_client *c, const struct boring_display_request *request,
                        uint32_t attachment, struct boring_display_reply *reply) {
    struct boring_ipc_receive_result received;
    if (boring_ipc_send(c->display, request, sizeof(*request), attachment) != 0L ||
        boring_ipc_receive(c->display, reply, sizeof(*reply), &received) != 0L ||
        received.payload_length != sizeof(*reply) || received.buffer_handle != 0U ||
        reply->version != BORING_DISPLAY_PROTOCOL_VERSION) {
        return fail(c, "surface RPC");
    }
    return true;
}

static bool wm_rpc(struct boring_client *c, const struct boring_wm_message *request,
                   struct boring_wm_message *reply) {
    struct boring_ipc_receive_result received;
    if (boring_ipc_send(c->manager, request, sizeof(*request), 0U) != 0L) {
        return fail(c, "WM send");
    }
    for (;;) {
        if (boring_ipc_receive(c->manager, reply, sizeof(*reply), &received) != 0L ||
            received.payload_length != sizeof(*reply) || received.buffer_handle != 0U ||
            reply->version != BORING_WM_VERSION) {
            return fail(c, "WM receive");
        }
        if (reply->type == BORING_WM_REPLY) { return true; }
        /* Preserve existing REGISTER/UNREGISTER asynchronous event ordering. */
        if (reply->type != BORING_WM_CONFIGURE &&
            !(request->type == BORING_WM_UNREGISTER &&
              (reply->type == BORING_WM_KEY || reply->type == BORING_WM_CLOSE))) {
            return fail(c, "unexpected WM RPC event");
        }
    }
}

bool boring_client_open(struct boring_client *c) {
    struct display_control request = {0};
    struct display_event info;
    long handle = boring_service_connect(BORING_DISPLAY_SERVICE_NAME,
                                         BORING_DISPLAY_SERVICE_NAME_LENGTH);
    if (handle <= 0L) { return fail(c, "display connect"); }
    c->display = (uint32_t)handle;
    handle = boring_service_connect(BORING_WM_SERVICE, BORING_WM_SERVICE_LENGTH);
    if (handle <= 0L) { return fail(c, "WM connect"); }
    c->manager = (uint32_t)handle;
    request.version = BORING_DISPLAY_CONTROL_VERSION;
    request.type = DISPLAY_INFO;
    if (!control_rpc(c, &request, &info)) { return false; }
    if (info.status != BORING_DISPLAY_STATUS_OK || info.width == 0U || info.height == 0U ||
        info.width > BORING_DISPLAY_MAX_WIDTH || info.height > BORING_DISPLAY_MAX_HEIGHT) {
        return fail(c, "display geometry");
    }
    c->width = info.width;
    c->height = info.height;
    c->stride = c->width * 4U;
    handle = boring_buffer_create((size_t)c->stride * c->height);
    if (handle <= 0L) { return fail(c, "M32 buffer create"); }
    c->buffer_handle = (uint32_t)handle;
    c->pixels = boring_buffer_map(c->buffer_handle);
    if (c->pixels == NULL) { return fail(c, "M32 buffer map"); }
    return true;
}

bool boring_client_publish(struct boring_client *c) {
    struct boring_display_request create = {0};
    struct boring_display_reply created;
    struct display_control control = {0};
    struct display_event delegated;
    struct boring_wm_message registration = {0};
    struct boring_wm_message registered;
    create.version = BORING_DISPLAY_PROTOCOL_VERSION;
    create.type = BORING_DISPLAY_REQUEST_CREATE;
    create.width = c->width;
    create.height = c->height;
    create.stride = c->stride;
    create.pixel_format = BORING_DISPLAY_PIXEL_FORMAT_XRGB8888;
    create.byte_size = (uint64_t)c->stride * c->height;
    if (!surface_rpc(c, &create, c->buffer_handle, &created)) { return false; }
    if (created.status != BORING_DISPLAY_STATUS_OK || created.surface_token == 0U) {
        return fail(c, "surface create");
    }
    c->surface = created.surface_token;
    control.version = BORING_DISPLAY_CONTROL_VERSION;
    control.type = DISPLAY_DELEGATE;
    control.surface = c->surface;
    if (!control_rpc(c, &control, &delegated)) { return false; }
    if (delegated.status != BORING_DISPLAY_STATUS_OK) { return fail(c, "surface delegate"); }
    registration.version = BORING_WM_VERSION;
    registration.type = BORING_WM_REGISTER;
    registration.surface = c->surface;
    if (!wm_rpc(c, &registration, &registered)) { return false; }
    if (registered.status != BORING_WM_OK || registered.token == 0U) {
        return fail(c, "WM REGISTER");
    }
    c->window_token = registered.token;
    return true;
}

bool boring_client_commit(struct boring_client *c) {
    struct boring_display_request request = {0};
    struct boring_display_reply reply;
    request.version = BORING_DISPLAY_PROTOCOL_VERSION;
    request.type = BORING_DISPLAY_REQUEST_COMMIT;
    request.surface_token = c->surface;
    if (!surface_rpc(c, &request, 0U, &reply)) { return false; }
    return reply.status == BORING_DISPLAY_STATUS_OK || fail(c, "surface commit");
}

bool boring_client_receive(struct boring_client *c, struct boring_wm_message *event) {
    struct boring_ipc_receive_result received;
    if (boring_ipc_receive(c->manager, event, sizeof(*event), &received) != 0L ||
        received.payload_length != sizeof(*event) || received.buffer_handle != 0U ||
        event->version != BORING_WM_VERSION || event->token != c->window_token) {
        return fail(c, "WM event envelope");
    }
    return true;
}

bool boring_client_unregister(struct boring_client *c) {
    struct boring_wm_message request = {0};
    struct boring_wm_message reply;
    if (c->manager == 0U || c->window_token == 0U) { return true; }
    request.version = BORING_WM_VERSION;
    request.type = BORING_WM_UNREGISTER;
    request.token = c->window_token;
    if (!wm_rpc(c, &request, &reply)) { return false; }
    if (reply.status != BORING_WM_OK) { return fail(c, "WM unregister"); }
    c->window_token = 0U;
    return true;
}

bool boring_client_release(struct boring_client *c) {
    struct boring_display_request request = {0};
    struct boring_display_reply reply;
    if (c->window_token != 0U) { return fail(c, "unregister before release"); }
    if (c->display != 0U && c->surface != 0U) {
        request.version = BORING_DISPLAY_PROTOCOL_VERSION;
        request.type = BORING_DISPLAY_REQUEST_DESTROY;
        request.surface_token = c->surface;
        if (!surface_rpc(c, &request, 0U, &reply)) { return false; }
        if (reply.status != BORING_DISPLAY_STATUS_OK) { return fail(c, "surface destroy"); }
        c->surface = 0U;
    }
    if (c->pixels != NULL && boring_buffer_unmap(c->pixels) != 0L) {
        return fail(c, "buffer unmap");
    }
    c->pixels = NULL;
    if (c->buffer_handle != 0U && boring_buffer_close(c->buffer_handle) != 0L) {
        return fail(c, "buffer close");
    }
    c->buffer_handle = 0U;
    if (c->manager != 0U) { (void)boring_ipc_close(c->manager); c->manager = 0U; }
    if (c->display != 0U) { (void)boring_ipc_close(c->display); c->display = 0U; }
    return true;
}
