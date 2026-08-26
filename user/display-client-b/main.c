#include <stddef.h>
#include <stdint.h>

#include <boring/ipc.h>
#include <boring/string.h>
#include <boring/syscall.h>
#include <boring/syscall_abi.h>
#include <boring/display_abi.h>

#define SURFACE_WIDTH 80U
#define SURFACE_HEIGHT 80U
#define SURFACE_STRIDE (SURFACE_WIDTH * BORING_DISPLAY_BYTES_PER_PIXEL)
#define SURFACE_BYTES ((size_t)SURFACE_STRIDE * (size_t)SURFACE_HEIGHT)
#define CLIENT_A_FIRST_TOKEN 0x00000101U

int boring_main(void);

static void say(const char *text) {
    (void)boring_debug_write(text, boring_strlen(text));
}

static void fail(const char *text) __attribute__((noreturn));
static void fail(const char *text) {
    say("display-client-b: FAILED: ");
    say(text);
    say("\n");
    boring_exit(92);
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

static void send_request(uint32_t endpoint,
                         const struct boring_display_request *request,
                         uint32_t attached) {
    if (boring_ipc_send(endpoint, request, sizeof(*request), attached) != 0L) {
        fail("request send");
    }
}

int boring_main(void) {
    struct boring_display_request request;
    struct boring_display_reply reply;
    long endpoint_raw;
    long buffer_raw;
    uint32_t endpoint;
    uint32_t handle;
    uint32_t own_token;
    uint8_t *pixels;
    size_t pixel;

    if (boring_getpid() != 3ULL) {
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
        pixels[pixel] = 0xb8U;
        pixels[pixel + 1U] = 0x68U;
        pixels[pixel + 2U] = 0x28U;
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
    send_request(endpoint, &request, handle);
    reply = receive_reply(endpoint);
    if ((reply.status != BORING_DISPLAY_STATUS_OK) ||
        (reply.surface_token == 0U) ||
        (reply.surface_token == CLIENT_A_FIRST_TOKEN)) {
        fail("create reply");
    }
    own_token = reply.surface_token;
    say("display-client-b: shared surface granted\n");

    request.type = BORING_DISPLAY_REQUEST_COMMIT;
    request.surface_token = CLIENT_A_FIRST_TOKEN;
    request.width = 0U;
    request.height = 0U;
    request.stride = 0U;
    request.pixel_format = 0U;
    request.byte_size = 0ULL;
    send_request(endpoint, &request, BORING_IPC_NO_ATTACHED_BUFFER);
    reply = receive_reply(endpoint);
    if ((reply.status != BORING_DISPLAY_STATUS_ACCESS) ||
        (reply.surface_token != CLIENT_A_FIRST_TOKEN)) {
        fail("foreign token accepted");
    }
    say("display-client-b: foreign token rejected\n");

    request.surface_token = own_token;
    send_request(endpoint, &request, BORING_IPC_NO_ATTACHED_BUFFER);
    reply = receive_reply(endpoint);
    if ((reply.status != BORING_DISPLAY_STATUS_OK) ||
        (reply.surface_token != own_token)) {
        fail("own commit");
    }
    say("display-client-b: own COMMIT acknowledged\n");

    reply = receive_reply(endpoint);
    if ((reply.status != BORING_DISPLAY_STATUS_OK) ||
        (reply.surface_token != 0U)) {
        fail("cursor completion signal");
    }

    request.type = BORING_DISPLAY_REQUEST_DESTROY;
    request.surface_token = own_token;
    send_request(endpoint, &request, BORING_IPC_NO_ATTACHED_BUFFER);
    reply = receive_reply(endpoint);
    if ((reply.status != BORING_DISPLAY_STATUS_OK) ||
        (reply.surface_token != own_token)) {
        fail("destroy reply");
    }
    say("display-client-b: destroy acknowledged\n");

    (void)boring_buffer_unmap(pixels);
    (void)boring_buffer_close(handle);
    (void)boring_ipc_close(endpoint);
    boring_exit(0);
}
