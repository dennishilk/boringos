#include <boring/desktop_log.h>
#include <boring/display.h>
#include <boring/event.h>
#include <boring/input_abi.h>
#include <boring/ipc.h>
#include <boring/wm.h>
#include "managed.h"

#define DISPLAY_PEERS 16U
static struct boring_display_core core;
static struct display_managed managed;
static struct display_layout_damage layout_damage;
static uint32_t peers[DISPLAY_PEERS];
static uint32_t composition;
static uint8_t *pixels;
static struct boring_display_cursor_damage cursor_damage;
static bool input_pending, manager_seen, focus_present_pending;
#if defined(BORING_M61_PHYSICAL_BREADCRUMBS)
static bool m61_post37_present_return_probed;
static bool m61_post37_loop_reentry_pending;
#endif
int boring_main(void);

#if defined(BORING_M66_USB_HUB_MOUSE)
#define M66_DAMAGE_WITNESS_CAPACITY 64U
static bool m66_damage_append(char *record, size_t capacity, size_t *used,
                              const char *text) {
    size_t index = 0U;
    while (text[index] != '\0') {
        if ((*used + 1U) >= capacity) { return false; }
        record[*used] = text[index];
        *used += 1U;
        ++index;
    }
    return true;
}

static void m66_damage_witness(const char *kind, uint32_t width, uint32_t height) {
    char record[M66_DAMAGE_WITNESS_CAPACITY];
    char number[21];
    size_t used = 0U;
    const size_t number_used = desktop_number(
        number, 0U, (uint64_t)width * (uint64_t)height);
    number[number_used] = '\0';
    if (!m66_damage_append(record, sizeof(record), &used, "m66-damage: ") ||
        !m66_damage_append(record, sizeof(record), &used, kind) ||
        !m66_damage_append(record, sizeof(record), &used, " pixels=") ||
        !m66_damage_append(record, sizeof(record), &used, number) ||
        (used + 2U > sizeof(record))) {
        desktop_fail("M66 damage witness overflow");
    }
    record[used++] = '\n';
    record[used] = '\0';
    desktop_say(record);
}
#endif

#if defined(BORING_M61_PHYSICAL_BREADCRUMBS)
static void m61_post37_present_return_probe(uint32_t endpoint) {
    struct boring_event_watch watch = {BORING_EVENT_IPC, endpoint, 0U, 0U, 0ULL};
    (void)boring_event_wait(&watch, 1U, BORING_EVENT_QUERY);
}

static void m61_post37_loop_reentry_probe(uint32_t listener) {
    struct boring_event_watch watches[2] = {
        {BORING_EVENT_IPC, listener, 0U, 0U, 0ULL},
        {BORING_EVENT_IPC, listener, 0U, 0U, 0ULL}
    };
    (void)boring_event_wait(watches, 2U, BORING_EVENT_QUERY);
}
#endif

static void present(void) {
    if (!display_managed_compose_scene(&managed, &core, pixels,
                                       (size_t)core.byte_size) ||
        !boring_display_cursor_damage_reset(&cursor_damage, &core, pixels,
                                            (size_t)core.byte_size) ||
        (boring_framebuffer_present(composition) != 0L)) {
        desktop_fail("display present");
    }
#if defined(BORING_M66_USB_HUB_MOUSE)
    m66_damage_witness("full", core.width, core.height);
#endif
}

