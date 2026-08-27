#include "core.h"
#include <boring/display_abi.h>

#define WM_GENERATION_MAX 0x00ffffffU
static uint32_t minimum(uint32_t a, uint32_t b) { return a < b ? a : b; }

bool wm_init(struct wm_core *wm, uint32_t width, uint32_t height) {
    uint32_t index;
    if ((wm == NULL) || (width == 0U) || (height == 0U) ||
        (width > BORING_DISPLAY_MAX_WIDTH) || (height > BORING_DISPLAY_MAX_HEIGHT)) {
        return false;
    }
    *wm = (struct wm_core){0};
    wm->width = width;
    wm->height = height;
    for (index = 0U; index < BORING_WM_CLIENT_MAX; ++index) {
        wm->clients[index].generation = 1U;
    }
    return true;
}

const struct wm_client *wm_lookup(const struct wm_core *wm, uint32_t token) {
    uint32_t low = token & 255U;
    const struct wm_client *client;
    if ((wm == NULL) || (low == 0U) || (low > BORING_WM_CLIENT_MAX)) {
        return NULL;
    }
    client = &wm->clients[low - 1U];
    return (client->active && (client->token == token)) ? client : NULL;
}

uint32_t wm_authority(const struct wm_core *wm, uint32_t endpoint, uint32_t token) {
    const struct wm_client *client = wm_lookup(wm, token);
    if (client == NULL) { return BORING_WM_INVALID; }
    return (client->endpoint == endpoint) ? BORING_WM_OK : BORING_WM_ACCESS;
}

static void rectangle(struct wm_client *client, uint32_t x, uint32_t y,
                       uint32_t width, uint32_t height) {
    uint32_t small = minimum(width, height);
    client->rect = (struct wm_rect){x, y, width, height,
        small == 0U ? 0U : minimum(BORING_WM_BORDER, (small - 1U) / 2U)};
}

/* Split a bounded column; early tiles receive the remainder pixels. */
static void column(struct wm_core *wm, uint32_t first, uint32_t count,
                    uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    uint32_t gap = 0U, span, index;
    if (count == 0U) { return; }
    if ((count > 1U) && (height >= count)) {
        gap = minimum(BORING_WM_INNER, (height - count) / (count - 1U));
    }
    span = height - gap * (count - 1U);
    for (index = 0U; index < count; ++index) {
        uint32_t h = span / count + (index < span % count ? 1U : 0U);
        rectangle(&wm->clients[wm->order[first + index]], x, y, width, h);
        y += h;
        if (index + 1U < count) { y += gap; }
    }
}

void wm_layout(struct wm_core *wm) {
    uint32_t outer, width, height, gap, master;
    if ((wm == NULL) || (wm->count == 0U) ||
        (wm->count > BORING_WM_CLIENT_MAX)) { return; }
    outer = minimum(BORING_WM_OUTER,
                    (minimum(wm->width, wm->height) - 1U) / 2U);
    width = wm->width - 2U * outer;
    height = wm->height - 2U * outer;
    if ((wm->count == 1U) || (width < 2U) || (height < wm->count - 1U)) {
        column(wm, 0U, wm->count, outer, outer, width, height);
        return;
    }
    gap = minimum(BORING_WM_INNER, width - 2U);
    master = ((width - gap) * 3U) / 5U;
    if (master == 0U) { master = 1U; }
    rectangle(&wm->clients[wm->order[0]], outer, outer, master, height);
    column(wm, 1U, wm->count - 1U, outer + master + gap, outer,
           width - master - gap, height);
}

uint32_t wm_add(struct wm_core *wm, uint32_t endpoint, uint64_t pid,
                uint32_t surface, uint32_t *token) {
    uint32_t index;
    if ((wm == NULL) || (endpoint == 0U) || (pid == 0ULL) ||
        (surface == 0U) || (token == NULL)) { return BORING_WM_INVALID; }
    for (index = 0U; index < BORING_WM_CLIENT_MAX; ++index) {
        if (wm->clients[index].active &&
            ((wm->clients[index].endpoint == endpoint) ||
             (wm->clients[index].surface == surface))) { return BORING_WM_EXISTS; }
    }
    if (wm->count >= BORING_WM_CLIENT_MAX) { return BORING_WM_NO_SPACE; }
    for (index = 0U; index < BORING_WM_CLIENT_MAX; ++index) {
        struct wm_client *client = &wm->clients[index];
        if (client->active || (client->generation == 0U)) { continue; }
        client->endpoint = endpoint;
        client->peer_pid = pid;
        client->surface = surface;
        client->token = (client->generation << 8U) | (index + 1U);
        client->active = true;
        client->closing = false;
        wm->order[wm->count++] = index;
        wm->focus = client->token;
        *token = client->token;
        wm_layout(wm);
        return BORING_WM_OK;
    }
    return BORING_WM_NO_SPACE;
}

