#include <stddef.h>
#include <stdint.h>

#include <boring/display.h>
#include <boring/input_abi.h>
#include <boring/ipc.h>
#include <boring/string.h>
#include <boring/syscall.h>
#include <boring/syscall_abi.h>

#include "core.h"

static struct boring_display_core display_core;
static uint32_t composition_handle;
static uint8_t *composition_pixels;

static void say(const char *text) {
    (void)boring_debug_write(text, boring_strlen(text));
}

static void fail(const char *text) __attribute__((noreturn));
static void fail(const char *text) {
    say("boring-display: FAILED: ");
    say(text);
    say("\n");
    boring_exit(90);
}

static void require_zero(long result, const char *name) {
    if (result != 0L) {
        fail(name);
    }
}

static void reply_send(uint32_t endpoint, uint32_t status, uint32_t token) {
    struct boring_display_reply reply;

    reply.version = BORING_DISPLAY_PROTOCOL_VERSION;
    reply.status = status;
    reply.surface_token = token;
    reply.reserved = 0U;
    require_zero(boring_ipc_send(endpoint, &reply, sizeof(reply),
                                 BORING_IPC_NO_ATTACHED_BUFFER),
                 "reply send");
}

static long request_receive(uint32_t endpoint,
                            struct boring_display_request *request,
                            struct boring_ipc_receive_result *received) {
    return boring_ipc_receive(endpoint, request, sizeof(*request), received);
}

static void compose_present(void) {
    if ((composition_pixels == NULL) ||
        (display_core.byte_size > (uint64_t)SIZE_MAX) ||
        !boring_display_compose(&display_core, composition_pixels,
                                (size_t)display_core.byte_size)) {
        fail("compose");
    }
    require_zero(boring_framebuffer_present(composition_handle), "present");
}

static uint32_t create_surface(uint32_t endpoint) {
    struct boring_display_request request;
    struct boring_ipc_receive_result received;
    long result;
    long actual_size;
    uint8_t *pixels;
    uint32_t status;
    uint32_t token = 0U;

    result = request_receive(endpoint, &request, &received);
    require_zero(result, "create receive");
    if ((received.payload_length != sizeof(request)) ||
        (received.buffer_handle == BORING_BUFFER_HANDLE_INVALID)) {
        fail("create envelope");
    }
    actual_size = boring_buffer_info(received.buffer_handle);
    if (actual_size <= 0L) {
        (void)boring_buffer_close(received.buffer_handle);
        fail("buffer info");
    }
    status = boring_display_validate_create(&display_core, &request,
                                            (uint64_t)actual_size);
    if (status != BORING_DISPLAY_STATUS_OK) {
        (void)boring_buffer_close(received.buffer_handle);
        reply_send(endpoint, status, 0U);
        return 0U;
    }
    pixels = (uint8_t *)boring_buffer_map(received.buffer_handle);
    if (pixels == NULL) {
        (void)boring_buffer_close(received.buffer_handle);
        fail("surface map");
    }
    status = boring_display_surface_add(&display_core, endpoint, &request,
                                        received.buffer_handle, pixels, &token);
    if (status != BORING_DISPLAY_STATUS_OK) {
        (void)boring_buffer_unmap(pixels);
        (void)boring_buffer_close(received.buffer_handle);
        reply_send(endpoint, status, 0U);
        return 0U;
    }
    reply_send(endpoint, BORING_DISPLAY_STATUS_OK, token);
    return token;
}

static void receive_commit(uint32_t endpoint,
                           uint32_t expected_status,
                           uint32_t expected_token) {
    struct boring_display_request request;
    struct boring_ipc_receive_result received;
    uint32_t status;

    require_zero(request_receive(endpoint, &request, &received),
                 "commit receive");
    if ((received.payload_length != sizeof(request)) ||
        (received.buffer_handle != BORING_BUFFER_HANDLE_INVALID) ||
        (request.version != BORING_DISPLAY_PROTOCOL_VERSION) ||
        (request.type != BORING_DISPLAY_REQUEST_COMMIT) ||
        (request.reserved != 0U)) {
        fail("commit envelope");
    }
    status = boring_display_surface_commit(&display_core, endpoint,
                                           request.surface_token);
    if ((status != expected_status) ||
        ((expected_token != 0U) && (request.surface_token != expected_token))) {
        fail("commit authority");
    }
    if (status == BORING_DISPLAY_STATUS_OK) {
        compose_present();
    }
    reply_send(endpoint, status, request.surface_token);
}

static void cleanup_peer(uint32_t endpoint) {
    uint32_t handles[BORING_DISPLAY_SURFACE_MAX];
    uint8_t *pixels[BORING_DISPLAY_SURFACE_MAX];
    size_t count;
    size_t index;

    count = boring_display_peer_cleanup(&display_core, endpoint, handles, pixels);
    for (index = 0U; index < count; ++index) {
        require_zero(boring_buffer_unmap(pixels[index]), "peer surface unmap");
        require_zero(boring_buffer_close(handles[index]), "peer surface close");
    }
}

