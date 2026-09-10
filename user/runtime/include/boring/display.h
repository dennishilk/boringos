#ifndef BORING_USER_DISPLAY_H
#define BORING_USER_DISPLAY_H

#include <stdint.h>

#include <boring/display_abi.h>

long boring_buffer_info(uint32_t handle);
long boring_framebuffer_claim(struct boring_display_scanout_info *info);
long boring_framebuffer_present(uint32_t buffer_handle);
long boring_framebuffer_present_region(uint32_t buffer_handle,
                                       uint32_t x,
                                       uint32_t y,
                                       uint32_t width,
                                       uint32_t height);
long boring_framebuffer_release(void);

#endif
