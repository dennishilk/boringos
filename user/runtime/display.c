#include <stdint.h>

#include <boring/display.h>
#include <boring/syscall_abi.h>

long boring_buffer_info(uint32_t handle) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_BUFFER_INFO), "D"((uint64_t)handle)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_framebuffer_claim(struct boring_display_scanout_info *info) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_FRAMEBUFFER_CLAIM), "D"(info)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_framebuffer_present(uint32_t buffer_handle) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_FRAMEBUFFER_PRESENT),
          "D"((uint64_t)buffer_handle)
        : "rcx", "r11", "cc", "memory");
    return result;
}

long boring_framebuffer_release(void) {
    long result;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_FRAMEBUFFER_RELEASE)
        : "rcx", "r11", "cc", "memory");
    return result;
}
