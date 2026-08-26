#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/boot_dashboard.h>
#include <boring/framebuffer.h>
#include <boring/graphics.h>
#include <boring/pixel_font.h>

#define DASHBOARD_MARGIN 24ULL
#define DASHBOARD_TOP_Y 16ULL
#define DASHBOARD_TOP_HEIGHT 48ULL
#define DASHBOARD_BODY_Y 80ULL
#define DASHBOARD_BOTTOM_MARGIN 24ULL
#define DASHBOARD_GRID_STEP 64ULL

struct dashboard_palette {
    uint32_t background;
    uint32_t panel;
    uint32_t secondary;
    uint32_t grid;
    uint32_t cyan;
    uint32_t text;
    uint32_t muted;
    uint32_t success;
    uint32_t amber;
};

static size_t dashboard_text_length(const char *text) {
    size_t length = 0U;

    if (text == NULL) {
        return 0U;
    }
    while ((text[length] != '\0') && (length < 127U)) {
        ++length;
    }
    return length;
}

static size_t dashboard_u64_decimal(uint64_t value, char *buffer, size_t size) {
    char reversed[21];
    size_t count = 0U;
    size_t index;

    if ((buffer == NULL) || (size == 0U)) {
        return 0U;
    }
    do {
        reversed[count] = (char)('0' + (char)(value % 10ULL));
        value /= 10ULL;
        ++count;
    } while ((value != 0ULL) && (count < sizeof(reversed)));

    if (count + 1U > size) {
        buffer[0] = '\0';
        return 0U;
    }
    for (index = 0U; index < count; ++index) {
        buffer[index] = reversed[count - index - 1U];
    }
    buffer[count] = '\0';
    return count;
}

static bool dashboard_append(char *buffer,
                             size_t capacity,
                             size_t *length,
                             const char *text) {
    size_t index = 0U;

    if ((buffer == NULL) || (length == NULL) || (text == NULL)) {
        return false;
    }
    while (text[index] != '\0') {
        if (*length + 1U >= capacity) {
            return false;
        }
        buffer[*length] = text[index];
        ++(*length);
        ++index;
    }
    buffer[*length] = '\0';
    return true;
}

static void dashboard_make_memory(uint64_t bytes,
                                  char *buffer,
                                  size_t capacity) {
    size_t length;

    if ((buffer == NULL) || (capacity == 0U)) {
        return;
    }
    if (bytes == 0ULL) {
        buffer[0] = 'N';
        if (capacity > 1U) buffer[1] = '/';
        if (capacity > 2U) buffer[2] = 'A';
        if (capacity > 3U) buffer[3] = '\0';
        return;
    }
    length = dashboard_u64_decimal(bytes / (1024ULL * 1024ULL), buffer, capacity);
    if (length == 0U) {
        return;
    }
    (void)dashboard_append(buffer, capacity, &length, " MiB");
}

static void dashboard_make_framebuffer(const struct boring_framebuffer *surface,
                                       char *buffer,
                                       size_t capacity) {
    size_t length = 0U;
    char number[24];

    if ((surface == NULL) || (buffer == NULL) || (capacity == 0U)) {
        return;
    }
    buffer[0] = '\0';
    if (dashboard_u64_decimal(surface->width, number, sizeof(number)) == 0U ||
        !dashboard_append(buffer, capacity, &length, number) ||
        !dashboard_append(buffer, capacity, &length, "x") ||
        dashboard_u64_decimal(surface->height, number, sizeof(number)) == 0U ||
        !dashboard_append(buffer, capacity, &length, number) ||
        !dashboard_append(buffer, capacity, &length, "x") ||
        dashboard_u64_decimal((uint64_t)surface->bpp,
                              number,
                              sizeof(number)) == 0U) {
        buffer[0] = '\0';
        return;
    }
    (void)dashboard_append(buffer, capacity, &length, number);
}

