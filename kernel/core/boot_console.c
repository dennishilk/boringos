#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/boot_console.h>
#include <boring/framebuffer.h>
#include <boring/graphics.h>
#include <boring/pixel_font.h>
#include <boring/serial.h>

#define BOOT_CONSOLE_MIN_WIDTH 640ULL
#define BOOT_CONSOLE_MIN_HEIGHT 480ULL
#define BOOT_CONSOLE_MARGIN 24ULL
#define BOOT_CONSOLE_HEADER_X 44ULL
#define BOOT_CONSOLE_HEADER_Y 34ULL
#define BOOT_CONSOLE_VERSION_Y 58ULL
#define BOOT_CONSOLE_DIVIDER_Y 76ULL
#define BOOT_CONSOLE_STAGE_X 44ULL
#define BOOT_CONSOLE_LABEL_X 92ULL
#define BOOT_CONSOLE_STAGE_Y 92ULL
#define BOOT_CONSOLE_FOOTER_SPACE 44ULL
#define BOOT_CONSOLE_TEXT_ADVANCE 6ULL

struct boring_boot_console_event {
    enum boring_boot_console_stage stage;
    enum boring_boot_console_status status;
    char reason[BORING_BOOT_CONSOLE_REASON_CAPACITY];
};

struct boring_boot_console_stage_state {
    enum boring_boot_console_status status;
    char reason[BORING_BOOT_CONSOLE_REASON_CAPACITY];
};

struct boring_boot_console_palette {
    uint32_t background;
    uint32_t panel;
    uint32_t row;
    uint32_t grid;
    uint32_t cyan;
    uint32_t text;
    uint32_t muted;
    uint32_t success;
    uint32_t pending;
    uint32_t failure;
};

static const char *const boot_stage_labels[BORING_BOOT_STAGE_COUNT] = {
    "CPU inventory",
    "PCI inventory",
    "SMBIOS",
    "Physical memory manager",
    "Virtual memory manager",
    "Kernel heap",
    "Exceptions",
    "Input",
    "IRQ",
    "PIT",
    "xHCI controller",
    "USB addressing",
    "USB descriptors",
    "USB HID",
    "USB mass storage",
    "BoringFS persistent root",
    "boring-init",
    "boring-display",
    "BoringWM",
    "automatic terminal",
    "desktop present"
};

static struct boring_boot_console_stage_state
    boot_stage_states[BORING_BOOT_STAGE_COUNT];
static struct boring_boot_console_event
    boot_history[BORING_BOOT_CONSOLE_HISTORY_CAPACITY];
static size_t boot_history_start;
static size_t boot_history_count;
static uint64_t boot_history_dropped;
static const struct boring_framebuffer *boot_surface;
static uint64_t boot_render_count;
static bool boot_framebuffer_activated;
static bool boot_framebuffer_active;
static bool boot_desktop_handoff_complete;

static bool boot_stage_valid(enum boring_boot_console_stage stage) {
    return (stage >= BORING_BOOT_STAGE_CPU_INVENTORY) &&
           (stage < BORING_BOOT_STAGE_COUNT);
}

static void boot_copy_reason(char *destination, const char *reason) {
    size_t index = 0U;

    if (destination == NULL) {
        return;
    }
    if (reason != NULL) {
        while ((reason[index] != '\0') &&
               (index + 1U < (size_t)BORING_BOOT_CONSOLE_REASON_CAPACITY)) {
            destination[index] = reason[index];
            ++index;
        }
    }
    destination[index] = '\0';
}

static size_t boot_text_length(const char *text) {
    size_t length = 0U;

    if (text == NULL) {
        return 0U;
    }
    while ((text[length] != '\0') && (length < 96U)) {
        ++length;
    }
    return length;
}

static const char *boot_status_text(enum boring_boot_console_status status) {
    if (status == BORING_BOOT_STATUS_OK) {
        return "[ OK ]";
    }
    if (status == BORING_BOOT_STATUS_FAIL) {
        return "[FAIL]";
    }
    return "[ .. ]";
}

