#ifndef BORING_FRAMEBUFFER_H
#define BORING_FRAMEBUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
#include <boring/vmm.h>
#endif

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

#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
struct boring_m61_framebuffer_mapping_diagnostics {
    struct vmm_framebuffer_resolution resolution;
    struct vmm_mapping_info alias_start_mapping;
    struct vmm_mapping_info alias_end_mapping;
    uintptr_t original_virtual_start;
    uintptr_t original_virtual_end;
    uintptr_t alias_virtual_start;
    uintptr_t alias_virtual_end;
    uint64_t byte_size;
    size_t mapping_pages;
    bool attempted;
    bool memmap_range_match;
    bool alias_created;
    bool metadata_preserved;
};

uint64_t boring_m61_framebuffer_count(void);
bool boring_m61_framebuffer_get(uint64_t index,
                                struct boring_framebuffer *surface);
bool boring_m61_framebuffer_normalize(
    struct boring_framebuffer *surface,
    struct boring_m61_framebuffer_mapping_diagnostics *diagnostics);
bool boring_m61_framebuffer_prepare_runtime(void);
bool boring_m61_framebuffer_get_mapping_diagnostics(
    struct boring_m61_framebuffer_mapping_diagnostics *diagnostics);
#endif

#endif