static struct dashboard_palette dashboard_palette_make(
    const struct boring_framebuffer *surface) {
    struct dashboard_palette palette;

    palette.background = boring_color_pack(surface, 0x08U, 0x0cU, 0x10U);
    palette.panel = boring_color_pack(surface, 0x0eU, 0x15U, 0x1bU);
    palette.secondary = boring_color_pack(surface, 0x11U, 0x1bU, 0x22U);
    palette.grid = boring_color_pack(surface, 0x18U, 0x28U, 0x31U);
    palette.cyan = boring_color_pack(surface, 0x3aU, 0xcdU, 0xdcU);
    palette.text = boring_color_pack(surface, 0xe8U, 0xefU, 0xf2U);
    palette.muted = boring_color_pack(surface, 0x80U, 0x90U, 0x9aU);
    palette.success = boring_color_pack(surface, 0x72U, 0xd6U, 0x8aU);
    palette.amber = boring_color_pack(surface, 0xe4U, 0xb8U, 0x68U);
    return palette;
}

static void dashboard_grid(const struct boring_framebuffer *surface,
                           const struct dashboard_palette *palette) {
    uint64_t x;
    uint64_t y;

    for (x = DASHBOARD_GRID_STEP; x < surface->width; x += DASHBOARD_GRID_STEP) {
        (void)boring_graphics_vertical_line(
            surface, x, 0ULL, surface->height, palette->grid);
    }
    for (y = DASHBOARD_GRID_STEP; y < surface->height; y += DASHBOARD_GRID_STEP) {
        (void)boring_graphics_horizontal_line(
            surface, 0ULL, y, surface->width, palette->grid);
    }
}

static void dashboard_logo(const struct boring_framebuffer *surface,
                           uint64_t x,
                           uint64_t y,
                           const struct dashboard_palette *palette) {
    (void)boring_graphics_fill_rect(surface, x, y, 12ULL, 72ULL, palette->cyan);
    (void)boring_graphics_fill_rect(surface, x + 12ULL, y, 34ULL, 10ULL,
                                    palette->cyan);
    (void)boring_graphics_fill_rect(surface, x + 12ULL, y + 31ULL, 30ULL, 10ULL,
                                    palette->cyan);
    (void)boring_graphics_fill_rect(surface, x + 12ULL, y + 62ULL, 34ULL, 10ULL,
                                    palette->cyan);
    (void)boring_graphics_fill_rect(surface, x + 38ULL, y + 8ULL, 10ULL, 25ULL,
                                    palette->cyan);
    (void)boring_graphics_fill_rect(surface, x + 38ULL, y + 39ULL, 10ULL, 25ULL,
                                    palette->cyan);
    (void)boring_graphics_fill_rect(surface, x + 55ULL, y + 58ULL, 8ULL, 8ULL,
                                    palette->success);
}

static void dashboard_label_value(const struct boring_framebuffer *surface,
                                  uint64_t x,
                                  uint64_t y,
                                  const char *label,
                                  const char *value,
                                  const struct dashboard_palette *palette) {
    (void)boring_pixel_font_draw_text(surface, x, y, label, palette->muted);
    (void)boring_pixel_font_draw_text(surface, x + 126ULL, y,
                                      (value != NULL) ? value : "N/A",
                                      palette->text);
}

static void dashboard_status(const struct boring_framebuffer *surface,
                             uint64_t x,
                             uint64_t y,
                             uint64_t width,
                             const char *name,
                             bool online,
                             const struct dashboard_palette *palette) {
    uint32_t indicator = online ? palette->success : palette->amber;

    (void)boring_graphics_fill_rect(surface, x, y, width, 26ULL,
                                    palette->secondary);
    (void)boring_graphics_stroke_rect(surface, x, y, width, 26ULL,
                                      palette->grid);
    (void)boring_graphics_fill_rect(surface, x + 8ULL, y + 9ULL, 7ULL, 7ULL,
                                    indicator);
    (void)boring_pixel_font_draw_text(surface, x + 22ULL, y + 9ULL,
                                      name, palette->text);
}

