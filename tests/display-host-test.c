#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <boring/display_abi.h>

#include "../user/boring-display/core.h"

static int failures;

static void check(bool condition, const char *name) {
    if (!condition) {
        (void)fprintf(stderr, "display-host-test: FAIL: %s\n", name);
        ++failures;
    }
}

static struct boring_display_scanout_info scanout(uint32_t width,
                                                   uint32_t height) {
    struct boring_display_scanout_info info;

    info.version = BORING_DISPLAY_SCANOUT_VERSION;
    info.width = width;
    info.height = height;
    info.stride = width * BORING_DISPLAY_BYTES_PER_PIXEL;
    info.byte_size = (uint64_t)info.stride * (uint64_t)height;
    return info;
}

static struct boring_display_request create_request(uint32_t width,
                                                     uint32_t height) {
    struct boring_display_request request;

    (void)memset(&request, 0, sizeof(request));
    request.version = BORING_DISPLAY_PROTOCOL_VERSION;
    request.type = BORING_DISPLAY_REQUEST_CREATE;
    request.pixel_format = BORING_DISPLAY_PIXEL_FORMAT_XRGB8888;
    request.width = width;
    request.height = height;
    request.stride = width * BORING_DISPLAY_BYTES_PER_PIXEL;
    request.byte_size = (uint64_t)request.stride * (uint64_t)height;
    return request;
}

static void fill(uint8_t *pixels, size_t bytes,
                 uint8_t red, uint8_t green, uint8_t blue) {
    size_t offset;

    for (offset = 0U; offset + 3U < bytes; offset += 4U) {
        pixels[offset] = blue;
        pixels[offset + 1U] = green;
        pixels[offset + 2U] = red;
        pixels[offset + 3U] = 0U;
    }
}

static bool pixel_is(const uint8_t *pixels, uint32_t stride,
                     uint32_t x, uint32_t y,
                     uint8_t red, uint8_t green, uint8_t blue) {
    const size_t offset = (size_t)y * (size_t)stride + (size_t)x * 4U;

    return (pixels[offset] == blue) &&
           (pixels[offset + 1U] == green) &&
           (pixels[offset + 2U] == red) &&
           (pixels[offset + 3U] == 0U);
}

static void validation_tests(void) {
    struct boring_display_core core;
    struct boring_display_scanout_info info = scanout(320U, 240U);
    struct boring_display_request request = create_request(64U, 64U);
    const uint64_t valid_size = request.byte_size;

    check(boring_display_core_init(&core, &info), "valid scanout");
    info.version = 0U;
    check(!boring_display_core_init(&core, &info), "scanout version");
    info = scanout(320U, 240U);
    check(boring_display_core_init(&core, &info), "scanout restore");

    check(boring_display_validate_create(&core, &request, valid_size) ==
          BORING_DISPLAY_STATUS_OK, "valid create");
    request.version = 0U;
    check(boring_display_validate_create(&core, &request, valid_size) ==
          BORING_DISPLAY_STATUS_INVALID, "protocol version");
    request = create_request(64U, 64U);
    request.type = 99U;
    check(boring_display_validate_create(&core, &request, valid_size) ==
          BORING_DISPLAY_STATUS_INVALID, "opcode");
    request = create_request(64U, 64U);
    request.surface_token = 0x101U;
    check(boring_display_validate_create(&core, &request, valid_size) ==
          BORING_DISPLAY_STATUS_INVALID, "create token");
    request = create_request(64U, 64U);
    request.reserved = 1U;
    check(boring_display_validate_create(&core, &request, valid_size) ==
          BORING_DISPLAY_STATUS_INVALID, "reserved");
    request = create_request(64U, 64U);
    request.pixel_format = 99U;
    check(boring_display_validate_create(&core, &request, valid_size) ==
          BORING_DISPLAY_STATUS_INVALID, "pixel format");
    request = create_request(64U, 64U);
    request.width = 0U;
    check(boring_display_validate_create(&core, &request, valid_size) ==
          BORING_DISPLAY_STATUS_INVALID, "zero width");
    request = create_request(64U, 64U);
    request.height = core.height + 1U;
    check(boring_display_validate_create(&core, &request, valid_size) ==
          BORING_DISPLAY_STATUS_INVALID, "height bound");
    request = create_request(64U, 64U);
    request.stride += 4U;
    check(boring_display_validate_create(&core, &request, valid_size) ==
          BORING_DISPLAY_STATUS_INVALID, "stride");
    request = create_request(64U, 64U);
    request.byte_size -= 4ULL;
    check(boring_display_validate_create(&core, &request, valid_size) ==
          BORING_DISPLAY_STATUS_INVALID, "declared size");
    request = create_request(64U, 64U);
    check(boring_display_validate_create(&core, &request, valid_size + 4ULL) ==
          BORING_DISPLAY_STATUS_INVALID, "authoritative backing size");

    request = create_request(1U, 1U);
    core.width = UINT32_MAX;
    core.height = UINT32_MAX;
    request.width = UINT32_MAX;
    request.height = 1U;
    request.stride = UINT32_MAX;
    request.byte_size = UINT64_MAX;
    check(boring_display_validate_create(&core, &request, UINT64_MAX) ==
          BORING_DISPLAY_STATUS_INVALID, "arithmetic overflow");
}

