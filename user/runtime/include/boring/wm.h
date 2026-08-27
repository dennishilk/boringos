#ifndef BORING_WM_ABI_H
#define BORING_WM_ABI_H
#include <stddef.h>
#include <stdint.h>

#define BORING_WM_SERVICE "boring.wm"
#define BORING_WM_SERVICE_LENGTH 9U
#define BORING_WM_VERSION 1U
#define BORING_WM_CLIENT_MAX 6U
#define BORING_WM_REGISTER 1U
#define BORING_WM_QUERY 2U
#define BORING_WM_UNREGISTER 3U
#define BORING_WM_REPLY 4U
#define BORING_WM_CLOSE 5U
#define BORING_WM_CONFIGURE 6U
#define BORING_WM_KEY 7U
#define BORING_WM_OK 0U
#define BORING_WM_INVALID 1U
#define BORING_WM_ACCESS 2U
#define BORING_WM_NO_SPACE 3U
#define BORING_WM_EXISTS 4U
#define BORING_WM_OUTER 4U
#define BORING_WM_INNER 8U
#define BORING_WM_BORDER 3U
#define BORING_WM_BACKGROUND 0x00282828U
#define BORING_WM_FOCUSED 0x00ebdbb2U
#define BORING_WM_UNFOCUSED 0x003c3836U

struct boring_wm_message {
    uint32_t version;
    uint32_t type;
    uint32_t token;
    uint32_t surface;
    uint32_t status;
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t border;
    uint32_t focused;
    uint32_t reserved;
};
_Static_assert(sizeof(struct boring_wm_message) == 48U, "WM message ABI");
#endif