static void dashboard_topbar(const struct boring_framebuffer *surface,
                             const struct boring_boot_dashboard_info *info,
                             const struct dashboard_palette *palette) {
    uint64_t width = surface->width - (2ULL * DASHBOARD_MARGIN);
    const char *version = (info->kernel_version != NULL) ?
                          info->kernel_version : "N/A";

    (void)boring_graphics_fill_rect(surface, DASHBOARD_MARGIN, DASHBOARD_TOP_Y,
                                    width, DASHBOARD_TOP_HEIGHT, palette->panel);
    (void)boring_graphics_horizontal_line(surface, DASHBOARD_MARGIN,
                                          DASHBOARD_TOP_Y + DASHBOARD_TOP_HEIGHT - 1ULL,
                                          width, palette->cyan);
    (void)boring_pixel_font_draw_text_scaled(surface,
                                             DASHBOARD_MARGIN + 16ULL,
                                             DASHBOARD_TOP_Y + 12ULL,
                                             "BORINGOS",
                                             palette->text,
                                             2U);
    (void)boring_pixel_font_draw_text(surface,
                                     DASHBOARD_MARGIN + 16ULL,
                                     DASHBOARD_TOP_Y + 34ULL,
                                     version,
                                     palette->muted);
    if (surface->width >= 800ULL) {
        uint64_t status_x = surface->width - DASHBOARD_MARGIN - 150ULL;
        (void)boring_pixel_font_draw_text(surface, status_x,
                                          DASHBOARD_TOP_Y + 13ULL,
                                          "KERNEL ONLINE", palette->text);
        (void)boring_graphics_fill_rect(surface,
                                        surface->width - DASHBOARD_MARGIN - 18ULL,
                                        DASHBOARD_TOP_Y + 12ULL,
                                        8ULL, 8ULL, palette->success);
    }
}

static void dashboard_footer(const struct boring_framebuffer *surface,
                             uint64_t body_bottom,
                             const struct dashboard_palette *palette) {
    const char *right = "UNEXCITING BY DESIGN";
    size_t right_len = dashboard_text_length(right);
    uint64_t right_width = (uint64_t)right_len *
                           (uint64_t)BORING_PIXEL_FONT_ADVANCE;
    uint64_t right_x = (surface->width > DASHBOARD_MARGIN + right_width) ?
                       (surface->width - DASHBOARD_MARGIN - right_width) :
                       DASHBOARD_MARGIN;

    if (body_bottom < 18ULL) {
        return;
    }
    (void)boring_pixel_font_draw_text(surface, DASHBOARD_MARGIN + 16ULL,
                                      body_bottom - 18ULL,
                                      "SERIAL CONSOLE ACTIVE",
                                      palette->muted);
    (void)boring_pixel_font_draw_text(surface, right_x,
                                      body_bottom - 18ULL,
                                      right, palette->cyan);
}