static void present_layout(void) {
    struct boring_display_region regions[BORING_DISPLAY_LAYOUT_REGION_MAX];
    size_t count = 0U;
    size_t index;
    if (layout_damage.full) {
        present();
        display_managed_layout_complete(&layout_damage);
        return;
    }
    if (!display_managed_layout_regions(&managed, &layout_damage, &core,
                                        regions, BORING_DISPLAY_LAYOUT_REGION_MAX,
                                        &count)) {
        desktop_fail("display layout regions");
    }
    if (count == 0U) {
        display_managed_layout_complete(&layout_damage);
        return;
    }
    if (!boring_display_cursor_damage_restore(&cursor_damage, &core, pixels,
                                              (size_t)core.byte_size)) {
        desktop_fail("display layout cursor restore");
    }
    for (index = 0U; index < count; ++index) {
        uint64_t composed_pixels = 0ULL;
        if (!display_managed_compose_scene_region(&managed, &core, pixels,
                                                  (size_t)core.byte_size,
                                                  &regions[index],
                                                  &composed_pixels)) {
            desktop_fail("display layout compose");
        }
        (void)composed_pixels;
    }
    if (!boring_display_cursor_damage_reset(&cursor_damage, &core, pixels,
                                            (size_t)core.byte_size)) {
        desktop_fail("display layout cursor reset");
    }
    for (index = 0U; index < count; ++index) {
        const struct boring_display_region *region = &regions[index];
        if (boring_framebuffer_present_region(composition,
                                              region->x, region->y,
                                              region->width,
                                              region->height) != 0L) {
            desktop_fail("display layout present");
        }
#if defined(BORING_M66_USB_HUB_MOUSE)
        m66_damage_witness("layout", region->width, region->height);
#endif
    }
    display_managed_layout_complete(&layout_damage);
}

static bool present_damage(uint32_t surface,
                           const struct boring_display_region *client_damage) {
    struct boring_display_region screen_damage;
    uint64_t composed_pixels = 0ULL;
    if (!display_managed_damage_region(&managed, &core, surface,
                                       client_damage, &screen_damage)) {
        return false;
    }
    if ((screen_damage.width == 0U) || (screen_damage.height == 0U)) {
        return true;
    }
    if (!boring_display_cursor_damage_restore(&cursor_damage, &core, pixels,
                                              (size_t)core.byte_size) ||
        !display_managed_compose_scene_region(&managed, &core, pixels,
                                              (size_t)core.byte_size,
                                              &screen_damage,
                                              &composed_pixels) ||
        !boring_display_cursor_damage_reset(&cursor_damage, &core, pixels,
                                            (size_t)core.byte_size) ||
        (boring_framebuffer_present_region(composition,
                                           screen_damage.x,
                                           screen_damage.y,
                                           screen_damage.width,
                                           screen_damage.height) != 0L)) {
        desktop_fail("display damage present");
    }
#if defined(BORING_M66_USB_HUB_MOUSE)
    m66_damage_witness("scene", screen_damage.width, screen_damage.height);
#endif
    (void)composed_pixels;
    return true;
}

static void present_focus_borders(void) {
    struct boring_display_region regions[BORING_DISPLAY_FOCUS_REGION_MAX];
    size_t count = 0U;
    size_t index;
    uint64_t pixel_count = 0ULL;
    if (!boring_display_cursor_damage_restore(&cursor_damage, &core, pixels,
                                              (size_t)core.byte_size)) {
        desktop_fail("display focus cursor restore");
    }
    if (!display_managed_compose_focus_borders(&managed, &core, pixels,
                                               (size_t)core.byte_size,
                                               regions,
                                               BORING_DISPLAY_FOCUS_REGION_MAX,
                                               &count, &pixel_count)) {
        present();
        return;
    }
    if (!boring_display_cursor_damage_reset(&cursor_damage, &core, pixels,
                                            (size_t)core.byte_size)) {
        desktop_fail("display focus cursor reset");
    }
    for (index = 0U; index < count; ++index) {
        const struct boring_display_region *region = &regions[index];
        if (boring_framebuffer_present_region(composition, region->x, region->y,
                                              region->width, region->height) != 0L) {
            desktop_fail("display focus border present");
        }
#if defined(BORING_M66_USB_HUB_MOUSE)
        m66_damage_witness("focus", region->width, region->height);
#endif
    }
}

