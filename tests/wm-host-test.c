#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../user/boringwm/core.h"
#include "../user/boring-display/managed.h"

static unsigned long checks;
static void check(bool condition, const char *name) {
    ++checks;
    if (!condition) { (void)fprintf(stderr, "wm-host-test FAILED: %s\n", name); exit(1); }
}
static uint32_t add(struct wm_core *wm, uint32_t n) {
    uint32_t token = 0U;
    check(wm_add(wm, n, n, 100U + n, &token) == BORING_WM_OK, "add client");
    return token;
}
static void geometry(const struct wm_core *wm) {
    uint32_t i, j, focuses = 0U;
    for (i = 0U; i < wm->count; ++i) {
        const struct wm_client *c = &wm->clients[wm->order[i]];
        const struct wm_rect *r = &c->rect;
        check(c->active && wm_lookup(wm, c->token) == c, "stable tile identity");
        check(r->x <= wm->width && r->y <= wm->height && r->width <= wm->width - r->x &&
              r->height <= wm->height - r->y, "rectangle clipped");
        check(r->border <= r->width / 2U && r->border <= r->height / 2U, "border clipped");
        if (r->width != 0U && r->height != 0U) {
            check(r->width > 2U * r->border && r->height > 2U * r->border, "positive interior");
        }
        if (wm->focus == c->token) { ++focuses; }
        for (j = 0U; j < i; ++j) {
            const struct wm_rect *s = &wm->clients[wm->order[j]].rect;
            check(r->width == 0U || r->height == 0U || s->width == 0U || s->height == 0U ||
                  r->x + r->width <= s->x || s->x + s->width <= r->x ||
                  r->y + r->height <= s->y || s->y + s->height <= r->y, "no overlap");
        }
    }
    check(focuses == (wm->count != 0U ? 1U : 0U), "exactly one or zero focus");
}

static void layouts(void) {
    struct wm_core wm;
    uint32_t width, height, count;
    check(!wm_init(&wm, 0U, 1U) && !wm_init(&wm, UINT32_MAX, 1U) &&
          !wm_init(&wm, 1U, UINT32_MAX), "invalid screen dimensions");
    check(wm_init(&wm, 800U, 600U), "init"); wm_layout(&wm); geometry(&wm);
    (void)add(&wm, 1U);
    check(wm.clients[0].rect.x == 4U && wm.clients[0].rect.y == 4U &&
          wm.clients[0].rect.width == 792U && wm.clients[0].rect.height == 592U &&
          wm.clients[0].rect.border == 3U, "one client outer gaps border");
    (void)add(&wm, 2U);
    check(wm.clients[0].rect.width == 470U && wm.clients[1].rect.x == 482U &&
          wm.clients[1].rect.width == 314U && wm.clients[1].rect.height == 592U, "two client 3/5 master");
    (void)add(&wm, 3U);
    check(wm.clients[1].rect.height == 292U && wm.clients[2].rect.y == 304U &&
          wm.clients[2].rect.height == 292U, "three client stack gap");
    check(wm_init(&wm, 801U, 601U), "odd screen");
    (void)add(&wm, 1U); (void)add(&wm, 2U); (void)add(&wm, 3U);
    check(wm.clients[1].rect.height == 293U && wm.clients[2].rect.height == 292U &&
          wm.clients[2].rect.y + wm.clients[2].rect.height == 597U, "early remainder pixel");
    for (width = 1U; width <= 90U; ++width) {
        for (height = 1U; height <= 90U; ++height) {
            struct wm_core copy;
            check(wm_init(&wm, width, height), "tiny init"); geometry(&wm);
            for (count = 1U; count <= BORING_WM_CLIENT_MAX; ++count) {
                (void)add(&wm, count); geometry(&wm); copy = wm; wm_layout(&wm);
                check(memcmp(&copy, &wm, sizeof(wm)) == 0, "deterministic retile");
            }
        }
    }
    check(wm_init(&wm, BORING_DISPLAY_MAX_WIDTH, BORING_DISPLAY_MAX_HEIGHT), "maximum screen");
    for (count = 1U; count <= BORING_WM_CLIENT_MAX; ++count) { (void)add(&wm, count); geometry(&wm); }
}

