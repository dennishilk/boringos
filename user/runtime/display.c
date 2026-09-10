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

long boring_framebuffer_present_region(uint32_t buffer_handle,
                                       uint32_t x,
                                       uint32_t y,
                                       uint32_t width,
                                       uint32_t height) {
    long result;
    register uint64_t width_argument __asm__("r10") = (uint64_t)width;
    register uint64_t height_argument __asm__("r8") = (uint64_t)height;

    __asm__ volatile(
        "syscall"
        : "=a"(result)
        : "a"((uint64_t)BORING_SYS_FRAMEBUFFER_PRESENT_REGION),
          "D"((uint64_t)buffer_handle), "S"((uint64_t)x),
          "d"((uint64_t)y), "r"(width_argument), "r"(height_argument)
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
