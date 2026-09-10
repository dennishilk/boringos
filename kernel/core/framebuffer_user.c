#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/display_abi.h>
#include <boring/framebuffer.h>
#include <boring/framebuffer_user.h>
#include <boring/graphics.h>
#include <boring/process.h>
#include <boring/user_memory.h>

#define PRESENT_CHUNK_BYTES 4096U

static uint8_t present_scratch[PRESENT_CHUNK_BYTES];
static uint64_t framebuffer_owner_pid;
static uint64_t framebuffer_present_count;
static uint64_t framebuffer_full_present_count;
static uint64_t framebuffer_region_present_count;
static uint64_t framebuffer_pixels_presented;
static bool framebuffer_claimed;

static bool scanout_info(struct boring_display_scanout_info *info) {
    const struct boring_framebuffer *surface = boring_framebuffer_get();
    uint64_t stride;
    uint64_t bytes;

    if ((info == NULL) || !boring_framebuffer_surface_valid(surface) ||
        (surface->width == 0ULL) || (surface->height == 0ULL) ||
        (surface->width > (uint64_t)BORING_DISPLAY_MAX_WIDTH) ||
        (surface->height > (uint64_t)BORING_DISPLAY_MAX_HEIGHT) ||
        (surface->width > UINT64_MAX / (uint64_t)BORING_DISPLAY_BYTES_PER_PIXEL)) {
        return false;
    }
    stride = surface->width * (uint64_t)BORING_DISPLAY_BYTES_PER_PIXEL;
    if ((stride > (uint64_t)UINT32_MAX) ||
        (surface->height > UINT64_MAX / stride)) {
        return false;
    }
    bytes = stride * surface->height;
    if ((bytes == 0ULL) ||
        (bytes > (uint64_t)BORING_DISPLAY_MAX_SCANOUT_BYTES) ||
        (surface->width > (uint64_t)UINT32_MAX) ||
        (surface->height > (uint64_t)UINT32_MAX)) {
        return false;
    }
    info->version = BORING_DISPLAY_SCANOUT_VERSION;
    info->width = (uint32_t)surface->width;
    info->height = (uint32_t)surface->height;
    info->stride = (uint32_t)stride;
    info->byte_size = bytes;
    return true;
}

enum boring_framebuffer_user_result boring_framebuffer_user_claim(
    uint64_t pid,
    struct boring_display_scanout_info *info) {
    struct boring_display_scanout_info candidate;

    if ((pid == 0ULL) || (info == NULL)) {
        return BORING_FRAMEBUFFER_USER_INVALID;
    }
    if (!scanout_info(&candidate)) {
        return BORING_FRAMEBUFFER_USER_UNAVAILABLE;
    }
    if (framebuffer_claimed) {
        return (framebuffer_owner_pid == pid) ?
            BORING_FRAMEBUFFER_USER_INVALID : BORING_FRAMEBUFFER_USER_BUSY;
    }
    framebuffer_claimed = true;
    framebuffer_owner_pid = pid;
    *info = candidate;
    return BORING_FRAMEBUFFER_USER_OK;
}

static bool present_chunk(const struct boring_framebuffer *surface,
                          uint64_t byte_offset,
                          size_t length) {
    size_t index;

    if ((surface == NULL) || ((length & 3U) != 0U) ||
        ((byte_offset & 3ULL) != 0ULL)) {
        return false;
    }
    for (index = 0U; index < length; index += 4U) {
        const uint64_t pixel_index = (byte_offset + (uint64_t)index) / 4ULL;
        const uint64_t x = pixel_index % surface->width;
        const uint64_t y = pixel_index / surface->width;
        const uint8_t blue = present_scratch[index];
        const uint8_t green = present_scratch[index + 1U];
        const uint8_t red = present_scratch[index + 2U];
        const uint32_t packed = boring_color_pack(surface, red, green, blue);

        if (!boring_graphics_put_pixel(surface, x, y, packed)) {
            return false;
        }
    }
    return true;
}

static void present_account(uint64_t pixels, bool region) {
    if (framebuffer_present_count != UINT64_MAX) {
        ++framebuffer_present_count;
    }
    if (region) {
        if (framebuffer_region_present_count != UINT64_MAX) {
            ++framebuffer_region_present_count;
        }
    } else if (framebuffer_full_present_count != UINT64_MAX) {
        ++framebuffer_full_present_count;
    }
    if (pixels > UINT64_MAX - framebuffer_pixels_presented) {
        framebuffer_pixels_presented = UINT64_MAX;
    } else {
        framebuffer_pixels_presented += pixels;
    }
}

enum boring_framebuffer_user_result boring_framebuffer_user_present(
    struct process *process,
    uint32_t buffer_handle) {
    struct boring_display_scanout_info info;
    const struct boring_framebuffer *surface = boring_framebuffer_get();
    uint64_t buffer_size = 0ULL;
    uint64_t offset = 0ULL;

    if ((process == NULL) || !process_is_alive(process) ||
        !framebuffer_claimed || (framebuffer_owner_pid != process->pid)) {
        return BORING_FRAMEBUFFER_USER_ACCESS;
    }
    if (!scanout_info(&info) || !boring_framebuffer_surface_valid(surface)) {
        return BORING_FRAMEBUFFER_USER_UNAVAILABLE;
    }
    if ((user_buffer_size(process, buffer_handle, &buffer_size) !=
         USER_MEMORY_RESULT_OK) || (buffer_size != info.byte_size)) {
        return BORING_FRAMEBUFFER_USER_INVALID;
    }
    while (offset < info.byte_size) {
        uint64_t remaining = info.byte_size - offset;
        size_t chunk = (remaining > (uint64_t)PRESENT_CHUNK_BYTES) ?
                       (size_t)PRESENT_CHUNK_BYTES : (size_t)remaining;

        if ((chunk == 0U) || ((chunk & 3U) != 0U) ||
            (user_buffer_copy_out(process, buffer_handle, offset,
                                  present_scratch, chunk) !=
             USER_MEMORY_RESULT_OK) ||
            !present_chunk(surface, offset, chunk)) {
            return BORING_FRAMEBUFFER_USER_INTERNAL;
        }
        offset += (uint64_t)chunk;
    }
    present_account((uint64_t)info.width * (uint64_t)info.height, false);
    return BORING_FRAMEBUFFER_USER_OK;
}

