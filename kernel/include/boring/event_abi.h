#ifndef BORING_EVENT_ABI_H
#define BORING_EVENT_ABI_H

#include <stdint.h>

#define BORING_EVENT_MAX 32U
#define BORING_EVENT_IPC 1U
#define BORING_EVENT_INPUT 2U
#define BORING_EVENT_FD 3U
#define BORING_EVENT_READ 1U
#define BORING_EVENT_HUP 2U
#define BORING_EVENT_QUERY 1U

struct boring_event_watch {
    uint32_t kind;
    uint32_t handle;
    uint32_t events;
    uint32_t reserved;
    uint64_t peer_pid;
};

_Static_assert(sizeof(struct boring_event_watch) == 24U, "event watch ABI");
#endif
