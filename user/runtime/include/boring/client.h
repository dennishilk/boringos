#ifndef BORING_USER_CLIENT_H
#define BORING_USER_CLIENT_H

#include <stdbool.h>
#include <stdint.h>
#include <boring/wm.h>

/* Process-owned state, zero-initialized once before open. Not a wire ABI.
 * Calls return false with a static diagnostic in error. On failure the caller
 * exits; the kernel reclaims partially acquired resources. No retry contract.
 * Drawing, event waiting, app policy and unsaved-document decisions stay local.
 */
struct boring_client {
    uint32_t display, manager, surface, window_token, buffer_handle;
    uint32_t width, height, stride;
    uint8_t *pixels;
    const char *error;
};

bool boring_client_open(struct boring_client *client);
/* Call after drawing the first frame into pixels. */
bool boring_client_publish(struct boring_client *client);
bool boring_client_commit(struct boring_client *client);
/* Call only after EVENT_WAIT reports WM input; HUP remains caller policy. */
bool boring_client_receive(struct boring_client *client, struct boring_wm_message *event);
bool boring_client_unregister(struct boring_client *client);
/* Requires unregister first; both successful cleanup calls are idempotent. */
bool boring_client_release(struct boring_client *client);

#endif
