#include <boring/desktop_log.h>
#include <boring/display.h>
#include <boring/display_control.h>
#include <boring/event.h>
#include <boring/ipc.h>
#include <boring/wm.h>

#ifndef WM_CLIENT_ID
#error WM_CLIENT_ID is required
#endif
static uint32_t display, manager, surface, window, buffer;
static uint8_t *pixels;
static uint32_t width, height;
int boring_main(void);

static void witness(const char *text) {
    const char *name = WM_CLIENT_ID == 1 ? "wm-client-a: " : WM_CLIENT_ID == 2 ? "wm-client-b: " : "wm-client-c: ";
    char line[64]; size_t prefix = boring_strlen(name), length = boring_strlen(text);
    if (prefix + length >= sizeof(line)) { desktop_fail("client witness length"); }
    boring_memcpy(line, name, prefix); boring_memcpy(line + prefix, text, length + 1U); desktop_say(line);
}

static struct display_event control_rpc(struct display_control *r) {
    struct display_event reply; struct boring_ipc_receive_result received;
    if ((boring_ipc_send(display, r, sizeof(*r), 0U) != 0L) ||
        (boring_ipc_receive(display, &reply, sizeof(reply), &received) != 0L) ||
        (received.payload_length != sizeof(reply)) || (received.buffer_handle != 0U) ||
        (reply.version != BORING_DISPLAY_CONTROL_VERSION) || (reply.type != DISPLAY_REPLY)) {
        desktop_fail("client display control");
    }
    return reply;
}

static struct boring_display_reply surface_rpc(struct boring_display_request *r, uint32_t attachment) {
    struct boring_display_reply reply; struct boring_ipc_receive_result received;
    if ((boring_ipc_send(display, r, sizeof(*r), attachment) != 0L) ||
        (boring_ipc_receive(display, &reply, sizeof(reply), &received) != 0L) ||
        (received.payload_length != sizeof(reply)) || (received.buffer_handle != 0U) ||
        (reply.version != BORING_DISPLAY_PROTOCOL_VERSION)) { desktop_fail("client surface RPC"); }
    return reply;
}

static struct boring_wm_message wm_rpc(const void *r, size_t size) {
    struct boring_wm_message reply; struct boring_ipc_receive_result received;
    if (boring_ipc_send(manager, r, size, 0U) != 0L) { desktop_fail("client WM send"); }
    for (;;) {
        if ((boring_ipc_receive(manager, &reply, sizeof(reply), &received) != 0L) ||
            (received.payload_length != sizeof(reply)) || (received.buffer_handle != 0U) ||
            (reply.version != BORING_WM_VERSION)) { desktop_fail("client WM receive"); }
        if (reply.type == BORING_WM_REPLY) { return reply; }
        if (reply.type != BORING_WM_CONFIGURE) { desktop_fail("client WM reply type"); }
    }
}

