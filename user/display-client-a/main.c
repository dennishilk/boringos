#include <stddef.h>
#include <stdint.h>

#include <boring/ipc.h>
#include <boring/string.h>
#include <boring/syscall.h>
#include <boring/syscall_abi.h>
#include <boring/display_abi.h>

#define SURFACE_WIDTH 64U
#define SURFACE_HEIGHT 64U
#define SURFACE_STRIDE (SURFACE_WIDTH * BORING_DISPLAY_BYTES_PER_PIXEL)
#define SURFACE_BYTES ((size_t)SURFACE_STRIDE * (size_t)SURFACE_HEIGHT)

static void say(const char *text) {
    (void)boring_debug_write(text, boring_strlen(text));
}

static void fail(const char *text) __attribute__((noreturn));
static void fail(const char *text) {
    say("display-client-a: FAILED: ");
    say(text);
    say("\n");
    boring_exit(91);
}

static struct boring_display_reply receive_reply(uint32_t endpoint) {
    struct boring_display_reply reply;
    struct boring_ipc_receive_result received;

    if ((boring_ipc_receive(endpoint, &reply, sizeof(reply), &received) != 0L) ||
        (received.payload_length != sizeof(reply)) ||
        (received.buffer_handle != BORING_BUFFER_HANDLE_INVALID) ||
        (reply.version != BORING_DISPLAY_PROTOCOL_VERSION) ||
        (reply.reserved != 0U)) {
        fail("reply");
    }
    return reply;
}

int boring_main(void) {
    struct boring_display_request request;
    struct boring_display_reply reply;
    long endpoint_raw;
    long buffer_raw;
    uint32_t endpoint;
    uint32_t handle;
    uint8_t *pixels;
    size_t pixel;

    if (boring_getpid() != 2ULL) {
        fail("pid");
    }
    endpoint_raw = boring_service_connect(BORING_DISPLAY_SERVICE_NAME,
                                          BORING_DISPLAY_SERVICE_NAME_LENGTH);
    if (endpoint_raw <= 0L) {
        fail("connect");
    }
    endpoint = (uint32_t)endpoint_raw;
    buffer_raw = boring_buffer_create(SURFACE_BYTES);
    if (buffer_raw <= 0L) {
        fail("buffer create");
    }
    handle = (uint32_t)buffer_raw;
    pixels = (uint8_t *)boring_buffer_map(handle);
    if (pixels == NULL) {
        fail("buffer map");
    }
    for (pixel = 0U; pixel < SURFACE_BYTES; pixel += 4U) {
        pixels[pixel] = 0x30U;
        pixels[pixel + 1U] = 0x78U;
        pixels[pixel + 2U] = 0xd8U;
        pixels[pixel + 3U] = 0U;
    }

    request.version = BORING_DISPLAY_PROTOCOL_VERSION;
    request.type = BORING_DISPLAY_REQUEST_CREATE;
    request.surface_token = 0U;
    request.width = SURFACE_WIDTH;
    request.height = SURFACE_HEIGHT;
    request.stride = SURFACE_STRIDE;
    request.pixel_format = BORING_DISPLAY_PIXEL_FORMAT_XRGB8888;
    request.reserved = 0U;
    request.byte_size = (uint64_t)SURFACE_BYTES;
    if (boring_ipc_send(endpoint, &request, sizeof(request), handle) != 0L) {
        fail("create send");
    }
    reply = receive_reply(endpoint);
    if ((reply.status != BORING_DISPLAY_STATUS_OK) ||
        (reply.surface_token == BORING_DISPLAY_SURFACE_INVALID)) {
        fail("create reply");
    }
    say("display-client-a: shared surface granted\n");

    /* Mutate the original sender mapping after grant: service must see it. */
    pixels[0] = 0x40U;
    pixels[1] = 0xe0U;
    pixels[2] = 0x40U;
    request.type = BORING_DISPLAY_REQUEST_COMMIT;
    request.surface_token = reply.surface_token;
    request.width = 0U;
    request.height = 0U;
    request.stride = 0U;
    request.pixel_format = 0U;
    request.byte_size = 0ULL;
    if (boring_ipc_send(endpoint, &request, sizeof(request),
                        BORING_IPC_NO_ATTACHED_BUFFER) != 0L) {
        fail("commit send");
    }
    reply = receive_reply(endpoint);
    if ((reply.status != BORING_DISPLAY_STATUS_OK) ||
        (reply.surface_token != request.surface_token)) {
        fail("commit reply");
    }
    say("display-client-a: live COMMIT acknowledged\n");

    /*
     * Stay alive until boring-display has produced the deterministic visual
     * witness with both clients and the real cursor. The service then sends
     * one ordinary protocol reply solely as the M34 acceptance release.
     */
    reply = receive_reply(endpoint);
    if ((reply.status != BORING_DISPLAY_STATUS_OK) ||
        (reply.surface_token != BORING_DISPLAY_SURFACE_INVALID)) {
        fail("visual witness release");
    }

    /* Leave endpoint, mapping and original handle for process-exit cleanup. */
    say("display-client-a: exiting without destroy\n");
    boring_exit(0);
}