static void present_cursor_move(int32_t dx, int32_t dy) {
    struct boring_display_region old_region;
    struct boring_display_region new_region;
    if (!boring_display_cursor_damage_move(&cursor_damage, &core, pixels,
                                           (size_t)core.byte_size,
                                           dx, dy, &old_region, &new_region)) {
        desktop_fail("display cursor damage");
    }
    if ((old_region.width == 0U) || (old_region.height == 0U)) { return; }
    if ((boring_framebuffer_present_region(composition,
                                           old_region.x, old_region.y,
                                           old_region.width, old_region.height) != 0L) ||
        (boring_framebuffer_present_region(composition,
                                           new_region.x, new_region.y,
                                           new_region.width, new_region.height) != 0L)) {
        desktop_fail("display cursor damage present");
    }
#if defined(BORING_M66_USB_HUB_MOUSE)
    m66_damage_witness("cursor", old_region.width, old_region.height);
    m66_damage_witness("cursor", new_region.width, new_region.height);
#endif
}

static void forget_peer(uint32_t endpoint) {
    uint32_t handles[BORING_DISPLAY_SURFACE_MAX];
    uint8_t *maps[BORING_DISPLAY_SURFACE_MAX];
    size_t index;
    size_t count;
    for (index = 0U; index < BORING_DISPLAY_SURFACE_MAX; ++index) {
        if (core.surfaces[index].active &&
            (core.surfaces[index].owner_endpoint == endpoint)) {
            display_managed_forget(&managed, core.surfaces[index].token);
        }
    }
    count = boring_display_peer_cleanup(&core, endpoint, handles, maps);
    for (index = 0U; index < count; ++index) {
        if ((boring_buffer_unmap(maps[index]) != 0L) ||
            (boring_buffer_close(handles[index]) != 0L)) {
            desktop_fail("display peer buffer cleanup");
        }
    }
    if (managed.manager_endpoint == endpoint) {
        managed.manager_endpoint = 0U;
        input_pending = false;
        focus_present_pending = false;
        display_managed_layout_complete(&layout_damage);
        desktop_say("display: manager disconnected; display survives\n");
    }
    for (index = 0U; index < DISPLAY_PEERS; ++index) {
        if (peers[index] == endpoint) { peers[index] = 0U; }
    }
    (void)boring_ipc_close(endpoint);
    present();
}

static void send_message(uint32_t endpoint, const void *data, size_t size) {
    if (boring_ipc_send(endpoint, data, size, 0U) != 0L) { forget_peer(endpoint); }
}

static void control_reply(uint32_t endpoint, uint32_t status, uint32_t surface) {
    struct display_event reply = {0};
    reply.version = BORING_DISPLAY_CONTROL_VERSION;
    reply.type = DISPLAY_REPLY;
    reply.status = status;
    reply.surface = surface;
    reply.width = core.width;
    reply.height = core.height;
    reply.cursor_x = core.cursor_x;
    reply.cursor_y = core.cursor_y;
    send_message(endpoint, &reply, sizeof(reply));
}

static bool metadata_empty(const struct boring_display_request *r) {
    return (r->reserved == 0U) && (r->width == 0U) && (r->height == 0U) &&
           (r->stride == 0U) && (r->byte_size == 0ULL) &&
           (r->pixel_format == 0U);
}

