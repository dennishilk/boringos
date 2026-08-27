#ifndef BORING_DISPLAY_CORE_H
#define BORING_DISPLAY_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/display_abi.h>
#include <boring/syscall_abi.h>

struct boring_display_surface_state {
    uint32_t owner_endpoint;
    uint32_t token;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t buffer_handle;
    uint8_t *pixels;
    uint32_t generation;
    uint64_t creation_order;
    bool active;
};

struct boring_display_core {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint64_t byte_size;
    uint32_t cursor_x;
    uint32_t cursor_y;
    uint32_t live_surfaces;
    uint64_t next_creation_order;
    struct boring_display_surface_state surfaces[BORING_DISPLAY_SURFACE_MAX];
};

bool boring_display_core_init(struct boring_display_core *core,
                              const struct boring_display_scanout_info *info);
uint32_t boring_display_validate_create(
    const struct boring_display_core *core,
    const struct boring_display_request *request,
    uint64_t actual_buffer_size);
uint32_t boring_display_surface_add(struct boring_display_core *core,
                                    uint32_t owner_endpoint,
                                    const struct boring_display_request *request,
                                    uint32_t buffer_handle,
                                    uint8_t *pixels,
                                    uint32_t *token_out);
uint32_t boring_display_surface_commit(const struct boring_display_core *core,
                                       uint32_t owner_endpoint,
                                       uint32_t token);
uint32_t boring_display_surface_destroy(struct boring_display_core *core,
                                        uint32_t owner_endpoint,
                                        uint32_t token,
                                        uint32_t *buffer_handle_out,
                                        uint8_t **pixels_out);
size_t boring_display_peer_cleanup(struct boring_display_core *core,
                                   uint32_t owner_endpoint,
                                   uint32_t handles[BORING_DISPLAY_SURFACE_MAX],
                                   uint8_t *pixels[BORING_DISPLAY_SURFACE_MAX]);
void boring_display_cursor_move(struct boring_display_core *core,
                                int32_t dx,
                                int32_t dy);
bool boring_display_compose(const struct boring_display_core *core,
                            uint8_t *output,
                            size_t output_size);
void boring_display_compose_cursor(const struct boring_display_core *core,
                                   uint8_t *output);

#endif