static void boot_serial_event(enum boring_boot_console_stage stage,
                              enum boring_boot_console_status status,
                              const char *reason) {
    serial_write_string("BOOT-CONSOLE ");
    serial_write_string(boot_status_text(status));
    serial_write_string(" ");
    serial_write_string(boot_stage_labels[stage]);
    if ((status == BORING_BOOT_STATUS_FAIL) &&
        (reason != NULL) && (reason[0] != '\0')) {
        serial_write_string(": ");
        serial_write_string(reason);
    }
    serial_write_string("\n");
}

static void boot_history_append(enum boring_boot_console_stage stage,
                                enum boring_boot_console_status status,
                                const char *reason) {
    size_t slot;

    if (boot_history_count < (size_t)BORING_BOOT_CONSOLE_HISTORY_CAPACITY) {
        slot = (boot_history_start + boot_history_count) %
               (size_t)BORING_BOOT_CONSOLE_HISTORY_CAPACITY;
        ++boot_history_count;
    } else {
        boot_history_start = (boot_history_start + 1U) %
                             (size_t)BORING_BOOT_CONSOLE_HISTORY_CAPACITY;
        slot = (boot_history_start + boot_history_count - 1U) %
               (size_t)BORING_BOOT_CONSOLE_HISTORY_CAPACITY;
        ++boot_history_dropped;
    }
    boot_history[slot].stage = stage;
    boot_history[slot].status = status;
    boot_copy_reason(boot_history[slot].reason, reason);
}

static struct boring_boot_console_palette boot_palette_make(
    const struct boring_framebuffer *surface) {
    struct boring_boot_console_palette palette;

    palette.background = boring_color_pack(surface, 0x08U, 0x0cU, 0x10U);
    palette.panel = boring_color_pack(surface, 0x0eU, 0x15U, 0x1bU);
    palette.row = boring_color_pack(surface, 0x11U, 0x1bU, 0x22U);
    palette.grid = boring_color_pack(surface, 0x18U, 0x28U, 0x31U);
    palette.cyan = boring_color_pack(surface, 0x3aU, 0xcdU, 0xdcU);
    palette.text = boring_color_pack(surface, 0xe8U, 0xefU, 0xf2U);
    palette.muted = boring_color_pack(surface, 0x80U, 0x90U, 0x9aU);
    palette.success = boring_color_pack(surface, 0x72U, 0xd6U, 0x8aU);
    palette.pending = boring_color_pack(surface, 0xe4U, 0xb8U, 0x68U);
    palette.failure = boring_color_pack(surface, 0xffU, 0x60U, 0x60U);
    return palette;
}

static uint32_t boot_status_color(
    const struct boring_boot_console_palette *palette,
    enum boring_boot_console_status status) {
    if (status == BORING_BOOT_STATUS_OK) {
        return palette->success;
    }
    if (status == BORING_BOOT_STATUS_FAIL) {
        return palette->failure;
    }
    return palette->pending;
}

