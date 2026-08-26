#ifndef BORING_FRAMEBUFFER_H
#define BORING_FRAMEBUFFER_H

#include <stdbool.h>
#include <stdint.h>

#define BORING_FRAMEBUFFER_MEMORY_MODEL_RGB 1U

enum boring_framebuffer_status {
    BORING_FRAMEBUFFER_STATUS_READY = 0,
    BORING_FRAMEBUFFER_STATUS_UNAVAILABLE,
    BORING_FRAMEBUFFER_STATUS_UNSUPPORTED,
    BORING_FRAMEBUFFER_STATUS_INVALID
};

struct boring_framebuffer {
    volatile uint8_t *address;
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint64_t byte_size;
    uint16_t bpp;
    uint8_t bytes_per_pixel;
    uint8_t memory_model;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
};

bool boring_framebuffer_surface_init(
    struct boring_framebuffer *surface,
    volatile uint8_t *address,
    uint64_t width,
    uint64_t height,
    uint64_t pitch,
    uint16_t bpp,
    uint8_t memory_model,
    uint8_t red_mask_size,
    uint8_t red_mask_shift,
    uint8_t green_mask_size,
    uint8_t green_mask_shift,
    uint8_t blue_mask_size,
    uint8_t blue_mask_shift);

bool boring_framebuffer_surface_valid(const struct boring_framebuffer *surface);
enum boring_framebuffer_status boring_framebuffer_boot_init(void);
const struct boring_framebuffer *boring_framebuffer_get(void);

#endif
