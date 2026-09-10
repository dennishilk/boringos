#ifndef BORING_FRAMEBUFFER_USER_H
#define BORING_FRAMEBUFFER_USER_H

#include <stdbool.h>
#include <stdint.h>

#include <boring/display_abi.h>

struct process;

enum boring_framebuffer_user_result {
    BORING_FRAMEBUFFER_USER_OK = 0,
    BORING_FRAMEBUFFER_USER_INVALID = 1,
    BORING_FRAMEBUFFER_USER_BUSY = 2,
    BORING_FRAMEBUFFER_USER_ACCESS = 3,
    BORING_FRAMEBUFFER_USER_UNAVAILABLE = 4,
    BORING_FRAMEBUFFER_USER_INTERNAL = 5
};

struct boring_framebuffer_user_stats {
    uint64_t owner_pid;
    uint64_t presents;
    uint64_t full_presents;
    uint64_t region_presents;
    uint64_t pixels_presented;
    bool claimed;
};

enum boring_framebuffer_user_result boring_framebuffer_user_claim(
    uint64_t pid,
    struct boring_display_scanout_info *info);
enum boring_framebuffer_user_result boring_framebuffer_user_present(
    struct process *process,
    uint32_t buffer_handle);
enum boring_framebuffer_user_result boring_framebuffer_user_present_region(
    struct process *process,
    uint32_t buffer_handle,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height);
enum boring_framebuffer_user_result boring_framebuffer_user_release(uint64_t pid);
bool boring_framebuffer_user_process_teardown(uint64_t pid, bool *released_out);
bool boring_framebuffer_user_get_stats(struct boring_framebuffer_user_stats *stats);

#endif