static bool boot_render(void) {
    struct boring_boot_console_palette palette;
    uint64_t panel_width;
    uint64_t panel_height;
    uint64_t available_height;
    uint64_t stage_stride;
    size_t index;

    if (!boot_framebuffer_active ||
        !boring_framebuffer_surface_valid(boot_surface) ||
        (boot_surface->width < BOOT_CONSOLE_MIN_WIDTH) ||
        (boot_surface->height < BOOT_CONSOLE_MIN_HEIGHT) ||
        (boot_surface->width <= 2ULL * BOOT_CONSOLE_MARGIN) ||
        (boot_surface->height <= BOOT_CONSOLE_STAGE_Y +
                                 BOOT_CONSOLE_FOOTER_SPACE)) {
        return false;
    }
    available_height = boot_surface->height - BOOT_CONSOLE_STAGE_Y -
                       BOOT_CONSOLE_FOOTER_SPACE;
    stage_stride = available_height / (uint64_t)BORING_BOOT_STAGE_COUNT;
    if (stage_stride < 12ULL) {
        return false;
    }

    palette = boot_palette_make(boot_surface);
    panel_width = boot_surface->width - 2ULL * BOOT_CONSOLE_MARGIN;
    panel_height = boot_surface->height - 2ULL * 18ULL;

    if (!boring_graphics_clear(boot_surface, palette.background) ||
        !boring_graphics_fill_rect(boot_surface, BOOT_CONSOLE_MARGIN, 18ULL,
                                   panel_width, panel_height, palette.panel) ||
        !boring_graphics_stroke_rect(boot_surface, BOOT_CONSOLE_MARGIN, 18ULL,
                                     panel_width, panel_height, palette.grid)) {
        return false;
    }

    (void)boring_pixel_font_draw_text_scaled(
        boot_surface, BOOT_CONSOLE_HEADER_X, BOOT_CONSOLE_HEADER_Y,
        "BoringOS booting...", palette.text, 2U);
    (void)boring_pixel_font_draw_text(
        boot_surface, BOOT_CONSOLE_HEADER_X, BOOT_CONSOLE_VERSION_Y,
        "BoringKernel 0.0.62-dev", palette.cyan);
    (void)boring_graphics_horizontal_line(
        boot_surface, BOOT_CONSOLE_HEADER_X, BOOT_CONSOLE_DIVIDER_Y,
        panel_width - 40ULL, palette.grid);

    for (index = 0U; index < (size_t)BORING_BOOT_STAGE_COUNT; ++index) {
        const enum boring_boot_console_status status =
            boot_stage_states[index].status;
        const uint64_t y = BOOT_CONSOLE_STAGE_Y +
                           (uint64_t)index * stage_stride;
        const uint32_t status_color = boot_status_color(&palette, status);

        if ((index & 1U) != 0U) {
            (void)boring_graphics_fill_rect(
                boot_surface, BOOT_CONSOLE_HEADER_X - 8ULL, y - 3ULL,
                panel_width - 32ULL, stage_stride, palette.row);
        }
        (void)boring_pixel_font_draw_text(
            boot_surface, BOOT_CONSOLE_STAGE_X, y,
            boot_status_text(status), status_color);
        (void)boring_pixel_font_draw_text(
            boot_surface, BOOT_CONSOLE_LABEL_X, y,
            boot_stage_labels[index], palette.text);
        if ((status == BORING_BOOT_STATUS_FAIL) &&
            (boot_stage_states[index].reason[0] != '\0')) {
            const size_t label_length =
                boot_text_length(boot_stage_labels[index]);
            const uint64_t reason_x = BOOT_CONSOLE_LABEL_X +
                ((uint64_t)label_length + 2ULL) * BOOT_CONSOLE_TEXT_ADVANCE;

            (void)boring_pixel_font_draw_text(
                boot_surface, reason_x, y, ": ", palette.failure);
            (void)boring_pixel_font_draw_text(
                boot_surface, reason_x + 2ULL * BOOT_CONSOLE_TEXT_ADVANCE,
                y, boot_stage_states[index].reason, palette.failure);
        }
    }

    (void)boring_pixel_font_draw_text(
        boot_surface, BOOT_CONSOLE_HEADER_X,
        boot_surface->height - 34ULL,
        "Serial mirror active | POST breadcrumbs independent",
        palette.muted);
    ++boot_render_count;
    return true;
}

static bool boot_record(enum boring_boot_console_stage stage,
                        enum boring_boot_console_status status,
                        const char *reason,
                        bool redraw) {
    if (!boot_stage_valid(stage) ||
        (status < BORING_BOOT_STATUS_PENDING) ||
        (status > BORING_BOOT_STATUS_FAIL) ||
        ((status == BORING_BOOT_STATUS_FAIL) &&
         ((reason == NULL) || (reason[0] == '\0')))) {
        return false;
    }

    boot_stage_states[stage].status = status;
    boot_copy_reason(boot_stage_states[stage].reason,
                     status == BORING_BOOT_STATUS_FAIL ? reason : NULL);
    boot_history_append(stage, status,
                        status == BORING_BOOT_STATUS_FAIL ? reason : NULL);
    boot_serial_event(stage, status, reason);

    if (redraw && boot_framebuffer_active) {
        return boot_render();
    }
    return true;
}

bool boring_boot_console_pending(enum boring_boot_console_stage stage) {
    return boot_record(stage, BORING_BOOT_STATUS_PENDING, NULL, true);
}

bool boring_boot_console_ok(enum boring_boot_console_stage stage) {
    return boot_record(stage, BORING_BOOT_STATUS_OK, NULL, true);
}

bool boring_boot_console_fail(enum boring_boot_console_stage stage,
                              const char *reason) {
    return boot_record(stage, BORING_BOOT_STATUS_FAIL, reason, true);
}

