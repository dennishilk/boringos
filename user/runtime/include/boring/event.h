#ifndef BORING_USER_EVENT_H
#define BORING_USER_EVENT_H
#include <stddef.h>
#include <stdint.h>
#include <boring/event_abi.h>
long boring_event_wait(struct boring_event_watch *watches, size_t count,
                       uint32_t flags);
long boring_endpoint_peer(uint32_t endpoint);
#endif