static void authority_and_generation_tests(void) {
    struct boring_display_core core;
    struct boring_display_scanout_info info = scanout(320U, 240U);
    struct boring_display_request request = create_request(16U, 16U);
    uint8_t first_pixels[16U * 16U * 4U];
    uint8_t second_pixels[16U * 16U * 4U];
    uint32_t first_token = 0U;
    uint32_t second_token = 0U;
    uint32_t handle = 0U;
    uint8_t *pixels = NULL;
    uint32_t handles[BORING_DISPLAY_SURFACE_MAX];
    uint8_t *mapped[BORING_DISPLAY_SURFACE_MAX];

    check(boring_display_core_init(&core, &info), "authority init");
    check(boring_display_surface_add(&core, 11U, &request, 101U,
                                     first_pixels, &first_token) ==
          BORING_DISPLAY_STATUS_OK, "surface allocation");
    check(first_token != BORING_DISPLAY_SURFACE_INVALID, "surface token");
    check(boring_display_surface_commit(&core, 11U, first_token) ==
          BORING_DISPLAY_STATUS_OK, "owner commit");
    check(boring_display_surface_commit(&core, 22U, first_token) ==
          BORING_DISPLAY_STATUS_ACCESS, "foreign commit rejected");
    check(boring_display_surface_destroy(&core, 22U, first_token,
                                         &handle, &pixels) ==
          BORING_DISPLAY_STATUS_ACCESS, "foreign destroy rejected");
    check(core.live_surfaces == 1U, "foreign operation preserved surface");
    check(boring_display_surface_destroy(&core, 11U, first_token,
                                         &handle, &pixels) ==
          BORING_DISPLAY_STATUS_OK, "owner destroy");
    check((handle == 101U) && (pixels == first_pixels), "destroy resources");
    check(boring_display_surface_commit(&core, 11U, first_token) ==
          BORING_DISPLAY_STATUS_INVALID, "stale token rejected");
    check(boring_display_surface_add(&core, 11U, &request, 102U,
                                     first_pixels, &second_token) ==
          BORING_DISPLAY_STATUS_OK, "slot reuse");
    check(second_token != first_token, "generation advances");
    check(boring_display_surface_add(&core, 22U, &request, 201U,
                                     second_pixels, &first_token) ==
          BORING_DISPLAY_STATUS_OK, "second owner allocation");
    check(boring_display_peer_cleanup(&core, 11U, handles, mapped) == 1U,
          "client cleanup count");
    check(core.live_surfaces == 1U, "client cleanup isolation");
    check(boring_display_surface_commit(&core, 22U, first_token) ==
          BORING_DISPLAY_STATUS_OK, "other client survives cleanup");
}

static void composition_tests(void) {
    struct boring_display_core core;
    struct boring_display_scanout_info info = scanout(256U, 180U);
    struct boring_display_request request = create_request(160U, 100U);
    uint8_t first[160U * 100U * 4U];
    uint8_t second[160U * 100U * 4U];
    uint8_t guarded[(256U * 180U * 4U) + 2U];
    uint8_t *output = &guarded[1];
    uint32_t token_a = 0U;
    uint32_t token_b = 0U;

    fill(first, sizeof(first), 30U, 80U, 220U);
    fill(second, sizeof(second), 230U, 120U, 30U);
    (void)memset(guarded, 0xa5, sizeof(guarded));
    check(boring_display_core_init(&core, &info), "composition init");
    check(boring_display_surface_add(&core, 11U, &request, 101U,
                                     first, &token_a) ==
          BORING_DISPLAY_STATUS_OK, "composition surface A");
    check(boring_display_surface_add(&core, 22U, &request, 201U,
                                     second, &token_b) ==
          BORING_DISPLAY_STATUS_OK, "composition surface B");
    check(boring_display_compose(&core, output, (size_t)info.byte_size),
          "compose");
    check(guarded[0] == 0xa5U && guarded[sizeof(guarded) - 1U] == 0xa5U,
          "composition bounds guards");
    check(pixel_is(output, info.stride, 10U, 10U, 11U, 17U, 24U),
          "dark background");
    check(pixel_is(output, info.stride, 90U, 90U, 30U, 80U, 220U),
          "surface A visible");
    check(pixel_is(output, info.stride, 200U, 150U, 230U, 120U, 30U),
          "deterministic stacking");
    check(pixel_is(output, info.stride, 255U, 179U, 230U, 120U, 30U),
          "right bottom clipping");

    boring_display_cursor_move(&core, INT32_MIN, INT32_MIN);
    check((core.cursor_x == 0U) && (core.cursor_y == 0U),
          "cursor left top clipping");
    boring_display_cursor_move(&core, INT32_MAX, INT32_MAX);
    check((core.cursor_x == 255U) && (core.cursor_y == 179U),
          "cursor right bottom clipping");
    check(!boring_display_compose(&core, output, (size_t)info.byte_size - 1U),
          "composition size rejection");
}

int main(void) {
    check(BORING_DISPLAY_CLIENT_MAX == 8U, "bounded client constant");
    check(BORING_DISPLAY_SURFACE_MAX == 16U, "bounded surface constant");
    validation_tests();
    authority_and_generation_tests();
    composition_tests();

    if (failures != 0) {
        (void)fprintf(stderr, "display-host-test: %d failure(s)\n", failures);
        return 1;
    }
    (void)puts("M34 display protocol/compositor host tests passed.");
    return 0;
}
