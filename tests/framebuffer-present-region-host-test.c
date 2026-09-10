#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <boring/framebuffer.h>
#include <boring/framebuffer_user.h>
#include <boring/graphics.h>
#include <boring/process.h>
#include <boring/user_memory.h>

#define MAX_WIDTH 1920U
#define MAX_HEIGHT 6U
#define MAX_PITCH (MAX_WIDTH * 4U + 16U)
#define HANDLE 7U

static uint8_t physical[MAX_PITCH * MAX_HEIGHT];
static uint8_t source[MAX_WIDTH * MAX_HEIGHT * 4U];
static uint64_t source_size;
static struct boring_framebuffer surface;
static unsigned int failures;

static void check(bool condition, const char *name) {
    if (!condition) {
        (void)fprintf(stderr,
                      "framebuffer-present-region-host-test: FAIL: %s\n",
                      name);
        ++failures;
    }
}

static void bytes_fill(uint8_t *bytes, size_t length, uint8_t value) {
    size_t index;

    for (index = 0U; index < length; ++index) {
        bytes[index] = value;
    }
}

static void configure(uint32_t width, uint32_t height, uint32_t pitch) {
    uint32_t y;

    bytes_fill(physical, sizeof(physical), 0x7cU);
    surface.address = physical;
    surface.width = width;
    surface.height = height;
    surface.pitch = pitch;
    surface.byte_size = (uint64_t)pitch * height;
    surface.bpp = 32U;
    surface.bytes_per_pixel = 4U;
    surface.memory_model = BORING_FRAMEBUFFER_MEMORY_MODEL_RGB;
    surface.red_mask_size = 8U;
    surface.red_mask_shift = 16U;
    surface.green_mask_size = 8U;
    surface.green_mask_shift = 8U;
    surface.blue_mask_size = 8U;
    surface.blue_mask_shift = 0U;
    source_size = (uint64_t)width * height * 4ULL;
    for (y = 0U; y < height; ++y) {
        uint32_t x;
        for (x = 0U; x < width; ++x) {
            const size_t offset = (size_t)y * (size_t)width * 4U +
                                  (size_t)x * 4U;
            source[offset] = (uint8_t)(x & 0xffU);
            source[offset + 1U] = (uint8_t)(y + 17U);
            source[offset + 2U] = (uint8_t)((x + y + 33U) & 0xffU);
            source[offset + 3U] = 0U;
        }
    }
}

bool process_is_alive(const struct process *process) {
    return (process != NULL) && (process->state == PROCESS_ALIVE);
}

const struct boring_framebuffer *boring_framebuffer_get(void) {
    return &surface;
}

bool boring_framebuffer_surface_valid(const struct boring_framebuffer *candidate) {
    return (candidate == &surface) && (candidate->address == physical) &&
           (candidate->width != 0ULL) && (candidate->height != 0ULL) &&
           (candidate->pitch >= candidate->width * 4ULL) &&
           (candidate->byte_size == candidate->pitch * candidate->height) &&
           (candidate->bpp == 32U) &&
           (candidate->bytes_per_pixel == 4U);
}

uint32_t boring_color_pack(const struct boring_framebuffer *candidate,
                           uint8_t red,
                           uint8_t green,
                           uint8_t blue) {
    (void)candidate;
    return (uint32_t)blue | ((uint32_t)green << 8U) |
           ((uint32_t)red << 16U);
}

bool boring_graphics_put_pixel(const struct boring_framebuffer *candidate,
                               uint64_t x,
                               uint64_t y,
                               uint32_t color) {
    uint64_t offset;

    if (!boring_framebuffer_surface_valid(candidate) ||
        (x >= candidate->width) || (y >= candidate->height)) {
        return false;
    }
    offset = y * candidate->pitch + x * 4ULL;
    physical[offset] = (uint8_t)color;
    physical[offset + 1ULL] = (uint8_t)(color >> 8U);
    physical[offset + 2ULL] = (uint8_t)(color >> 16U);
    physical[offset + 3ULL] = 0U;
    return true;
}

enum user_memory_result user_buffer_size(struct process *process,
                                         uint32_t handle,
                                         uint64_t *size_out) {
    if ((process == NULL) || (handle != HANDLE) || (size_out == NULL)) {
        return USER_MEMORY_RESULT_INVALID;
    }
    *size_out = source_size;
    return USER_MEMORY_RESULT_OK;
}

enum user_memory_result user_buffer_copy_out(struct process *process,
                                             uint32_t handle,
                                             uint64_t offset,
                                             void *destination,
                                             size_t length) {
    uint8_t *output = (uint8_t *)destination;
    size_t index;

    if ((process == NULL) || (handle != HANDLE) || (destination == NULL) ||
        (offset > source_size) ||
        ((uint64_t)length > source_size - offset)) {
        return USER_MEMORY_RESULT_INVALID;
    }
    for (index = 0U; index < length; ++index) {
        output[index] = source[(size_t)offset + index];
    }
    return USER_MEMORY_RESULT_OK;
}

