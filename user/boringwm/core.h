#ifndef BORING_WM_CORE_H
#define BORING_WM_CORE_H
#include <stdbool.h>
#include <boring/wm.h>
#include <boring/input_abi.h>

struct wm_rect { uint32_t x, y, width, height, border; };
struct wm_client {
    uint32_t endpoint, surface, token, generation;
    uint64_t peer_pid;
    struct wm_rect rect;
    bool active, closing;
};
struct wm_core {
    uint32_t width, height, count, focus;
    uint32_t order[BORING_WM_CLIENT_MAX];
    struct wm_client clients[BORING_WM_CLIENT_MAX];
};
enum wm_action { WM_NONE, WM_FOCUS, WM_REORDER, WM_CLOSE, WM_NO_LAUNCHER };
bool wm_init(struct wm_core *wm, uint32_t width, uint32_t height);
void wm_layout(struct wm_core *wm);
const struct wm_client *wm_lookup(const struct wm_core *wm, uint32_t token);
uint32_t wm_add(struct wm_core *wm, uint32_t endpoint, uint64_t pid,
                uint32_t surface, uint32_t *token);
uint32_t wm_remove(struct wm_core *wm, uint32_t endpoint, uint32_t token);
uint32_t wm_authority(const struct wm_core *wm, uint32_t endpoint, uint32_t token);
bool wm_focus_step(struct wm_core *wm, int direction);
bool wm_reorder(struct wm_core *wm, int direction);
bool wm_pointer(struct wm_core *wm, uint32_t x, uint32_t y);
enum wm_action wm_key(struct wm_core *wm, const struct boring_input_event *event);
uint32_t wm_validate_request(const struct boring_wm_message *message, size_t length);
#endif