static void policy(void) {
    struct wm_core wm; uint32_t a, b, c, token, i;
    struct boring_input_event key = {BORING_INPUT_EVENT_KEY, BORING_KEY_J, 1, 0, BORING_MOD_SUPER, 0U};
    check(wm_init(&wm, 800U, 600U), "policy init");
    check(!wm_focus_step(&wm, 1) && !wm_reorder(&wm, -1), "empty navigation");
    a = add(&wm, 1U); b = add(&wm, 2U); c = add(&wm, 3U);
    check(wm.focus == c, "new client takes focus");
    check(wm_key(&wm, &key) == WM_FOCUS && wm.focus == a, "real normalized super J wraps");
    check(wm_focus_step(&wm, 1) && wm.focus == b, "focus next");
    check(wm_focus_step(&wm, -1) && wm.focus == a, "focus previous");
    check(wm_reorder(&wm, 1) && wm.order[0] == 1U && wm.order[1] == 0U && wm.focus == a, "master replacement");
    check(wm_reorder(&wm, -1) && wm.order[0] == 0U, "reorder back");
    check(!wm_reorder(&wm, -1), "reorder clamps at start");
    check(!wm_pointer(&wm, 0U, 0U), "gap does not focus");
    check(wm_pointer(&wm, 500U, 500U) && wm.focus == c, "mouse actual tile");
    check(!wm_pointer(&wm, UINT32_MAX, UINT32_MAX), "out of bounds pointer");
    key.code = BORING_KEY_Q;
    check(wm_key(&wm, &key) == WM_CLOSE && wm.clients[2].closing, "graceful close requested");
    check(wm_key(&wm, &key) == WM_NONE, "repeat close is idempotent");
    check(wm_remove(&wm, 99U, c) == BORING_WM_ACCESS && wm.count == 3U, "foreign removal rejected");
    check(wm_remove(&wm, 3U, c) == BORING_WM_OK && wm.focus == a, "focus after last tile close wraps");
    check(wm_remove(&wm, 3U, c) == BORING_WM_INVALID, "stale repeated remove");
    check(wm_remove(&wm, 1U, a) == BORING_WM_OK && wm.focus == b && wm.order[0] == 1U, "master crash repaired");
    token = add(&wm, 4U);
    check(token != a && wm_lookup(&wm, a) == NULL, "generation reuse rejects old identity");
    check(wm_add(&wm, 4U, 4ULL, 222U, &i) == BORING_WM_EXISTS, "duplicate endpoint");
    check(wm_add(&wm, 5U, 5ULL, 104U, &i) == BORING_WM_EXISTS, "duplicate surface");
    key.code = BORING_KEY_ENTER;
    check(wm_key(&wm, &key) == WM_NO_LAUNCHER, "no fake terminal");
    key.code = BORING_KEY_J; key.flags = BORING_INPUT_FLAG_REPEAT;
    check(wm_key(&wm, &key) == WM_NONE, "repeat ignored");
    key.flags = 0U; key.modifiers |= BORING_MOD_CTRL;
    check(wm_key(&wm, &key) == WM_NONE, "extra modifiers ignored");
    for (i = 0U; i < BORING_WM_CLIENT_MAX; ++i) {
        if (wm.clients[i].active) { check(wm_remove(&wm, wm.clients[i].endpoint, wm.clients[i].token) == BORING_WM_OK, "cleanup"); }
    }
    geometry(&wm);
    for (i = 1U; i <= BORING_WM_CLIENT_MAX; ++i) { (void)add(&wm, i); }
    check(wm_add(&wm, 20U, 20ULL, 200U, &token) == BORING_WM_NO_SPACE, "client table full");
    check(wm_init(&wm, 20U, 20U), "generation retirement init");
    wm.clients[0].generation = 0x00ffffffU;
    a = add(&wm, 1U); check(wm_remove(&wm, 1U, a) == BORING_WM_OK, "retire generation");
    b = add(&wm, 2U); check((b & 255U) == 2U && wm.clients[0].generation == 0U, "no generation ABA wrap");
}

static void protocol(void) {
    struct boring_wm_message m = {0};
    uint32_t opcode;
    m.version = BORING_WM_VERSION; m.type = BORING_WM_REGISTER; m.surface = 1U;
    check(wm_validate_request(&m, sizeof(m)) == BORING_WM_OK, "register envelope");
    check(wm_validate_request(&m, sizeof(m) - 1U) == BORING_WM_INVALID, "truncated message");
    check(wm_validate_request(&m, sizeof(m) + 1U) == BORING_WM_INVALID, "oversized message");
    m.version = 0U; check(wm_validate_request(&m, sizeof(m)) == BORING_WM_INVALID, "invalid version"); m.version = BORING_WM_VERSION;
    m.x = UINT32_MAX; check(wm_validate_request(&m, sizeof(m)) == BORING_WM_INVALID, "geometry injection"); m.x = 0U;
    m.reserved = 1U; check(wm_validate_request(&m, sizeof(m)) == BORING_WM_INVALID, "reserved field"); m.reserved = 0U;
    for (opcode = 4U; opcode < 256U; ++opcode) {
        m.type = opcode; check(wm_validate_request(&m, sizeof(m)) == BORING_WM_INVALID, "app cannot send WM commands/events");
    }
}