bool boring_boot_dashboard_render(
    const struct boring_framebuffer *surface,
    const struct boring_boot_dashboard_info *info) {
    struct dashboard_palette palette;
    char memory[32];
    char framebuffer[48];
    uint64_t body_width;
    uint64_t body_height;
    uint64_t body_bottom;
    uint64_t content_x;
    uint64_t system_x;
    uint64_t system_y;
    uint64_t status_y;
    uint64_t status_width;
    uint64_t gap = 8ULL;

    if (!boring_framebuffer_surface_valid(surface) || (info == NULL) ||
        (surface->width < 320ULL) || (surface->height < 240ULL)) {
        return false;
    }

    palette = dashboard_palette_make(surface);
    if (!boring_graphics_clear(surface, palette.background)) {
        return false;
    }
    dashboard_grid(surface, &palette);

    if ((surface->width <= (2ULL * DASHBOARD_MARGIN)) ||
        (surface->height <= DASHBOARD_BODY_Y + DASHBOARD_BOTTOM_MARGIN)) {
        return false;
    }
    body_width = surface->width - (2ULL * DASHBOARD_MARGIN);
    body_height = surface->height - DASHBOARD_BODY_Y - DASHBOARD_BOTTOM_MARGIN;
    body_bottom = DASHBOARD_BODY_Y + body_height;

    dashboard_topbar(surface, info, &palette);
    (void)boring_graphics_fill_rect(surface, DASHBOARD_MARGIN, DASHBOARD_BODY_Y,
                                    body_width, body_height, palette.panel);
    (void)boring_graphics_stroke_rect(surface, DASHBOARD_MARGIN,
                                      DASHBOARD_BODY_Y,
                                      body_width, body_height, palette.grid);

    content_x = DASHBOARD_MARGIN + 24ULL;
    dashboard_logo(surface, content_x, DASHBOARD_BODY_Y + 24ULL, &palette);
    (void)boring_pixel_font_draw_text_scaled(surface,
                                             content_x + 86ULL,
                                             DASHBOARD_BODY_Y + 28ULL,
                                             "BORING OS",
                                             palette.text,
                                             2U);
    (void)boring_pixel_font_draw_text(surface,
                                     content_x + 86ULL,
                                     DASHBOARD_BODY_Y + 54ULL,
                                     "Independent desktop operating system",
                                     palette.muted);
    (void)boring_pixel_font_draw_text(surface,
                                     content_x + 86ULL,
                                     DASHBOARD_BODY_Y + 70ULL,
                                     "UNEXCITING BY DESIGN",
                                     palette.cyan);

    system_x = content_x;
    system_y = DASHBOARD_BODY_Y + 122ULL;
    (void)boring_pixel_font_draw_text(surface, system_x, system_y,
                                     "SYSTEM", palette.cyan);
    (void)boring_graphics_horizontal_line(surface, system_x, system_y + 14ULL,
                                          body_width - 48ULL,
                                          palette.grid);

    dashboard_make_memory(info->memory_bytes, memory, sizeof(memory));
    dashboard_make_framebuffer(surface, framebuffer, sizeof(framebuffer));
    dashboard_label_value(surface, system_x, system_y + 30ULL,
                          "KERNEL", info->kernel_name, &palette);
    dashboard_label_value(surface, system_x, system_y + 50ULL,
                          "VERSION", info->kernel_version, &palette);
    dashboard_label_value(surface, system_x, system_y + 70ULL,
                          "ARCH", info->arch, &palette);
    dashboard_label_value(surface, system_x, system_y + 90ULL,
                          "MEMORY", memory, &palette);
    dashboard_label_value(surface, system_x, system_y + 110ULL,
                          "FRAMEBUFFER", framebuffer, &palette);
    dashboard_label_value(surface, system_x, system_y + 130ULL,
                          "ROOT", info->root_fs, &palette);
    dashboard_label_value(surface, system_x, system_y + 150ULL,
                          "STORAGE", info->block_device, &palette);
    dashboard_label_value(surface, system_x, system_y + 170ULL,
                          "USERSPACE",
                          info->ring3_available ? "Ring 3 / static ELF64" : "N/A",
                          &palette);

    status_y = system_y + 208ULL;
    (void)boring_pixel_font_draw_text(surface, system_x, status_y,
                                     "BOOT FOUNDATION", palette.cyan);
    status_y += 18ULL;
    status_width = (body_width - 48ULL - (5ULL * gap)) / 6ULL;
    if (status_width < 88ULL) {
        status_width = 88ULL;
    }
    dashboard_status(surface, system_x, status_y, status_width,
                     "PMM", info->pmm_online, &palette);
    dashboard_status(surface, system_x + (status_width + gap), status_y,
                     status_width, "VMM", info->vmm_online, &palette);
    dashboard_status(surface, system_x + (2ULL * (status_width + gap)), status_y,
                     status_width, "IRQ", info->irq_online, &palette);
    dashboard_status(surface, system_x + (3ULL * (status_width + gap)), status_y,
                     status_width, "RING3", info->ring3_available, &palette);
    dashboard_status(surface, system_x + (4ULL * (status_width + gap)), status_y,
                     status_width, "VFS", info->vfs_online, &palette);
    dashboard_status(surface, system_x + (5ULL * (status_width + gap)), status_y,
                     status_width, "BORINGFS", info->boringfs_online, &palette);

    dashboard_footer(surface, body_bottom, &palette);
    return true;
}
