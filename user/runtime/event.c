#include <boring/event.h>
#include <boring/syscall_abi.h>

long boring_event_wait(struct boring_event_watch *watches, size_t count,
                       uint32_t flags) {
    long result;
    __asm__ volatile("syscall" : "=a"(result)
        : "a"((uint64_t)BORING_SYS_EVENT_WAIT), "D"(watches), "S"(count),
          "d"((uint64_t)flags) : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_endpoint_peer(uint32_t endpoint) {
    struct boring_event_watch watch = { BORING_EVENT_IPC, endpoint, 0U, 0U, 0ULL };
    long result = boring_event_wait(&watch, 1U, BORING_EVENT_QUERY);
    if (result < 0L) {
        return result;
    }
    if ((watch.peer_pid == 0ULL) || (watch.peer_pid > (uint64_t)INT64_MAX)) {
        return -(long)BORING_SYSCALL_EPIPE;
    }
    return (long)watch.peer_pid;
}