bool boring_boot_console_activate(
    const struct boring_framebuffer *surface) {
    if (boot_desktop_handoff_complete ||
        !boring_framebuffer_surface_valid(surface) ||
        (surface->width < BOOT_CONSOLE_MIN_WIDTH) ||
        (surface->height < BOOT_CONSOLE_MIN_HEIGHT)) {
        return false;
    }

    boot_surface = surface;
    boot_framebuffer_active = true;
    if (!boot_render()) {
        boot_surface = NULL;
        boot_framebuffer_active = false;
        serial_write_string(
            "BOOT-CONSOLE framebuffer activation rejected\n");
        return false;
    }
    boot_framebuffer_activated = true;
    serial_write_string(
        "BOOT-CONSOLE framebuffer activated at safe normal present\n");
    serial_write_string("BOOT-CONSOLE history replay complete events=");
    serial_write_u64((uint64_t)boot_history_count);
    serial_write_string(" stages=");
    serial_write_u64((uint64_t)BORING_BOOT_STAGE_COUNT);
    serial_write_string("\n");
    return true;
}

bool boring_boot_console_refresh(void) {
    return boot_framebuffer_active && boot_render();
}

void boring_boot_console_desktop_handoff(void) {
    (void)boot_record(BORING_BOOT_STAGE_DESKTOP_PRESENT,
                      BORING_BOOT_STATUS_OK, NULL, false);
    boot_framebuffer_active = false;
    boot_surface = NULL;
    boot_desktop_handoff_complete = true;
    serial_write_string(
        "BOOT-CONSOLE framebuffer handoff complete; redraw disabled\n");
}

bool boring_boot_console_get_stage(
    enum boring_boot_console_stage stage,
    struct boring_boot_console_stage_info *info) {
    if (!boot_stage_valid(stage) || (info == NULL)) {
        return false;
    }
    info->stage = stage;
    info->status = boot_stage_states[stage].status;
    info->label = boot_stage_labels[stage];
    info->reason = boot_stage_states[stage].reason;
    return true;
}

bool boring_boot_console_get_history(
    size_t index,
    struct boring_boot_console_stage_info *info) {
    size_t slot;

    if ((info == NULL) || (index >= boot_history_count)) {
        return false;
    }
    slot = (boot_history_start + index) %
           (size_t)BORING_BOOT_CONSOLE_HISTORY_CAPACITY;
    info->stage = boot_history[slot].stage;
    info->status = boot_history[slot].status;
    info->label = boot_stage_labels[boot_history[slot].stage];
    info->reason = boot_history[slot].reason;
    return true;
}

bool boring_boot_console_get_stats(struct boring_boot_console_stats *stats) {
    if (stats == NULL) {
        return false;
    }
    stats->history_count = boot_history_count;
    stats->history_dropped = boot_history_dropped;
    stats->render_count = boot_render_count;
    stats->pre_activation_framebuffer_writes = 0ULL;
    stats->framebuffer_activated = boot_framebuffer_activated;
    stats->framebuffer_active = boot_framebuffer_active;
    stats->desktop_handoff_complete = boot_desktop_handoff_complete;
    return true;
}

#ifdef BORING_BOOT_CONSOLE_TEST
void boring_boot_console_test_reset(void) {
    size_t index;

    for (index = 0U; index < (size_t)BORING_BOOT_STAGE_COUNT; ++index) {
        boot_stage_states[index].status = BORING_BOOT_STATUS_PENDING;
        boot_stage_states[index].reason[0] = '\0';
    }
    for (index = 0U;
         index < (size_t)BORING_BOOT_CONSOLE_HISTORY_CAPACITY; ++index) {
        boot_history[index].stage = BORING_BOOT_STAGE_CPU_INVENTORY;
        boot_history[index].status = BORING_BOOT_STATUS_PENDING;
        boot_history[index].reason[0] = '\0';
    }
    boot_history_start = 0U;
    boot_history_count = 0U;
    boot_history_dropped = 0ULL;
    boot_surface = NULL;
    boot_render_count = 0ULL;
    boot_framebuffer_activated = false;
    boot_framebuffer_active = false;
    boot_desktop_handoff_complete = false;
}
#endif