static bool physical_pixel_matches_source(uint32_t x, uint32_t y) {
    const size_t actual = (size_t)y * (size_t)surface.pitch +
                          (size_t)x * 4U;
    const size_t expected = (size_t)y * (size_t)surface.width * 4U +
                            (size_t)x * 4U;
    size_t byte;

    for (byte = 0U; byte < 4U; ++byte) {
        if (physical[actual + byte] != source[expected + byte]) {
            return false;
        }
    }
    return true;
}

static void small_region_test(struct process *owner) {
    struct boring_display_scanout_info info;
    struct boring_framebuffer_user_stats stats;
    uint32_t y;

    configure(8U, 6U, 40U);
    check(boring_framebuffer_user_claim(owner->pid, &info) ==
          BORING_FRAMEBUFFER_USER_OK, "claim");
    check((info.width == 8U) && (info.height == 6U) &&
          (info.stride == 32U) && (info.byte_size == 192ULL),
          "logical scanout geometry ignores physical pitch padding");
    check(boring_framebuffer_user_present(owner, HANDLE) ==
          BORING_FRAMEBUFFER_USER_OK, "full present baseline");
    for (y = 0U; y < 6U; ++y) {
        uint32_t x;
        for (x = 0U; x < 8U; ++x) {
            check(physical_pixel_matches_source(x, y),
                  "full present pixel");
        }
        for (x = 32U; x < 40U; ++x) {
            check(physical[(size_t)y * 40U + x] == 0x7cU,
                  "physical pitch padding preserved");
        }
    }

    bytes_fill(physical, sizeof(physical), 0x7cU);
    check(boring_framebuffer_user_present_region(
              owner, HANDLE, 2U, 1U, 3U, 2U) ==
          BORING_FRAMEBUFFER_USER_OK, "bounded region present");
    for (y = 0U; y < 6U; ++y) {
        uint32_t x;
        for (x = 0U; x < 8U; ++x) {
            const bool inside = (x >= 2U) && (x < 5U) &&
                                (y >= 1U) && (y < 3U);
            check(inside ? physical_pixel_matches_source(x, y) :
                           (physical[(size_t)y * 40U +
                                     (size_t)x * 4U] == 0x7cU),
                  "region changes only requested pixels");
        }
    }
    check(boring_framebuffer_user_get_stats(&stats) &&
          (stats.presents == 2ULL) && (stats.full_presents == 1ULL) &&
          (stats.region_presents == 1ULL) &&
          (stats.pixels_presented == 54ULL),
          "full/region pixel accounting");
    check(boring_framebuffer_user_present_region(
              owner, HANDLE, 0U, 0U, 0U, 1U) ==
          BORING_FRAMEBUFFER_USER_INVALID, "zero width rejected");
    check(boring_framebuffer_user_present_region(
              owner, HANDLE, 7U, 5U, 2U, 1U) ==
          BORING_FRAMEBUFFER_USER_INVALID, "right overflow rejected");
    check(boring_framebuffer_user_present_region(
              owner, HANDLE, 7U, 5U, 1U, 2U) ==
          BORING_FRAMEBUFFER_USER_INVALID, "bottom overflow rejected");
}

static void chunk_and_authority_test(struct process *owner) {
    struct process stranger = {0};
    struct boring_display_scanout_info info;
    struct boring_framebuffer_user_stats stats;

    check(boring_framebuffer_user_release(owner->pid) ==
          BORING_FRAMEBUFFER_USER_OK, "release between geometries");
    configure(MAX_WIDTH, 1U, MAX_WIDTH * 4U);
    check(boring_framebuffer_user_claim(owner->pid, &info) ==
          BORING_FRAMEBUFFER_USER_OK, "wide claim");
    check(boring_framebuffer_user_present_region(
              owner, HANDLE, 0U, 0U, MAX_WIDTH, 1U) ==
          BORING_FRAMEBUFFER_USER_OK, "region row larger than scratch chunk");
    check(physical_pixel_matches_source(0U, 0U) &&
          physical_pixel_matches_source(1023U, 0U) &&
          physical_pixel_matches_source(1024U, 0U) &&
          physical_pixel_matches_source(MAX_WIDTH - 1U, 0U),
          "wide region chunk boundary pixels");
    stranger.pid = owner->pid + 1ULL;
    stranger.state = PROCESS_ALIVE;
    check(boring_framebuffer_user_present_region(
              &stranger, HANDLE, 0U, 0U, 1U, 1U) ==
          BORING_FRAMEBUFFER_USER_ACCESS, "foreign region present rejected");
    check(boring_framebuffer_user_get_stats(&stats) &&
          (stats.presents == 3ULL) && (stats.full_presents == 1ULL) &&
          (stats.region_presents == 2ULL) &&
          (stats.pixels_presented == 1974ULL),
          "wide region accounting");
    check(boring_framebuffer_user_release(owner->pid) ==
          BORING_FRAMEBUFFER_USER_OK, "final release");
}

int main(void) {
    struct process owner = {0};

    owner.pid = 17ULL;
    owner.state = PROCESS_ALIVE;
    small_region_test(&owner);
    chunk_and_authority_test(&owner);
    if (failures != 0U) {
        (void)fprintf(stderr,
                      "framebuffer-present-region-host-test: %u failure(s)\n",
                      failures);
        return 1;
    }
    (void)puts("Framebuffer bounded region present host tests passed.");
    return 0;
}