static void receive_destroy(uint32_t endpoint, uint32_t expected_token) {
    struct boring_display_request request;
    struct boring_ipc_receive_result received;
    uint32_t handle = 0U;
    uint8_t *pixels = NULL;
    uint32_t status;

    require_zero(request_receive(endpoint, &request, &received),
                 "destroy receive");
    if ((received.payload_length != sizeof(request)) ||
        (received.buffer_handle != BORING_BUFFER_HANDLE_INVALID) ||
        (request.version != BORING_DISPLAY_PROTOCOL_VERSION) ||
        (request.type != BORING_DISPLAY_REQUEST_DESTROY) ||
        (request.surface_token != expected_token)) {
        fail("destroy envelope");
    }
    status = boring_display_surface_destroy(&display_core, endpoint,
                                            request.surface_token,
                                            &handle, &pixels);
    if (status != BORING_DISPLAY_STATUS_OK) {
        fail("destroy authority");
    }
    require_zero(boring_buffer_unmap(pixels), "destroy unmap");
    require_zero(boring_buffer_close(handle), "destroy close");
    compose_present();
    reply_send(endpoint, status, request.surface_token);
}

int boring_main(void) {
    struct boring_display_scanout_info info;
    struct boring_input_event events[BORING_INPUT_READ_MAX];
    struct boring_display_request closed_request;
    struct boring_ipc_receive_result closed_result;
    long listener_raw;
    long endpoint_a_raw;
    long endpoint_b_raw;
    long composition_raw;
    long result;
    uint32_t listener;
    uint32_t endpoint_a;
    uint32_t endpoint_b;
    uint32_t token_a;
    uint32_t token_b;
    unsigned int reads = 0U;

    if (boring_getpid() != 1ULL) {
        fail("service pid");
    }
    listener_raw = boring_service_register(BORING_DISPLAY_SERVICE_NAME,
                                           BORING_DISPLAY_SERVICE_NAME_LENGTH);
    if (listener_raw <= 0L) {
        fail("service register");
    }
    listener = (uint32_t)listener_raw;
    say("boring-display: service boring.display registered\n");

    require_zero(boring_framebuffer_claim(&info), "framebuffer claim");
    if (!boring_display_core_init(&display_core, &info)) {
        fail("scanout info");
    }
    composition_raw = boring_buffer_create((size_t)info.byte_size);
    if (composition_raw <= 0L) {
        fail("composition buffer create");
    }
    composition_handle = (uint32_t)composition_raw;
    composition_pixels = (uint8_t *)boring_buffer_map(composition_handle);
    if (composition_pixels == NULL) {
        fail("composition buffer map");
    }
    if (boring_buffer_info(composition_handle) != (long)info.byte_size) {
        fail("composition buffer info");
    }
    compose_present();
    say("boring-display: dark desktop presented\n");
    require_zero(boring_input_claim(), "input claim");
    say("boring-display: M31 input claimed\n");

    endpoint_a_raw = boring_service_accept(listener);
    if (endpoint_a_raw <= 0L) {
        fail("accept client A");
    }
    endpoint_a = (uint32_t)endpoint_a_raw;
    token_a = create_surface(endpoint_a);
    if (token_a == 0U) {
        fail("client A surface");
    }
    say("boring-display: client A surface created\n");
    receive_commit(endpoint_a, BORING_DISPLAY_STATUS_OK, token_a);
    say("boring-display: live shared-buffer COMMIT passed\n");

    endpoint_b_raw = boring_service_accept(listener);
    if (endpoint_b_raw <= 0L) {
        fail("accept client B");
    }
    endpoint_b = (uint32_t)endpoint_b_raw;
    token_b = create_surface(endpoint_b);
    if ((token_b == 0U) || (token_b == token_a)) {
        fail("client B surface");
    }
    say("boring-display: client B surface created\n");
    receive_commit(endpoint_b, BORING_DISPLAY_STATUS_ACCESS, 0U);
    say("boring-display: cross-client authority isolation passed\n");
    receive_commit(endpoint_b, BORING_DISPLAY_STATUS_OK, token_b);
    say("boring-display: deterministic stacking passed\n");

    result = request_receive(endpoint_a, &closed_request, &closed_result);
    if (result != -(long)BORING_SYSCALL_EPIPE) {
        fail("client A death witness");
    }
    cleanup_peer(endpoint_a);
    say("boring-display: client death cleanup passed\n");

    say("boring-display: waiting for real mouse input\n");
    while (((display_core.cursor_x + 1U) < display_core.width ||
            (display_core.cursor_y + 1U) < display_core.height) &&
           (reads < 64U)) {
        long count = boring_input_read(events, BORING_INPUT_READ_MAX);
        long index;
        if (count <= 0L) {
            fail("input read");
        }
        for (index = 0L; index < count; ++index) {
            if (events[index].type == BORING_INPUT_EVENT_MOUSE_MOVE) {
                boring_display_cursor_move(&display_core,
                                           events[index].value1,
                                           events[index].value2);
            }
        }
        ++reads;
    }
    if (((display_core.cursor_x + 1U) != display_core.width) ||
        ((display_core.cursor_y + 1U) != display_core.height)) {
        fail("cursor clipping");
    }
    compose_present();
    say("boring-display: real mouse cursor clipped and presented\n");
    reply_send(endpoint_b, BORING_DISPLAY_STATUS_OK, 0U);

    receive_destroy(endpoint_b, token_b);
    say("boring-display: client B surface destroyed\n");

    /* Deliberately leave service/input/framebuffer/composition claims open. */
    say("boring-display: exiting with live service and device claims\n");
    boring_exit(0);
}