static void old_request(uint32_t endpoint,
                        const struct boring_display_request *r,
                        uint32_t attachment) {
    uint32_t status = BORING_DISPLAY_STATUS_INVALID;
    uint32_t token = r->surface_token;
    struct boring_display_reply reply = {
        BORING_DISPLAY_PROTOCOL_VERSION, 0U, 0U, 0U
    };
    if ((r->type == BORING_DISPLAY_REQUEST_CREATE) && (attachment != 0U)) {
        long size = boring_buffer_info(attachment);
        if (size > 0L) {
            status = boring_display_validate_create(&core, r, (uint64_t)size);
        }
        if (status == BORING_DISPLAY_STATUS_OK) {
            uint8_t *map = boring_buffer_map(attachment);
            if (map != NULL) {
                status = boring_display_surface_add(&core, endpoint, r,
                                                    attachment, map, &token);
                if (status == BORING_DISPLAY_STATUS_OK) { attachment = 0U; }
                else { (void)boring_buffer_unmap(map); }
            } else {
                status = BORING_DISPLAY_STATUS_NO_SPACE;
            }
        }
    } else if (attachment == 0U) {
        if ((r->type == BORING_DISPLAY_REQUEST_COMMIT) && metadata_empty(r)) {
            status = boring_display_surface_commit(&core, endpoint, token);
            if (status == BORING_DISPLAY_STATUS_OK) { present(); }
        } else if ((r->type == BORING_DISPLAY_REQUEST_DESTROY) && metadata_empty(r)) {
            uint32_t handle = 0U;
            uint8_t *map = NULL;
            status = boring_display_surface_destroy(&core, endpoint, token,
                                                    &handle, &map);
            if (status == BORING_DISPLAY_STATUS_OK) {
                display_managed_forget(&managed, token);
                (void)boring_buffer_unmap(map);
                (void)boring_buffer_close(handle);
                present();
            }
        } else if (r->type == BORING_DISPLAY_REQUEST_COMMIT_DAMAGE) {
            const struct boring_display_damage_request *damage =
                (const struct boring_display_damage_request *)(const void *)r;
            if ((damage->reserved == 0U) && (damage->reserved2 == 0ULL) &&
                (damage->width != 0U) && (damage->height != 0U)) {
                struct boring_display_region region = {
                    damage->x, damage->y, damage->width, damage->height
                };
                status = boring_display_surface_commit(&core, endpoint, token);
                if ((status == BORING_DISPLAY_STATUS_OK) &&
                    !present_damage(token, &region)) {
                    status = BORING_DISPLAY_STATUS_INVALID;
                }
            }
        }
    }
    if (attachment != 0U) { (void)boring_buffer_close(attachment); }
    reply.status = status;
    reply.surface_token = status == BORING_DISPLAY_STATUS_OK ? token : 0U;
    send_message(endpoint, &reply, sizeof(reply));
}

static void control(uint32_t endpoint, const struct display_control *r) {
    uint32_t status = display_control_validate(r, sizeof(*r));
#if defined(BORING_M61_PHYSICAL_BREADCRUMBS)
    bool m61_post37_present_completed = false;
#endif
    long peer = boring_endpoint_peer(endpoint);
    if ((status == BORING_DISPLAY_STATUS_OK) && (peer <= 0L)) {
        forget_peer(endpoint);
        return;
    }
    if (status == BORING_DISPLAY_STATUS_OK) {
        if (r->type == DISPLAY_INFO) {
            /* Read-only scanout metadata is public, no management authority. */
        } else if (r->type == DISPLAY_MANAGER) {
            long probe = boring_service_connect(BORING_WM_SERVICE,
                                                BORING_WM_SERVICE_LENGTH);
            if ((managed.manager_endpoint != 0U) || manager_seen || (probe <= 0L)) {
                status = BORING_DISPLAY_STATUS_ACCESS;
            } else if (boring_endpoint_peer((uint32_t)probe) != peer) {
                status = BORING_DISPLAY_STATUS_ACCESS;
            } else {
                managed.manager_endpoint = endpoint;
                manager_seen = true;
                desktop_say("display: exact manager endpoint authenticated\n");
            }
            if (probe > 0L) { (void)boring_ipc_close((uint32_t)probe); }
        } else if (r->type == DISPLAY_INPUT_ACK) {
            if ((endpoint == managed.manager_endpoint) && input_pending) {
                input_pending = false;
                return;
            }
            status = BORING_DISPLAY_STATUS_ACCESS;
        } else {
            status = display_managed_layout_control(&managed, &layout_damage,
                                                    &core, endpoint,
                                                    (uint64_t)peer, r);
            if ((status == BORING_DISPLAY_STATUS_OK) &&
                ((r->type == DISPLAY_PRESENT) ||
                 (r->type == DISPLAY_PRESENT_FOCUS))) {
                if (r->type == DISPLAY_PRESENT_FOCUS) {
                    focus_present_pending = true;
                } else {
                    focus_present_pending = false;
                    present_layout();
                }
#if defined(BORING_M61_PHYSICAL_BREADCRUMBS)
                if (r->type == DISPLAY_PRESENT) { m61_post37_present_completed = true; }
#endif
            }
        }
    }
    control_reply(endpoint, status, r->surface);
#if defined(BORING_M61_PHYSICAL_BREADCRUMBS)
    if (m61_post37_present_completed && !m61_post37_present_return_probed) {
        m61_post37_present_return_probe(endpoint);
        m61_post37_present_return_probed = true;
        m61_post37_loop_reentry_pending = true;
    }
#endif
}