static void paint(void) {
    /* Original 5x7 glyphs: CLIENT A/B/C. The application owns every pixel. */
    static const uint8_t glyphs[8][7] = {
        {14,17,16,16,16,17,14}, {16,16,16,16,16,16,31},
        {31,4,4,4,4,4,31}, {31,16,16,30,16,16,31},
        {17,25,25,21,19,19,17}, {31,4,4,4,4,4,4},
        {0,0,0,0,0,0,0}, {0,0,0,0,0,0,0}
    };
    static const uint8_t letters[3][7] = {
        {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {14,17,16,16,16,17,14}
    };
    const uint32_t body = WM_CLIENT_ID == 1 ? 0x0032382aU : WM_CLIENT_ID == 2 ? 0x0026363cU : 0x003e2e34U;
    const uint32_t accent = WM_CLIENT_ID == 1 ? 0x00b8bb26U : WM_CLIENT_ID == 2 ? 0x0083a598U : 0x00d3869bU;
    uint32_t y;
    for (y = 0U; y < height; ++y) {
        uint32_t x;
        for (x = 0U; x < width; ++x) {
            uint32_t color = body;
            size_t offset = ((size_t)y * width + x) * 4U;
            if ((y >= 4U) && (y < 6U)) { color = accent; }
            if ((x >= 16U) && (x < 112U) && (y >= 20U) && (y < 34U)) {
                uint32_t glyph = (x - 16U) / 12U, column = ((x - 16U) % 12U) / 2U;
                uint32_t row = (y - 20U) / 2U;
                uint8_t bits = glyph == 7U ? letters[WM_CLIENT_ID - 1][row] : glyphs[glyph][row];
                if ((column < 5U) && ((bits & (uint8_t)(1U << (4U - column))) != 0U)) { color = 0x00ebdbb2U; }
            }
            pixels[offset] = (uint8_t)color; pixels[offset + 1U] = (uint8_t)(color >> 8U);
            pixels[offset + 2U] = (uint8_t)(color >> 16U); pixels[offset + 3U] = 0U;
        }
    }
}

static void malformed_tests(void) {
    struct boring_wm_message r = {0};
    r.version = BORING_WM_VERSION; r.type = BORING_WM_REGISTER; r.surface = UINT32_MAX;
    if (wm_rpc(&r, sizeof(r)).status != BORING_WM_ACCESS) { desktop_fail("invalid surface association"); }
    r.version = 99U;
    if (wm_rpc(&r, sizeof(r)).status != BORING_WM_INVALID) { desktop_fail("WM version validation"); }
    r.version = BORING_WM_VERSION; r.type = 99U;
    if (wm_rpc(&r, sizeof(r)).status != BORING_WM_INVALID) { desktop_fail("WM opcode validation"); }
    r.type = BORING_WM_REGISTER;
    if (wm_rpc(&r, sizeof(r) - 1U).status != BORING_WM_INVALID) { desktop_fail("WM size validation"); }
    r.width = 1U;
    if (wm_rpc(&r, sizeof(r)).status != BORING_WM_INVALID) { desktop_fail("WM geometry injection"); }
    witness("malformed requests rejected\n");
}

int boring_main(void) {
    long ep;
    struct display_control request = {0}; struct display_event info;
    struct boring_display_request create = {0}; struct boring_display_reply created;
    struct boring_wm_message registration = {0}, registered;
    ep = boring_service_connect(BORING_DISPLAY_SERVICE_NAME, BORING_DISPLAY_SERVICE_NAME_LENGTH);
    if (ep <= 0L) { desktop_fail("client display connect"); } display = (uint32_t)ep;
    ep = boring_service_connect(BORING_WM_SERVICE, BORING_WM_SERVICE_LENGTH);
    if (ep <= 0L) { desktop_fail("client WM connect"); } manager = (uint32_t)ep;
    request.version = BORING_DISPLAY_CONTROL_VERSION; request.type = DISPLAY_INFO;
    info = control_rpc(&request); width = info.width; height = info.height;
    if ((info.status != BORING_DISPLAY_STATUS_OK) || (width == 0U) || (height == 0U) ||
        (width > BORING_DISPLAY_MAX_WIDTH) || (height > BORING_DISPLAY_MAX_HEIGHT)) { desktop_fail("client scanout"); }
    ep = boring_buffer_create((size_t)width * height * 4U);
    if (ep <= 0L) { desktop_fail("client M32 buffer"); } buffer = (uint32_t)ep;
    pixels = boring_buffer_map(buffer); if (pixels == NULL) { desktop_fail("client buffer map"); } paint();
    create.version = BORING_DISPLAY_PROTOCOL_VERSION; create.type = BORING_DISPLAY_REQUEST_CREATE;
    create.width = width; create.height = height; create.stride = width * 4U;
    create.pixel_format = BORING_DISPLAY_PIXEL_FORMAT_XRGB8888; create.byte_size = (uint64_t)width * height * 4ULL;
    created = surface_rpc(&create, buffer);
    if ((created.status != BORING_DISPLAY_STATUS_OK) || (created.surface_token == 0U)) { desktop_fail("client surface create"); }
    surface = created.surface_token;
    request.type = DISPLAY_DELEGATE; request.surface = surface;
    if (control_rpc(&request).status != BORING_DISPLAY_STATUS_OK) { desktop_fail("client explicit delegation"); }
    witness("M32 buffer granted only to display\n");
    registration.version = BORING_WM_VERSION; registration.type = BORING_WM_REGISTER; registration.surface = surface;
    registered = wm_rpc(&registration, sizeof(registration));
    if ((registered.status != BORING_WM_OK) || (registered.token == 0U)) { desktop_fail("client WM registration"); }
    window = registered.token;
    if (wm_rpc(&registration, sizeof(registration)).status != BORING_WM_EXISTS) { desktop_fail("duplicate registration"); }
    registration.type = BORING_WM_QUERY; registration.surface = 0U; registration.token = window ^ 0x01000000U;
    if (wm_rpc(&registration, sizeof(registration)).status != BORING_WM_INVALID) { desktop_fail("stale token"); }
    if (WM_CLIENT_ID == 1) {
        uint32_t saved = manager;
        ep = boring_service_connect(BORING_WM_SERVICE, BORING_WM_SERVICE_LENGTH);
        if (ep <= 0L) { desktop_fail("hostile probe connection"); }
        manager = (uint32_t)ep; malformed_tests();
        (void)boring_ipc_close(manager); manager = saved;
    }
    if (WM_CLIENT_ID == 3) {
        registration.token = 257U;
        if (wm_rpc(&registration, sizeof(registration)).status != BORING_WM_ACCESS) { desktop_fail("foreign WM identity"); }
        witness("foreign client token rejected\n");
    }
    witness("managed ready\n");
    for (;;) {
        struct boring_wm_message event; struct boring_ipc_receive_result received;
        long status = boring_ipc_receive(manager, &event, sizeof(event), &received);
        if (status == -(long)BORING_SYSCALL_EPIPE) {
            request = (struct display_control){0}; request.version = BORING_DISPLAY_CONTROL_VERSION; request.type = DISPLAY_INFO;
            if (control_rpc(&request).status != BORING_DISPLAY_STATUS_OK) { desktop_fail("display survives WM death"); }
            witness("WM gone; app and display survived\n"); boring_exit(0);
        }
        if ((status != 0L) || (received.payload_length != sizeof(event)) ||
            (received.buffer_handle != 0U) || (event.version != BORING_WM_VERSION) ||
            (event.token != window)) { desktop_fail("client event envelope"); }
        if (event.type == BORING_WM_CLOSE) {
            witness("graceful close received\n");
            create = (struct boring_display_request){0}; create.version = BORING_DISPLAY_PROTOCOL_VERSION;
            create.type = BORING_DISPLAY_REQUEST_DESTROY; create.surface_token = surface;
            if (surface_rpc(&create, 0U).status != BORING_DISPLAY_STATUS_OK) { desktop_fail("client destroy"); }
            if ((boring_buffer_unmap(pixels) != 0L) || (boring_buffer_close(buffer) != 0L)) { desktop_fail("client own buffer cleanup"); }
            witness("surface/buffer closed; normal exit\n"); boring_exit(0);
        }
        if ((event.type == BORING_WM_KEY) && (event.surface == BORING_KEY_X) &&
            (event.x == BORING_KEY_DOWN_VALUE) && (event.y == 0U)) {
            witness("unsolicited exit with live resources\n"); boring_exit(17);
        }
        if ((event.type != BORING_WM_CONFIGURE) && (event.type != BORING_WM_KEY)) { desktop_fail("client event type"); }
    }
}
