#include <stddef.h>
#include <stdint.h>

#include <boring/syscall.h>
#include <boring/syscall_abi.h>

uint64_t boring_getpid(void) {
    uint64_t result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_GETPID)
        : "rcx", "r11", "cc");

    return result;
}

long boring_debug_write(const void *buffer, size_t length) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_DEBUG_WRITE),
          "D"(buffer),
          "S"(length)
        : "rcx", "r11", "cc", "memory");

    return result;
}