static void receive(uint32_t endpoint) {
    union {
        uint8_t bytes[BORING_IPC_INLINE_PAYLOAD_MAX];
        struct display_control control;
        struct boring_display_request old;
    } payload = {0};
    struct boring_ipc_receive_result result;
    long status = boring_ipc_receive(endpoint, &payload, sizeof(payload), &result);
    if (status != 0L) { forget_peer(endpoint); return; }
    if ((result.payload_length == sizeof(payload.old)) &&
        (payload.old.version == BORING_DISPLAY_PROTOCOL_VERSION)) {
        old_request(endpoint, &payload.old, result.buffer_handle);
        return;
    }
    if (result.buffer_handle != 0U) {
        (void)boring_buffer_close(result.buffer_handle);
    }
    if ((result.payload_length == sizeof(payload.control)) &&
        (result.buffer_handle == 0U)) {
        control(endpoint, &payload.control);
    } else {
        control_reply(endpoint, BORING_DISPLAY_STATUS_INVALID, 0U);
    }
}

static void input(void) {
    struct boring_input_event event;
    struct display_event message = {0};
    if (boring_input_read(&event, 1U) != 1L) { desktop_fail("display M31 read"); }
#if defined(BORING_M66_USB_HUB_MOUSE)
    if (event.type == BORING_INPUT_EVENT_MOUSE_MOVE) {
        desktop_say("display: M66 USB hub mouse movement reached Ring3 desktop\n");
    } else if ((event.type == BORING_INPUT_EVENT_MOUSE_BUTTON) &&
               (event.code == BORING_MOUSE_BUTTON_LEFT)) {
        desktop_say(event.value1 != 0 ?
            "display: M66 USB hub left button down reached Ring3 desktop\n" :
            "display: M66 USB hub left button up reached Ring3 desktop\n");
    }
#elif defined(BORING_M54_USB_ONLY_DESKTOP)
    if (event.type == BORING_INPUT_EVENT_MOUSE_MOVE) {
        desktop_say("display: M54 USB tablet movement reached Ring3 desktop\n");
    } else if ((event.type == BORING_INPUT_EVENT_MOUSE_BUTTON) &&
               (event.code == BORING_MOUSE_BUTTON_LEFT)) {
        desktop_say(event.value1 != 0 ?
            "display: M54 USB left button down reached Ring3 desktop\n" :
            "display: M54 USB left button up reached Ring3 desktop\n");
    }
#endif
#ifdef BORING_M38_DISPLAY_DEATH_ACCEPTANCE
    if ((event.type == BORING_INPUT_EVENT_KEY) &&
        (event.code == BORING_KEY_F11) &&
        (event.value1 == BORING_KEY_DOWN_VALUE)) {
        desktop_say("display: M38 test-only unexpected Ring3 exit\n");
        boring_exit(73);
    }
#endif
    if (event.type == BORING_INPUT_EVENT_MOUSE_MOVE) {
        present_cursor_move(event.value1, event.value2);
    }
    if (managed.manager_endpoint == 0U) { return; }
    message.version = BORING_DISPLAY_CONTROL_VERSION;
    message.type = DISPLAY_INPUT;
    message.input = event;
    message.cursor_x = core.cursor_x;
    message.cursor_y = core.cursor_y;
    input_pending = true;
    send_message(managed.manager_endpoint, &message, sizeof(message));
}

