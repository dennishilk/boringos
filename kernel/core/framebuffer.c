#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/boot_protocol.h>
#include <boring/framebuffer.h>

#define BORING_FRAMEBUFFER_MAX_BOOT_SURFACES 16ULL

__attribute__((used, section(".limine_requests")))
static volatile struct boring_limine_framebuffer_request limine_framebuffer_request = {
    .id = BORING_LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0ULL,
    .response = 0
};

static struct boring_framebuffer boot_surface;
static bool boot_surface_ready;

static bool boring_u64_mul(uint64_t first, uint64_t second, uint64_t *result) {
    if (result == NULL) {
        return false;
    }
    if ((first != 0ULL) && (second > (UINT64_MAX / first))) {
        return false;
    }
    *result = first * second;
    return true;
}

static bool boring_framebuffer_masks_valid(
    uint16_t bpp,
    uint8_t red_size,
    uint8_t red_shift,
    uint8_t green_size,
    uint8_t green_shift,
    uint8_t blue_size,
    uint8_t blue_shift) {
    uint64_t red_mask;
    uint64_t green_mask;
    uint64_t blue_mask;

    if ((red_size == 0U) || (red_size > 8U) ||
        (green_size == 0U) || (green_size > 8U) ||
        (blue_size == 0U) || (blue_size > 8U)) {
        return false;
    }
    if (((uint16_t)red_shift + (uint16_t)red_size > bpp) ||
        ((uint16_t)green_shift + (uint16_t)green_size > bpp) ||
        ((uint16_t)blue_shift + (uint16_t)blue_size > bpp)) {
        return false;
    }

    red_mask = (((1ULL << red_size) - 1ULL) << red_shift);
    green_mask = (((1ULL << green_size) - 1ULL) << green_shift);
    blue_mask = (((1ULL << blue_size) - 1ULL) << blue_shift);

    return ((red_mask & green_mask) == 0ULL) &&
           ((red_mask & blue_mask) == 0ULL) &&
           ((green_mask & blue_mask) == 0ULL);
}

bool boring_framebuffer_surface_valid(const struct boring_framebuffer *surface) {
    uint64_t row_bytes;
    uint64_t byte_size;
    uintptr_t address;

    if ((surface == NULL) || (surface->address == NULL) ||
        (surface->width == 0ULL) || (surface->height == 0ULL) ||
        ((surface->bpp != 24U) && (surface->bpp != 32U)) ||
        (surface->memory_model != BORING_FRAMEBUFFER_MEMORY_MODEL_RGB)) {
        return false;
    }

    if (surface->bytes_per_pixel != (uint8_t)(surface->bpp / 8U)) {
        return false;
    }
    if (!boring_u64_mul(surface->width,
                        (uint64_t)surface->bytes_per_pixel,
                        &row_bytes) ||
        (surface->pitch < row_bytes) ||
        !boring_u64_mul(surface->height, surface->pitch, &byte_size) ||
        (surface->byte_size != byte_size)) {
        return false;
    }
    if (!boring_framebuffer_masks_valid(
            surface->bpp,
            surface->red_mask_size,
            surface->red_mask_shift,
            surface->green_mask_size,
            surface->green_mask_shift,
            surface->blue_mask_size,
            surface->blue_mask_shift)) {
        return false;
    }

    address = (uintptr_t)surface->address;
    if ((byte_size == 0ULL) ||
        (byte_size > (uint64_t)UINTPTR_MAX) ||
        (address > (UINTPTR_MAX - (uintptr_t)byte_size))) {
        return false;
    }
    return true;
}

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
    uint8_t blue_mask_shift) {
    uint64_t byte_size = 0ULL;

    if (surface == NULL) {
        return false;
    }

    surface->address = address;
    surface->width = width;
    surface->height = height;
    surface->pitch = pitch;
    surface->byte_size = 0ULL;
    surface->bpp = bpp;
    surface->bytes_per_pixel =
        ((bpp == 24U) || (bpp == 32U)) ? (uint8_t)(bpp / 8U) : 0U;
    surface->memory_model = memory_model;
    surface->red_mask_size = red_mask_size;
    surface->red_mask_shift = red_mask_shift;
    surface->green_mask_size = green_mask_size;
    surface->green_mask_shift = green_mask_shift;
    surface->blue_mask_size = blue_mask_size;
    surface->blue_mask_shift = blue_mask_shift;

    if ((pitch != 0ULL) && !boring_u64_mul(height, pitch, &byte_size)) {
        return false;
    }
    surface->byte_size = byte_size;
    return boring_framebuffer_surface_valid(surface);
}

enum boring_framebuffer_status boring_framebuffer_boot_init(void) {
    const struct boring_limine_framebuffer_response *response =
        limine_framebuffer_request.response;
    uint64_t index;
    bool saw_rgb = false;

    boot_surface_ready = false;
    if ((response == NULL) || (response->framebuffer_count == 0ULL) ||
        (response->framebuffers == NULL)) {
        return BORING_FRAMEBUFFER_STATUS_UNAVAILABLE;
    }
    if (response->framebuffer_count > BORING_FRAMEBUFFER_MAX_BOOT_SURFACES) {
        return BORING_FRAMEBUFFER_STATUS_INVALID;
    }

    for (index = 0ULL; index < response->framebuffer_count; ++index) {
        const struct boring_limine_framebuffer *const framebuffer =
            response->framebuffers[index];

        if (framebuffer == NULL) {
            continue;
        }
        if (framebuffer->memory_model != BORING_LIMINE_FRAMEBUFFER_RGB) {
            continue;
        }
        saw_rgb = true;
        if (boring_framebuffer_surface_init(
                &boot_surface,
                (volatile uint8_t *)framebuffer->address,
                framebuffer->width,
                framebuffer->height,
                framebuffer->pitch,
                framebuffer->bpp,
                framebuffer->memory_model,
                framebuffer->red_mask_size,
                framebuffer->red_mask_shift,
                framebuffer->green_mask_size,
                framebuffer->green_mask_shift,
                framebuffer->blue_mask_size,
                framebuffer->blue_mask_shift)) {
            boot_surface_ready = true;
            return BORING_FRAMEBUFFER_STATUS_READY;
        }
    }

    return saw_rgb ? BORING_FRAMEBUFFER_STATUS_INVALID
                   : BORING_FRAMEBUFFER_STATUS_UNSUPPORTED;
}

const struct boring_framebuffer *boring_framebuffer_get(void) {
    return boot_surface_ready ? &boot_surface : NULL;
}
