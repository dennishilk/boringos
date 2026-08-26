#ifndef BORING_DISPLAY_ABI_H
#define BORING_DISPLAY_ABI_H

#include <stddef.h>
#include <stdint.h>

#define BORING_DISPLAY_PROTOCOL_VERSION 1U
#define BORING_DISPLAY_SERVICE_NAME "boring.display"
#define BORING_DISPLAY_SERVICE_NAME_LENGTH 14U
#define BORING_DISPLAY_PIXEL_FORMAT_XRGB8888 1U
#define BORING_DISPLAY_BYTES_PER_PIXEL 4U
#define BORING_DISPLAY_CLIENT_MAX 8U
#define BORING_DISPLAY_SURFACE_MAX 16U
#define BORING_DISPLAY_SURFACE_INVALID 0U
#define BORING_DISPLAY_SCANOUT_VERSION 1U
#define BORING_DISPLAY_MAX_WIDTH 1920U
#define BORING_DISPLAY_MAX_HEIGHT 1080U
#define BORING_DISPLAY_MAX_SCANOUT_BYTES (16ULL * 1024ULL * 1024ULL)

#define BORING_DISPLAY_REQUEST_CREATE 1U
#define BORING_DISPLAY_REQUEST_COMMIT 2U
#define BORING_DISPLAY_REQUEST_DESTROY 3U

#define BORING_DISPLAY_STATUS_OK 0U
#define BORING_DISPLAY_STATUS_INVALID 1U
#define BORING_DISPLAY_STATUS_ACCESS 2U
#define BORING_DISPLAY_STATUS_NO_SPACE 3U
#define BORING_DISPLAY_STATUS_INTERNAL 4U

struct boring_display_scanout_info {
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint64_t byte_size;
};

struct boring_display_request {
    uint32_t version;
    uint32_t type;
    uint32_t surface_token;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t pixel_format;
    uint32_t reserved;
    uint64_t byte_size;
};

struct boring_display_reply {
    uint32_t version;
    uint32_t status;
    uint32_t surface_token;
    uint32_t reserved;
};

_Static_assert(sizeof(struct boring_display_scanout_info) == 24U,
               "M34 scanout ABI must remain 24 bytes");
_Static_assert(sizeof(struct boring_display_request) == 40U,
               "M34 request ABI must remain 40 bytes");
_Static_assert(sizeof(struct boring_display_reply) == 16U,
               "M34 reply ABI must remain 16 bytes");

#endif
