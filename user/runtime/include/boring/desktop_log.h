#ifndef BORING_DESKTOP_LOG_H
#define BORING_DESKTOP_LOG_H
#include <boring/syscall.h>
#include <boring/string.h>
#include <boring/memory.h>
#include <boring/syscall_abi.h>

static inline void desktop_say(const char *text) {
    size_t left = boring_strlen(text);
    while (left != 0U) {
        size_t size = left > BORING_SYSCALL_DEBUG_WRITE_MAX ? BORING_SYSCALL_DEBUG_WRITE_MAX : left;
        (void)boring_debug_write(text, size);
        text += size;
        left -= size;
    }
}
static inline void desktop_fail(const char *reason) __attribute__((noreturn));
static inline void desktop_fail(const char *reason) {
    desktop_say("M35 FAILED: "); desktop_say(reason); desktop_say("\n"); boring_exit(90);
}
static inline size_t desktop_number(char *out, size_t offset, uint64_t value) {
    char digits[20]; size_t used = 0U;
    do { digits[used++] = (char)('0' + value % 10ULL); value /= 10ULL; } while (value != 0ULL);
    while (used != 0U) { out[offset++] = digits[--used]; }
    return offset;
}
#endif
