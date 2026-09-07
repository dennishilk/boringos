#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <boring/client.h>
#include <boring/display.h>
#include <boring/display_control.h>
#include <boring/ipc.h>
#include <boring/syscall.h>

/* Deterministic transport faults supplement, never replace, real QEMU tests. */
static unsigned calls, fault, malformed, async_type, closes;
static uint32_t request_type;
static size_t request_size;
static unsigned char pixels[800U * 600U * 4U];
static bool trip(void) { ++calls; return calls == fault; }
static void reset(void) {
    calls = fault = malformed = async_type = closes = 0U;
    request_type = 0U; request_size = 0U;
}

long boring_service_connect(const char *name, size_t length) {
    assert(strlen(name) == length);
    if (trip()) { return -1L; }
    return strcmp(name, BORING_WM_SERVICE) == 0 ? 2L : 1L;
}
long boring_buffer_create(size_t length) {
    assert(length == sizeof(pixels));
    return trip() ? -1L : 3L;
}
void *boring_buffer_map(uint32_t handle) {
    assert(handle == 3U);
    return trip() ? NULL : pixels;
}
long boring_buffer_unmap(void *address) {
    assert(address == pixels);
    return trip() ? -1L : 0L;
}
long boring_buffer_close(uint32_t handle) {
    assert(handle == 3U);
    return trip() ? -1L : 0L;
}
long boring_ipc_close(uint32_t handle) {
    assert(handle == 1U || handle == 2U);
    ++closes;
    return 0L;
}
long boring_ipc_send(uint32_t endpoint, const void *payload, size_t length,
                     uint32_t attachment) {
    const uint32_t *words = payload;
    assert(endpoint == 1U || endpoint == 2U);
    request_type = words[1]; request_size = length;
    if (endpoint == 1U && length == sizeof(struct boring_display_request) &&
        request_type == BORING_DISPLAY_REQUEST_CREATE) {
        const struct boring_display_request *r = payload;
        assert(attachment == 3U && r->width == 800U && r->height == 600U);
        assert(r->stride == 3200U && r->byte_size == sizeof(pixels));
    } else { assert(attachment == 0U); }
    return trip() ? -1L : 0L;
}
long boring_ipc_receive(uint32_t endpoint, void *payload, size_t capacity,
                        struct boring_ipc_receive_result *result) {
    if (trip()) { return -1L; }
    memset(payload, 0, capacity);
    memset(result, 0, sizeof(*result));
    result->payload_length = capacity;
    if (endpoint == 2U) {
        struct boring_wm_message *r = payload;
        assert(capacity == sizeof(*r));
        r->version = BORING_WM_VERSION;
        r->type = async_type != 0U ? async_type : BORING_WM_REPLY;
        async_type = 0U;
        r->token = 5U;
        if (malformed == 5U) { r->token = 0U; }
        if (malformed == 6U) { r->status = BORING_WM_INVALID; }
    } else if (request_size == sizeof(struct display_control)) {
        struct display_event *r = payload;
        assert(capacity == sizeof(*r));
        r->version = BORING_DISPLAY_CONTROL_VERSION; r->type = DISPLAY_REPLY;
        r->width = 800U; r->height = 600U;
        if (malformed == 4U) { r->width = BORING_DISPLAY_MAX_WIDTH + 1U; }
        if (malformed == 5U) { r->height = 0U; }
        if (malformed == 6U) { r->status = BORING_DISPLAY_STATUS_INVALID; }
        if (malformed == 7U) { r->type = DISPLAY_INPUT; }
    } else {
        struct boring_display_reply *r = payload;
        assert(capacity == sizeof(*r));
        r->version = BORING_DISPLAY_PROTOCOL_VERSION; r->surface_token = 4U;
        if (malformed == 5U) { r->surface_token = 0U; }
        if (malformed == 6U) { r->status = BORING_DISPLAY_STATUS_INVALID; }
    }
    if (malformed == 1U) { --result->payload_length; }
    if (malformed == 2U) { result->buffer_handle = 99U; }
    if (malformed == 3U) { ((uint32_t *)payload)[0] = 999U; }
    return 0L;
}

static bool lifecycle(struct boring_client *c) {
    struct boring_wm_message event;
    return boring_client_open(c) && boring_client_publish(c) &&
           boring_client_commit(c) && boring_client_receive(c, &event) &&
           boring_client_unregister(c) && boring_client_release(c);
}

int main(void) {
    struct boring_client c = {0};
    unsigned total;
    reset();
    assert(lifecycle(&c));
    total = calls;
    assert(c.display == 0U && c.manager == 0U && c.surface == 0U);
    assert(c.window_token == 0U && c.buffer_handle == 0U && c.pixels == NULL);
    assert(closes == 2U);
    assert(boring_client_unregister(&c) && boring_client_release(&c));
    assert(calls == total && closes == 2U);
    /* Every fallible transport/buffer call in the complete lifecycle. */
    for (unsigned i = 1U; i <= total; ++i) {
        c = (struct boring_client){0}; reset(); fault = i;
        assert(!lifecycle(&c)); assert(c.error != NULL); assert(calls == i);
    }
    for (unsigned i = 1U; i <= 7U; ++i) {
        c = (struct boring_client){0}; reset(); malformed = i;
        assert(!boring_client_open(&c)); assert(c.error != NULL);
    }
    /* Surface response and event envelopes reject malformed peers. */
    for (unsigned i = 1U; i <= 6U; ++i) {
        if (i == 4U) { continue; }
        c = (struct boring_client){0}; reset(); assert(boring_client_open(&c));
        malformed = i; assert(!boring_client_publish(&c));
        c = (struct boring_client){0}; reset(); assert(boring_client_open(&c));
        assert(boring_client_publish(&c)); malformed = i;
        if (i != 6U) {
            struct boring_wm_message event;
            assert(!boring_client_receive(&c, &event));
        } else { assert(!boring_client_unregister(&c)); }
    }
    c = (struct boring_client){0}; reset(); assert(boring_client_open(&c));
    async_type = BORING_WM_CONFIGURE; assert(boring_client_publish(&c));
    assert(!boring_client_release(&c)); /* Never destroy a registered window. */
    async_type = BORING_WM_KEY; assert(boring_client_unregister(&c));
    assert(boring_client_release(&c));
    c = (struct boring_client){0}; reset(); assert(boring_client_open(&c));
    async_type = BORING_WM_KEY; assert(!boring_client_publish(&c));
    c = (struct boring_client){0}; reset(); assert(boring_client_open(&c));
    assert(boring_client_publish(&c)); async_type = BORING_WM_CLOSE;
    assert(boring_client_unregister(&c)); assert(boring_client_release(&c));
    c = (struct boring_client){0}; reset(); assert(boring_client_open(&c));
    assert(boring_client_publish(&c)); malformed = 6U;
    assert(!boring_client_commit(&c));
    malformed = 0U; assert(boring_client_unregister(&c)); malformed = 6U;
    assert(!boring_client_release(&c));
    assert(c.surface == 4U && c.pixels == pixels && c.buffer_handle == 3U);
    printf("Native client lifecycle, %u injected call failures, malformed envelopes and close ordering passed.\n", total);
    return 0;
}