int boring_main(void) {
    struct boring_display_scanout_info info;
    long listener;
    long buffer;
    if ((boring_framebuffer_claim(&info) != 0L) ||
        !boring_display_core_init(&core, &info)) {
        desktop_fail("display claim/init");
    }
    display_managed_init(&managed);
    display_layout_damage_init(&layout_damage);
    boring_display_cursor_damage_init(&cursor_damage);
    buffer = boring_buffer_create((size_t)info.byte_size);
    if (buffer <= 0L) { desktop_fail("display composition buffer"); }
    composition = (uint32_t)buffer;
    pixels = boring_buffer_map(composition);
    if (pixels == NULL) { desktop_fail("display composition mapping"); }
    listener = boring_service_register(BORING_DISPLAY_SERVICE_NAME,
                                       BORING_DISPLAY_SERVICE_NAME_LENGTH);
    if ((listener <= 0L) || (boring_input_claim() != 0L)) {
        desktop_fail("display service/input claim");
    }
    present();
    desktop_say("display: M35 service and M31 input ready\n");
    for (;;) {
        struct boring_event_watch watches[DISPLAY_PEERS + 2U] = {0};
        size_t count = 0U;
        size_t index;
#ifdef BORING_BOUNDED_DESKTOP_ACCEPTANCE
        size_t live = 0U;
#endif
        watches[count++] = (struct boring_event_watch){
            BORING_EVENT_IPC, (uint32_t)listener, 0U, 0U, 0ULL
        };
        if (!input_pending) {
            watches[count++] = (struct boring_event_watch){
                BORING_EVENT_INPUT, 0U, 0U, 0U, 0ULL
            };
        }
        for (index = 0U; index < DISPLAY_PEERS; ++index) {
            if (peers[index] != 0U) {
                watches[count++] = (struct boring_event_watch){
                    BORING_EVENT_IPC, peers[index], 0U, 0U, 0ULL
                };
#ifdef BORING_BOUNDED_DESKTOP_ACCEPTANCE
                ++live;
#endif
            }
        }
#ifdef BORING_BOUNDED_DESKTOP_ACCEPTANCE
        if (manager_seen && (managed.manager_endpoint == 0U) && (live == 0U)) {
            desktop_say("display: session drained; exiting with claims\n");
            boring_exit(0);
        }
#endif
#if defined(BORING_M61_PHYSICAL_BREADCRUMBS)
        if (m61_post37_loop_reentry_pending) {
            m61_post37_loop_reentry_probe((uint32_t)listener);
            m61_post37_loop_reentry_pending = false;
        }
#endif
        {
            const bool can_present_focus = focus_present_pending && !input_pending;
            const uint32_t flags = can_present_focus ? BORING_EVENT_QUERY : 0U;
            const long ready = boring_event_wait(watches, count, flags);
            if (ready < 0L) { desktop_fail("display event wait"); }
            if (ready == 0L) {
                if (!can_present_focus) { desktop_fail("display event wait"); }
                present_focus_borders();
                focus_present_pending = false;
                desktop_say("display: focus frame ready\n");
                continue;
            }
        }
        for (index = 0U; index < count; ++index) {
            if (watches[index].events == 0U) { continue; }
            if (watches[index].kind == BORING_EVENT_INPUT) {
                input();
            } else if (watches[index].handle == (uint32_t)listener) {
                long ep = boring_service_accept((uint32_t)listener);
                size_t slot;
                if (ep <= 0L) { desktop_fail("display accept"); }
                for (slot = 0U; slot < DISPLAY_PEERS; ++slot) {
                    if (peers[slot] == 0U) { break; }
                }
                if (slot == DISPLAY_PEERS) { (void)boring_ipc_close((uint32_t)ep); }
                else { peers[slot] = (uint32_t)ep; }
            } else if ((watches[index].events & BORING_EVENT_HUP) != 0U) {
                forget_peer(watches[index].handle);
            } else {
                receive(watches[index].handle);
            }
        }
    }
}