uint32_t wm_remove(struct wm_core *wm, uint32_t endpoint, uint32_t token) {
    uint32_t status = wm_authority(wm, endpoint, token);
    uint32_t position, index;
    struct wm_client *client;
    if (status != BORING_WM_OK) { return status; }
    client = &wm->clients[(token & 255U) - 1U];
    for (position = 0U; position < wm->count; ++position) {
        if (wm->clients[wm->order[position]].token == token) { break; }
    }
    for (index = position + 1U; index < wm->count; ++index) {
        wm->order[index - 1U] = wm->order[index];
    }
    --wm->count;
    wm->order[wm->count] = 0U;
    if (wm->focus == token) {
        wm->focus = wm->count == 0U ? 0U :
            wm->clients[wm->order[position % wm->count]].token;
    }
    index = client->generation;
    *client = (struct wm_client){0};
    client->generation = index == WM_GENERATION_MAX ? 0U : index + 1U;
    wm_layout(wm);
    return BORING_WM_OK;
}

static uint32_t focused_position(const struct wm_core *wm) {
    uint32_t index;
    for (index = 0U; index < wm->count; ++index) {
        if (wm->clients[wm->order[index]].token == wm->focus) { return index; }
    }
    return wm->count;
}

bool wm_focus_step(struct wm_core *wm, int direction) {
    uint32_t position;
    if ((wm == NULL) || (wm->count == 0U) ||
        ((direction != -1) && (direction != 1))) { return false; }
    position = focused_position(wm);
    if (position >= wm->count) { return false; }
    position = direction > 0 ? (position + 1U) % wm->count :
        (position + wm->count - 1U) % wm->count;
    wm->focus = wm->clients[wm->order[position]].token;
    return true;
}

bool wm_reorder(struct wm_core *wm, int direction) {
    uint32_t position, other, slot;
    if ((wm == NULL) || (wm->count < 2U) ||
        ((direction != -1) && (direction != 1))) { return false; }
    position = focused_position(wm);
    if ((position >= wm->count) || ((direction < 0) && (position == 0U)) ||
        ((direction > 0) && (position + 1U == wm->count))) { return false; }
    other = direction > 0 ? position + 1U : position - 1U;
    slot = wm->order[position];
    wm->order[position] = wm->order[other];
    wm->order[other] = slot;
    wm_layout(wm);
    return true;
}

bool wm_pointer(struct wm_core *wm, uint32_t x, uint32_t y) {
    uint32_t index;
    if (wm == NULL) { return false; }
    for (index = 0U; index < wm->count; ++index) {
        const struct wm_client *client = &wm->clients[wm->order[index]];
        const struct wm_rect *r = &client->rect;
        if ((x >= r->x) && (y >= r->y) && (x - r->x < r->width) &&
            (y - r->y < r->height) && (wm->focus != client->token)) {
            wm->focus = client->token;
            return true;
        }
    }
    return false;
}

enum wm_action wm_key(struct wm_core *wm, const struct boring_input_event *event) {
    int direction = 0;
    if ((wm == NULL) || (event == NULL) ||
        (event->type != BORING_INPUT_EVENT_KEY) ||
        (event->value1 != BORING_KEY_DOWN_VALUE) || (event->flags != 0U) ||
        ((event->modifiers != BORING_MOD_SUPER) &&
         (event->modifiers != (BORING_MOD_SUPER | BORING_MOD_SHIFT)))) {
        return WM_NONE;
    }
    if ((event->code == BORING_KEY_H) || (event->code == BORING_KEY_K) ||
        (event->code == BORING_KEY_LEFT) || (event->code == BORING_KEY_UP)) {
        direction = -1;
    } else if ((event->code == BORING_KEY_J) || (event->code == BORING_KEY_L) ||
               (event->code == BORING_KEY_RIGHT) || (event->code == BORING_KEY_DOWN)) {
        direction = 1;
    }
    if (direction != 0) {
        if ((event->modifiers & BORING_MOD_SHIFT) != 0U) {
            return wm_reorder(wm, direction) ? WM_REORDER : WM_NONE;
        }
        return wm_focus_step(wm, direction) ? WM_FOCUS : WM_NONE;
    }
    if (event->modifiers != BORING_MOD_SUPER) { return WM_NONE; }
    if (event->code == BORING_KEY_ENTER) { return WM_NO_LAUNCHER; }
    if ((event->code == BORING_KEY_Q) && (wm_lookup(wm, wm->focus) != NULL)) {
        struct wm_client *client = &wm->clients[(wm->focus & 255U) - 1U];
        if (!client->closing) {
            client->closing = true;
            return WM_CLOSE;
        }
    }
    return WM_NONE;
}

uint32_t wm_validate_request(const struct boring_wm_message *m, size_t length) {
    if ((m == NULL) || (length != sizeof(*m)) || (m->version != BORING_WM_VERSION) ||
        (m->status != 0U) || (m->x != 0U) || (m->y != 0U) || (m->width != 0U) ||
        (m->height != 0U) || (m->border != 0U) || (m->focused != 0U) ||
        (m->reserved != 0U)) { return BORING_WM_INVALID; }
    if (m->type == BORING_WM_REGISTER) {
        return ((m->token == 0U) && (m->surface != 0U)) ?
            BORING_WM_OK : BORING_WM_INVALID;
    }
    if ((m->type == BORING_WM_QUERY) || (m->type == BORING_WM_UNREGISTER)) {
        return ((m->token != 0U) && (m->surface == 0U)) ?
            BORING_WM_OK : BORING_WM_INVALID;
    }
    return BORING_WM_INVALID;
}