static void display_authority(void) {
    struct boring_display_core core;
    struct display_managed state;
    struct boring_display_scanout_info info = {BORING_DISPLAY_SCANOUT_VERSION, 20U, 20U, 80U, 1600ULL};
    struct boring_display_request create = {BORING_DISPLAY_PROTOCOL_VERSION, BORING_DISPLAY_REQUEST_CREATE, 0U, 20U, 20U, 80U, BORING_DISPLAY_PIXEL_FORMAT_XRGB8888, 0U, 1600ULL};
    struct display_control r = {0};
    uint8_t source[1600], output[1608]; uint32_t token, handle; uint8_t *map;
    memset(source, 0x55, sizeof(source)); memset(output, 0xaa, sizeof(output));
    check(boring_display_core_init(&core, &info), "display init"); display_managed_init(&state); state.manager_endpoint = 99U;
    check(boring_display_surface_add(&core, 10U, &create, 7U, source, &token) == 0U, "surface add");
    r.version = BORING_DISPLAY_CONTROL_VERSION; r.type = DISPLAY_BIND; r.surface = token; r.window = 257U; r.owner_pid = 4ULL;
    check(display_managed_control(&state, &core, 99U, 2ULL, &r) == BORING_DISPLAY_STATUS_ACCESS, "binding requires owner opt-in");
    r.type = DISPLAY_DELEGATE; r.window = 0U; r.owner_pid = 0ULL;
    check(display_managed_control(&state, &core, 11U, 4ULL, &r) == BORING_DISPLAY_STATUS_ACCESS, "foreign surface delegation");
    check(display_managed_control(&state, &core, 10U, 4ULL, &r) == 0U, "explicit delegation");
    r.type = DISPLAY_BIND; r.window = 257U; r.owner_pid = 5ULL;
    check(display_managed_control(&state, &core, 99U, 2ULL, &r) == BORING_DISPLAY_STATUS_ACCESS, "foreign app surface association");
    r.owner_pid = 4ULL;
    check(display_managed_control(&state, &core, 98U, 2ULL, &r) == BORING_DISPLAY_STATUS_ACCESS, "same PID is not manager authority");
    check(display_managed_control(&state, &core, 99U, 2ULL, &r) == 0U, "manager binds verified app");
    check(display_managed_control(&state, &core, 99U, 2ULL, &r) == BORING_DISPLAY_STATUS_ACCESS, "duplicate binding");
    r.type = DISPLAY_PLACE; r.owner_pid = 0ULL; r.x = 2U; r.y = 2U; r.width = 16U; r.height = 16U; r.border = 3U; r.color = BORING_WM_FOCUSED;
    check(display_managed_control(&state, &core, 10U, 4ULL, &r) == BORING_DISPLAY_STATUS_ACCESS, "client cannot place own surface");
    check(display_managed_control(&state, &core, 99U, 2ULL, &r) == 0U, "bounded place");
    check(display_managed_compose(&state, &core, output + 4U, 1600U), "managed compose");
    check(output[0] == 0xaa && output[3] == 0xaa && output[1604] == 0xaa && output[1607] == 0xaa, "render output guards");
    check(output[4U + 2U * 80U + 2U * 4U] == 0xb2U, "focused border pixel");
    check(output[4U + 6U * 80U + 6U * 4U] == 0x55U, "client buffer pixel");
    r.x = UINT32_MAX; check(display_managed_control(&state, &core, 99U, 2ULL, &r) == BORING_DISPLAY_STATUS_INVALID, "x overflow rejected"); r.x = 2U;
    r.width = UINT32_MAX; check(display_managed_control(&state, &core, 99U, 2ULL, &r) == BORING_DISPLAY_STATUS_INVALID, "extent overflow rejected"); r.width = 16U;
    r.border = UINT32_MAX; check(display_managed_control(&state, &core, 99U, 2ULL, &r) == BORING_DISPLAY_STATUS_INVALID, "border overflow rejected"); r.border = 3U;
    r.window = 513U; check(display_managed_control(&state, &core, 99U, 2ULL, &r) == BORING_DISPLAY_STATUS_ACCESS, "stale window binding rejected");
    check(boring_display_surface_destroy(&core, 10U, token, &handle, &map) == 0U, "surface destroy"); display_managed_forget(&state, token);
    check(!state.placements[0].visible && state.placements[0].surface == 0U, "placement cleanup");
    check(display_managed_control(&state, &core, 99U, 2ULL, &r) == BORING_DISPLAY_STATUS_INVALID, "stale surface rejected");
}

int main(void) {
    layouts(); policy(); protocol(); display_authority();
    (void)printf("M35 WM policy/protocol/layout/authority tests passed (%lu checks).\n", checks);
    return 0;
}