static bool region_valid(const struct boring_display_scanout_info *info,
                         uint32_t x,
                         uint32_t y,
                         uint32_t width,
                         uint32_t height) {
    return (info != NULL) && (width != 0U) && (height != 0U) &&
           (x < info->width) && (y < info->height) &&
           (width <= info->width - x) &&
           (height <= info->height - y);
}

static bool present_region_row(struct process *process,
                               uint32_t buffer_handle,
                               const struct boring_display_scanout_info *info,
                               const struct boring_framebuffer *surface,
                               uint32_t x,
                               uint32_t y,
                               uint32_t width) {
    uint32_t completed = 0U;

    while (completed < width) {
        const uint32_t remaining = width - completed;
        const uint32_t scratch_pixels =
            PRESENT_CHUNK_BYTES / BORING_DISPLAY_BYTES_PER_PIXEL;
        const uint32_t chunk_pixels =
            (remaining > scratch_pixels) ? scratch_pixels : remaining;
        const size_t chunk_bytes =
            (size_t)chunk_pixels *
            (size_t)BORING_DISPLAY_BYTES_PER_PIXEL;
        const uint64_t offset =
            (uint64_t)y * (uint64_t)info->stride +
            (uint64_t)(x + completed) *
                (uint64_t)BORING_DISPLAY_BYTES_PER_PIXEL;
        uint32_t pixel;

        if ((chunk_pixels == 0U) ||
            (user_buffer_copy_out(process, buffer_handle, offset,
                                  present_scratch, chunk_bytes) !=
             USER_MEMORY_RESULT_OK)) {
            return false;
        }
        for (pixel = 0U; pixel < chunk_pixels; ++pixel) {
            const size_t source =
                (size_t)pixel *
                (size_t)BORING_DISPLAY_BYTES_PER_PIXEL;
            const uint32_t packed = boring_color_pack(
                surface,
                present_scratch[source + 2U],
                present_scratch[source + 1U],
                present_scratch[source]);

            if (!boring_graphics_put_pixel(surface,
                                           (uint64_t)x + completed + pixel,
                                           y, packed)) {
                return false;
            }
        }
        completed += chunk_pixels;
    }
    return true;
}

enum boring_framebuffer_user_result boring_framebuffer_user_present_region(
    struct process *process,
    uint32_t buffer_handle,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height) {
    struct boring_display_scanout_info info;
    const struct boring_framebuffer *surface = boring_framebuffer_get();
    uint64_t buffer_size = 0ULL;
    uint32_t row;

    if ((process == NULL) || !process_is_alive(process) ||
        !framebuffer_claimed || (framebuffer_owner_pid != process->pid)) {
        return BORING_FRAMEBUFFER_USER_ACCESS;
    }
    if (!scanout_info(&info) || !boring_framebuffer_surface_valid(surface)) {
        return BORING_FRAMEBUFFER_USER_UNAVAILABLE;
    }
    if (!region_valid(&info, x, y, width, height) ||
        (user_buffer_size(process, buffer_handle, &buffer_size) !=
         USER_MEMORY_RESULT_OK) || (buffer_size != info.byte_size)) {
        return BORING_FRAMEBUFFER_USER_INVALID;
    }
    for (row = 0U; row < height; ++row) {
        if (!present_region_row(process, buffer_handle, &info, surface,
                                x, y + row, width)) {
            return BORING_FRAMEBUFFER_USER_INTERNAL;
        }
    }
    present_account((uint64_t)width * (uint64_t)height, true);
    return BORING_FRAMEBUFFER_USER_OK;
}

enum boring_framebuffer_user_result boring_framebuffer_user_release(uint64_t pid) {
    if ((pid == 0ULL) || !framebuffer_claimed ||
        (framebuffer_owner_pid != pid)) {
        return BORING_FRAMEBUFFER_USER_ACCESS;
    }
    framebuffer_claimed = false;
    framebuffer_owner_pid = 0ULL;
    return BORING_FRAMEBUFFER_USER_OK;
}

bool boring_framebuffer_user_process_teardown(uint64_t pid, bool *released_out) {
    bool released = false;

    if (pid == 0ULL) {
        return false;
    }
    if (framebuffer_claimed && (framebuffer_owner_pid == pid)) {
        framebuffer_claimed = false;
        framebuffer_owner_pid = 0ULL;
        released = true;
    }
    if (released_out != NULL) {
        *released_out = released;
    }
    return true;
}

bool boring_framebuffer_user_get_stats(struct boring_framebuffer_user_stats *stats) {
    if (stats == NULL) {
        return false;
    }
    stats->owner_pid = framebuffer_owner_pid;
    stats->presents = framebuffer_present_count;
    stats->full_presents = framebuffer_full_present_count;
    stats->region_presents = framebuffer_region_present_count;
    stats->pixels_presented = framebuffer_pixels_presented;
    stats->claimed = framebuffer_claimed;
    return true;
}
