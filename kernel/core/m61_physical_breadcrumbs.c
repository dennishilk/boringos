#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <boring/boot_protocol.h>
#include <boring/boringfs_vfs.h>
#include <boring/cpu.h>
#include <boring/cpu_inventory.h>
#include <boring/descriptor.h>
#include <boring/exception.h>
#include <boring/framebuffer.h>
#include <boring/framebuffer_user.h>
#include <boring/graphics.h>
#include <boring/heap.h>
#include <boring/input.h>
#include <boring/ipc.h>
#include <boring/irq.h>
#include <boring/pci_inventory.h>
#include <boring/pmm.h>
#include <boring/process.h>
#include <boring/serial.h>
#include <boring/smbios.h>
#include <boring/syscall_test.h>
#include <boring/timer.h>
#include <boring/usb_mass_storage.h>
#include <boring/vmm.h>
#include <boring/xhci.h>
#include <boring/xhci_mixed.h>

#ifndef BORING_M61_PHYSICAL_BREADCRUMBS
#error "M61 physical trace must stay candidate-build gated"
#endif

#define TRACE_TEXT_X 80ULL
#define TRACE_STAGE_Y 42ULL
#define TRACE_STAGE_HEIGHT 12ULL
#define TRACE_SCALE 2U
#define TRACE_LAST_STAGE 27U
#define TRACE_PANEL_MAX_WIDTH 632ULL
#define TRACE_WITNESS_X 4ULL
#define TRACE_WITNESS_Y 4ULL
#define TRACE_WITNESS_WIDTH 64ULL
#define TRACE_WITNESS_HEIGHT 16ULL
#define TRACE_WITNESS_STRIPES 8U
#define TRACE_TSC_HOLD_CYCLES 500000000ULL
#define EARLY_IDT_GATE_INTERRUPT 0x8eU
#define EARLY_IDT_IST_NONE 0U
#define EARLY_IDT_IST1 1U

struct m61_early_idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct m61_early_idtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct glyph {
    char character;
    uint16_t bits;
};

_Static_assert(sizeof(struct m61_early_idt_entry) == 16U,
               "M61 emergency IDT entry must be 16 bytes");
_Static_assert(sizeof(struct m61_early_idtr) == 10U,
               "M61 emergency IDTR must be 10 bytes");

extern const uintptr_t
    x86_64_exception_stub_table[X86_64_EXCEPTION_VECTOR_COUNT];
void x86_64_load_idt(const struct m61_early_idtr *idtr);
void x86_64_store_idt(struct m61_early_idtr *idtr);
uint16_t x86_64_read_cs(void);

const char boring_m61_physical_breadcrumbs_enabled[] =
    "M61 safe direct framebuffer boot trace enabled";
#ifdef BORING_M61_EARLY_FAULT_TEST
const char boring_m61_early_fault_test_enabled[] =
    "M61 controlled pre-exception-init fault test enabled";
#endif

static struct m61_early_idt_entry
    early_idt[X86_64_EXCEPTION_VECTOR_COUNT] __attribute__((aligned(16)));
static const struct boring_framebuffer *fb;
static volatile bool early_containment_active;
static volatile bool framebuffer_write_active;
static bool framebuffer_writes_proven;
static bool serial_ready;
static bool wm_ready;
static bool desktop_presented;

static const struct glyph glyphs[] = {
    {'A', 0x2bedU}, {'B', 0x6baeU}, {'C', 0x3923U}, {'D', 0x6b6eU},
    {'E', 0x79a7U}, {'F', 0x79a4U}, {'G', 0x396bU}, {'H', 0x5bedU},
    {'I', 0x7497U}, {'J', 0x126aU}, {'K', 0x5badU}, {'L', 0x4927U},
    {'M', 0x5fedU}, {'N', 0x5ffdU}, {'O', 0x2b6aU}, {'P', 0x6ba4U},
    {'Q', 0x2b7bU}, {'R', 0x6badU}, {'S', 0x388eU}, {'T', 0x7492U},
    {'U', 0x5b6fU}, {'V', 0x5b6aU}, {'W', 0x5bfdU}, {'X', 0x5aadU},
    {'Y', 0x5a92U}, {'Z', 0x72a7U},
    {'0', 0x7b6fU}, {'1', 0x2c97U}, {'2', 0x62a7U}, {'3', 0x628eU},
    {'4', 0x5bc9U}, {'5', 0x798eU}, {'6', 0x39aaU}, {'7', 0x7292U},
    {'8', 0x2aaaU}, {'9', 0x2aceU},
    {'[', 0x6926U}, {']', 0x324bU}, {'>', 0x4454U}, {'+', 0x05d0U},
    {'!', 0x2482U}, {'=', 0x0e38U}, {':', 0x0410U}, {'/', 0x12a4U},
    {'-', 0x01c0U}, {'.', 0x0002U}, {'_', 0x0007U}, {' ', 0U}
};

static void emergency_halt(void) __attribute__((noreturn));
static void emergency_halt(void) {
    x86_64_interrupts_disable();
    x86_64_halt_forever();
}

static void early_idt_set_gate(uint8_t vector, uintptr_t handler,
                               uint16_t selector, uint8_t ist) {
    struct m61_early_idt_entry *const entry = &early_idt[vector];
    const uint64_t address = (uint64_t)handler;

    entry->offset_low = (uint16_t)(address & 0xffffULL);
    entry->selector = selector;
    entry->ist = ist;
    entry->type_attributes = (uint8_t)EARLY_IDT_GATE_INTERRUPT;
    entry->offset_middle = (uint16_t)((address >> 16U) & 0xffffULL);
    entry->offset_high = (uint32_t)((address >> 32U) & 0xffffffffULL);
    entry->reserved = 0U;
}

static bool early_idt_load(uint16_t selector, uint8_t ist) {
    struct m61_early_idtr requested;
    struct m61_early_idtr active;
    size_t index;

    if (selector == 0U) {
        return false;
    }
    for (index = 0U;
         index < (size_t)X86_64_EXCEPTION_VECTOR_COUNT; ++index) {
        if (x86_64_exception_stub_table[index] == (uintptr_t)0U) {
            return false;
        }
        early_idt_set_gate((uint8_t)index,
                           x86_64_exception_stub_table[index],
                           selector, ist);
    }

    requested.limit = (uint16_t)(sizeof(early_idt) - 1U);
    requested.base = (uint64_t)(uintptr_t)&early_idt[0];
    active.limit = 0U;
    active.base = 0ULL;
    x86_64_load_idt(&requested);
    x86_64_store_idt(&active);
    return (active.limit == requested.limit) &&
           (active.base == requested.base);
}

static bool early_containment_install(void) {
    uint16_t selector = x86_64_read_cs();

    early_containment_active = true;
    if (!early_idt_load(selector, (uint8_t)EARLY_IDT_IST_NONE)) {
        return false;
    }

    /*
     * descriptor_init() is static-only and dependency-free. In this
     * diagnostic build it also supplies IST1, giving #PF/#GP/#UD/#DF a
     * dedicated stack before any GOP memory is touched.
     */
    if (!descriptor_init()) {
        return false;
    }
    selector = x86_64_read_cs();
    return early_idt_load(selector, (uint8_t)EARLY_IDT_IST1);
}

static uint16_t glyph_bits(char character) {
    size_t index;

    if ((character >= 'a') && (character <= 'z')) {
        character = (char)(character - ('a' - 'A'));
    }
    for (index = 0U; index < sizeof(glyphs) / sizeof(glyphs[0]); ++index) {
        if (glyphs[index].character == character) {
            return glyphs[index].bits;
        }
    }
    return 0U;
}

static uint32_t trace_color(uint8_t red, uint8_t green, uint8_t blue) {
    return (fb == NULL) ? 0U : boring_color_pack(fb, red, green, blue);
}

static void trace_character(uint64_t x, uint64_t y, char character,
                            uint32_t packed, uint32_t scale) {
    const uint16_t glyph = glyph_bits(character);
    uint32_t row;

    if ((fb == NULL) || (scale == 0U)) {
        return;
    }
    for (row = 0U; row < 5U; ++row) {
        uint32_t column;
        for (column = 0U; column < 3U; ++column) {
            const uint32_t bit = ((4U - row) * 3U) + (2U - column);
            if ((glyph & (uint16_t)(1U << bit)) != 0U) {
                uint32_t yy;
                for (yy = 0U; yy < scale; ++yy) {
                    uint32_t xx;
                    for (xx = 0U; xx < scale; ++xx) {
                        (void)boring_graphics_put_pixel(
                            fb,
                            x + (uint64_t)(column * scale + xx),
                            y + (uint64_t)(row * scale + yy),
                            packed);
                    }
                }
            }
        }
    }
}

static void trace_text(uint64_t x, uint64_t y, const char *value,
                       uint32_t packed, uint32_t scale) {
    size_t index = 0U;
    const uint64_t step = (uint64_t)(4U * scale);

    if ((fb == NULL) || (value == NULL)) {
        return;
    }
    while ((value[index] != '\0') && (index < 96U)) {
        trace_character(x, y, value[index], packed, scale);
        x += step;
        ++index;
    }
}

static uint64_t trace_decimal(uint64_t x, uint64_t y, uint64_t value,
                              uint32_t packed, uint32_t scale) {
    char digits[21];
    size_t count = 0U;

    do {
        digits[count] = (char)('0' + (char)(value % 10ULL));
        ++count;
        value /= 10ULL;
    } while ((value != 0ULL) && (count < sizeof(digits)));

    while (count != 0U) {
        --count;
        trace_character(x, y, digits[count], packed, scale);
        x += (uint64_t)(4U * scale);
    }
    return x;
}

static void trace_hex64(uint64_t x, uint64_t y, uint64_t value,
                        uint32_t packed, uint32_t scale) {
    static const char hex[] = "0123456789ABCDEF";
    int shift;

    trace_text(x, y, "0X", packed, scale);
    x += (uint64_t)(8U * scale);
    for (shift = 60; shift >= 0; shift -= 4) {
        trace_character(
            x, y, hex[(value >> (uint32_t)shift) & 0x0fULL],
            packed, scale);
        x += (uint64_t)(4U * scale);
    }
}

static uint64_t trace_stage_y(uint32_t stage_number) {
    return TRACE_STAGE_Y +
           ((uint64_t)(stage_number - 1U) * TRACE_STAGE_HEIGHT);
}

static void serial_stage(uint32_t stage_number, char mark,
                         const char *label) {
    if (!serial_ready) {
        return;
    }
    serial_write_string("M61 TRACE [");
    if (stage_number < 10U) {
        serial_write_string("0");
    }
    serial_write_u64((uint64_t)stage_number);
    serial_write_string(mark == '>' ? ">] " :
                        mark == '+' ? "+] " : "!] ");
    serial_write_string(label);
    serial_write_string("\n");
}

static uint64_t trace_panel_width(void) {
    uint64_t available;

    if ((fb == NULL) || (fb->width <= 8ULL)) {
        return 0ULL;
    }
    available = fb->width - 8ULL;
    return (available < TRACE_PANEL_MAX_WIDTH) ?
           available : TRACE_PANEL_MAX_WIDTH;
}

static void trace_stage(uint32_t stage_number, char mark,
                        const char *label) {
    uint64_t x;
    const uint64_t y = trace_stage_y(stage_number);
    uint32_t packed;

    if ((stage_number == 0U) || (stage_number > TRACE_LAST_STAGE) ||
        (label == NULL)) {
        return;
    }
    serial_stage(stage_number, mark, label);
    if ((fb == NULL) || !framebuffer_writes_proven) {
        return;
    }

    framebuffer_write_active = true;
    (void)boring_graphics_fill_rect(
        fb, 4ULL, y - 1ULL, trace_panel_width(), 11ULL,
        trace_color(12U, 12U, 16U));
    packed = mark == '>' ? trace_color(255U, 190U, 64U) :
             mark == '+' ? trace_color(96U, 255U, 128U) :
                           trace_color(255U, 80U, 80U);
    x = 8ULL;
    trace_character(x, y, '[', packed, TRACE_SCALE);
    x += 8ULL;
    if (stage_number < 10U) {
        trace_character(x, y, '0', packed, TRACE_SCALE);
        x += 8ULL;
    }
    x = trace_decimal(x, y, (uint64_t)stage_number,
                      packed, TRACE_SCALE);
    trace_character(x, y, mark, packed, TRACE_SCALE);
    x += 8ULL;
    trace_character(x, y, ']', packed, TRACE_SCALE);
    x += 16ULL;
    trace_text(x, y, label, packed, TRACE_SCALE);
    framebuffer_write_active = false;
}

static void serial_surface(uint64_t index,
                           const struct boring_framebuffer *surface,
                           bool selected) {
    if ((!serial_ready) || (surface == NULL)) {
        return;
    }
    serial_write_string("M61 FRAMEBUFFER CANDIDATE index=");
    serial_write_u64(index);
    serial_write_string(" selected=");
    serial_write_string(selected ? "yes" : "no");
    serial_write_string(" width=");
    serial_write_u64(surface->width);
    serial_write_string(" height=");
    serial_write_u64(surface->height);
    serial_write_string(" pitch=");
    serial_write_u64(surface->pitch);
    serial_write_string(" bpp=");
    serial_write_u64((uint64_t)surface->bpp);
    serial_write_string(" model=");
    serial_write_u64((uint64_t)surface->memory_model);
    serial_write_string(" masks=");
    serial_write_u64((uint64_t)surface->red_mask_size);
    serial_write_string("/");
    serial_write_u64((uint64_t)surface->red_mask_shift);
    serial_write_string(",");
    serial_write_u64((uint64_t)surface->green_mask_size);
    serial_write_string("/");
    serial_write_u64((uint64_t)surface->green_mask_shift);
    serial_write_string(",");
    serial_write_u64((uint64_t)surface->blue_mask_size);
    serial_write_string("/");
    serial_write_u64((uint64_t)surface->blue_mask_shift);
    serial_write_string("\n");
}

static bool same_surface(const struct boring_framebuffer *first,
                         const struct boring_framebuffer *second) {
    if ((first == NULL) || (second == NULL)) {
        return false;
    }
    return (first->address == second->address) &&
           (first->width == second->width) &&
           (first->height == second->height) &&
           (first->pitch == second->pitch) &&
           (first->bpp == second->bpp) &&
           (first->memory_model == second->memory_model);
}

static bool tiny_surface_probe(const struct boring_framebuffer *surface) {
    bool wrote;

    framebuffer_write_active = true;
    wrote = boring_graphics_fill_rect(
        surface, 0ULL, 0ULL, 2ULL, 2ULL,
        boring_color_pack(surface, 255U, 255U, 255U));
    framebuffer_write_active = false;
    return wrote;
}

static bool witness_surface(const struct boring_framebuffer *surface) {
    static const uint8_t colors[TRACE_WITNESS_STRIPES][3] = {
        {255U, 0U, 0U},
        {0U, 255U, 0U},
        {0U, 0U, 255U},
        {0U, 255U, 255U},
        {255U, 0U, 255U},
        {255U, 255U, 0U},
        {255U, 255U, 255U},
        {0U, 0U, 0U}
    };
    uint32_t stripe;
    bool wrote = true;

    if ((surface == NULL) ||
        (surface->width < TRACE_WITNESS_X + TRACE_WITNESS_WIDTH) ||
        (surface->height < TRACE_WITNESS_Y + TRACE_WITNESS_HEIGHT)) {
        return false;
    }

    framebuffer_write_active = true;
    for (stripe = 0U; stripe < TRACE_WITNESS_STRIPES; ++stripe) {
        wrote = boring_graphics_fill_rect(
                    surface,
                    TRACE_WITNESS_X + (uint64_t)(stripe * 8U),
                    TRACE_WITNESS_Y,
                    8ULL,
                    TRACE_WITNESS_HEIGHT,
                    boring_color_pack(surface,
                                      colors[stripe][0],
                                      colors[stripe][1],
                                      colors[stripe][2])) && wrote;
    }
    framebuffer_write_active = false;
    return wrote;
}

static uint64_t read_tsc(void) {
    uint32_t low;
    uint32_t high;

    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32U) | (uint64_t)low;
}

static void hold_trace_for_qmp(void) {
    const uint64_t start = read_tsc();

    while ((read_tsc() - start) < TRACE_TSC_HOLD_CYCLES) {
        x86_64_pause();
    }
}

static void acquire_framebuffers(void) {
    const enum boring_framebuffer_status status =
        boring_framebuffer_boot_init();
    const uint64_t count = boring_m61_framebuffer_count();
    uint64_t selected_index = UINT64_MAX;
    uint64_t index;
    bool selected_witness = false;

    serial_write_string("M61 FRAMEBUFFER COUNT=");
    serial_write_u64(count);
    serial_write_string("\n");
    if (status != BORING_FRAMEBUFFER_STATUS_READY) {
        serial_write_string("M61 FRAMEBUFFER TRACE UNAVAILABLE status=");
        serial_write_u64((uint64_t)status);
        serial_write_string("\n");
        return;
    }
    fb = boring_framebuffer_get();
    if (!boring_framebuffer_surface_valid(fb)) {
        fb = NULL;
        serial_write_string("M61 FRAMEBUFFER TRACE INVALID SELECTED SURFACE\n");
        return;
    }

    for (index = 0ULL; index < count; ++index) {
        struct boring_framebuffer candidate;
        bool selected;
        bool witness;

        if (!boring_m61_framebuffer_get(index, &candidate)) {
            serial_write_string("M61 FRAMEBUFFER CANDIDATE index=");
            serial_write_u64(index);
            serial_write_string(" valid=no\n");
            continue;
        }
        selected = (selected_index == UINT64_MAX) &&
                   same_surface(&candidate, fb);
        if (selected) {
            selected_index = index;
        }
        serial_surface(index, &candidate, selected);
        if (!tiny_surface_probe(&candidate)) {
            serial_write_string("M61 FRAMEBUFFER TINY PROBE FAILED index=");
            serial_write_u64(index);
            serial_write_string("\n");
            continue;
        }
        witness = witness_surface(&candidate);
        serial_write_string("M61 FRAMEBUFFER WITNESS index=");
        serial_write_u64(index);
        serial_write_string(" selected=");
        serial_write_string(selected ? "yes" : "no");
        serial_write_string(" x=4 y=4 width=64 height=16 result=");
        serial_write_string(witness ? "pass\n" : "fail\n");
        if (selected) {
            selected_witness = witness;
        }
    }

    if (!selected_witness) {
        serial_write_string("M61 FRAMEBUFFER TRACE SELECTED WITNESS FAILED\n");
        return;
    }

    framebuffer_writes_proven = true;
    framebuffer_write_active = true;
    (void)boring_graphics_fill_rect(
        fb, 76ULL, 2ULL,
        fb->width > 80ULL ?
            ((fb->width - 80ULL < 320ULL) ? fb->width - 80ULL : 320ULL) :
            0ULL,
        34ULL, trace_color(12U, 12U, 16U));
    trace_text(TRACE_TEXT_X, 8ULL, "BORINGOS PHYSICAL BOOT TRACE",
               trace_color(96U, 224U, 255U), TRACE_SCALE);
    trace_text(TRACE_TEXT_X, 22ULL, "FB", trace_color(180U, 180U, 190U), 1U);
    (void)trace_decimal(TRACE_TEXT_X + 12ULL, 22ULL, fb->width,
                        trace_color(180U, 180U, 190U), 1U);
    framebuffer_write_active = false;

    serial_write_string("M61 FRAMEBUFFER TRACE READY index=");
    serial_write_u64(selected_index);
    serial_write_string(" x=4 y=4 width=64 height=16\n");
    hold_trace_for_qmp();
}

static bool ends_with(const char *value, const char *ending) {
    size_t value_length = 0U;
    size_t ending_length = 0U;
    size_t index;

    if ((value == NULL) || (ending == NULL)) {
        return false;
    }
    while ((value[value_length] != '\0') && (value_length < 96U)) {
        ++value_length;
    }
    while ((ending[ending_length] != '\0') && (ending_length < 96U)) {
        ++ending_length;
    }
    if ((value_length >= 96U) || (ending_length >= 96U) ||
        (value_length < ending_length)) {
        return false;
    }
    for (index = 0U; index < ending_length; ++index) {
        if (value[value_length - ending_length + index] != ending[index]) {
            return false;
        }
    }
    return true;
}

static bool same_bytes(const char *value, size_t value_length,
                       const char *expected, size_t expected_length) {
    size_t index;

    if ((value == NULL) || (expected == NULL) ||
        (value_length != expected_length)) {
        return false;
    }
    for (index = 0U; index < value_length; ++index) {
        if (value[index] != expected[index]) {
            return false;
        }
    }
    return true;
}

static void present_guard(uint32_t stage_number, const char *label) {
    trace_stage(stage_number, '>', label);
    if ((fb != NULL) && framebuffer_writes_proven &&
        (fb->height >= 14ULL)) {
        framebuffer_write_active = true;
        trace_text(8ULL, fb->height - 12ULL, "PRESENT IN PROGRESS",
                   trace_color(255U, 190U, 64U), TRACE_SCALE);
        framebuffer_write_active = false;
    }
}

static void fatal_screen(const struct x86_64_trap_frame *frame) {
    uint32_t packed;
    uint64_t panel_width;

    if ((fb == NULL) || !framebuffer_writes_proven ||
        framebuffer_write_active) {
        return;
    }
    panel_width = fb->width < 480ULL ? fb->width : 480ULL;
    framebuffer_write_active = true;
    (void)boring_graphics_fill_rect(
        fb, 0ULL, 0ULL, panel_width,
        fb->height < 96ULL ? fb->height : 96ULL,
        trace_color(96U, 0U, 0U));
    packed = trace_color(255U, 240U, 240U);
    trace_text(8ULL, 8ULL, "FATAL", packed, TRACE_SCALE);
    if (frame == NULL) {
        trace_text(8ULL, 32ULL, "TRAP FRAME UNAVAILABLE",
                   packed, TRACE_SCALE);
    } else {
        trace_text(8ULL, 32ULL, "VECTOR=", packed, TRACE_SCALE);
        (void)trace_decimal(64ULL, 32ULL, frame->vector,
                            packed, TRACE_SCALE);
        trace_text(8ULL, 46ULL, "ERROR=", packed, TRACE_SCALE);
        trace_hex64(56ULL, 46ULL, frame->error_code,
                    packed, TRACE_SCALE);
        trace_text(8ULL, 60ULL, "RIP=", packed, TRACE_SCALE);
        trace_hex64(40ULL, 60ULL, frame->rip,
                    packed, TRACE_SCALE);
    }
    framebuffer_write_active = false;
}

void __real_serial_init(void);
void __wrap_serial_init(void);
void __real_boring_cpu_inventory_init(void);
void __wrap_boring_cpu_inventory_init(void);
void __real_boring_pci_inventory_init(void);
void __wrap_boring_pci_inventory_init(void);
void __real_boring_smbios_boot_init(
    const struct boring_limine_hhdm_response *,
    const struct boring_limine_memmap_response *);
void __wrap_boring_smbios_boot_init(
    const struct boring_limine_hhdm_response *,
    const struct boring_limine_memmap_response *);
bool __real_pmm_init(const struct boring_limine_memmap_response *);
bool __wrap_pmm_init(const struct boring_limine_memmap_response *);
bool __real_vmm_init(const struct boring_limine_hhdm_response *,
                     const struct boring_limine_paging_mode_response *,
                     const struct boring_limine_memmap_response *);
bool __wrap_vmm_init(const struct boring_limine_hhdm_response *,
                     const struct boring_limine_paging_mode_response *,
                     const struct boring_limine_memmap_response *);
bool __real_heap_init(void);
bool __wrap_heap_init(void);
bool __real_exception_init(void);
bool __wrap_exception_init(void);
void __real_syscall_test_run(void);
void __wrap_syscall_test_run(void);
bool __real_boring_input_init(void);
bool __wrap_boring_input_init(void);
bool __real_irq_init(void);
bool __wrap_irq_init(void);
bool __real_timer_init(uint32_t);
bool __wrap_timer_init(uint32_t);
bool __real_xhci_init(struct xhci_state *);
bool __wrap_xhci_init(struct xhci_state *);
bool __real_xhci_address_connected(struct xhci_state *);
bool __wrap_xhci_address_connected(struct xhci_state *);
bool __real_xhci_discover_descriptors(struct xhci_state *);
bool __wrap_xhci_discover_descriptors(struct xhci_state *);
bool __real_xhci_configure_hid_devices_mixed(struct xhci_state *);
bool __wrap_xhci_configure_hid_devices_mixed(struct xhci_state *);
bool __real_usb_mass_storage_init(struct xhci_state *);
bool __wrap_usb_mass_storage_init(struct xhci_state *);
enum vfs_result __real_boringfs_vfs_create_writable(
    const struct block_device *, uint64_t, struct boringfs_vfs **,
    struct boringfs_validation_error *);
enum vfs_result __wrap_boringfs_vfs_create_writable(
    const struct block_device *, uint64_t, struct boringfs_vfs **,
    struct boringfs_validation_error *);
bool __real_process_set_name(struct process *, const char *);
bool __wrap_process_set_name(struct process *, const char *);
enum boring_framebuffer_user_result __real_boring_framebuffer_user_claim(
    uint64_t, struct boring_display_scanout_info *);
enum boring_framebuffer_user_result __wrap_boring_framebuffer_user_claim(
    uint64_t, struct boring_display_scanout_info *);
enum boring_framebuffer_user_result __real_boring_framebuffer_user_present(
    struct process *, uint32_t);
enum boring_framebuffer_user_result __wrap_boring_framebuffer_user_present(
    struct process *, uint32_t);
enum boring_ipc_result __real_boring_ipc_service_register(
    struct process *, const char *, size_t, uint32_t *);
enum boring_ipc_result __wrap_boring_ipc_service_register(
    struct process *, const char *, size_t, uint32_t *);
void __real_x86_64_exception_dispatch(const struct x86_64_trap_frame *);
void __wrap_x86_64_exception_dispatch(const struct x86_64_trap_frame *);

void __wrap_serial_init(void) {
    if (!early_containment_install()) {
        emergency_halt();
    }
    __real_serial_init();
    serial_ready = true;
    serial_write_string("M61 EARLY EXCEPTION CONTAINMENT ARMED IST1\n");
#ifdef BORING_M61_EARLY_FAULT_TEST
    serial_write_string("M61 CONTROLLED EARLY FAULT ARMED vector=6\n");
    __asm__ volatile("ud2");
    emergency_halt();
#endif
    acquire_framebuffers();
    trace_stage(1U, '>', "SERIAL PROBE");
    trace_stage(1U, '+', "SERIAL PROBE");
}

void __wrap_boring_cpu_inventory_init(void) {
    trace_stage(2U, '>', "CPU INVENTORY");
    __real_boring_cpu_inventory_init();
    trace_stage(2U, '+', "CPU INVENTORY");
}

void __wrap_boring_pci_inventory_init(void) {
    trace_stage(3U, '>', "PCI INVENTORY");
    __real_boring_pci_inventory_init();
    trace_stage(3U, '+', "PCI INVENTORY");
}

void __wrap_boring_smbios_boot_init(
    const struct boring_limine_hhdm_response *hhdm,
    const struct boring_limine_memmap_response *memmap) {
    trace_stage(4U, '>', "SMBIOS");
    __real_boring_smbios_boot_init(hhdm, memmap);
    trace_stage(4U, '+', "SMBIOS");
}

bool __wrap_pmm_init(const struct boring_limine_memmap_response *memmap) {
    bool result;

    trace_stage(5U, '>', "PMM INIT");
    result = __real_pmm_init(memmap);
    trace_stage(5U, result ? '+' : '!', "PMM INIT");
    return result;
}

bool __wrap_vmm_init(
    const struct boring_limine_hhdm_response *hhdm,
    const struct boring_limine_paging_mode_response *paging,
    const struct boring_limine_memmap_response *memmap) {
    bool result;

    trace_stage(6U, '>', "VMM INIT");
    result = __real_vmm_init(hhdm, paging, memmap);
    trace_stage(6U, result ? '+' : '!', "VMM INIT");
    return result;
}

bool __wrap_heap_init(void) {
    bool result;

    trace_stage(7U, '>', "HEAP INIT");
    result = __real_heap_init();
    trace_stage(7U, result ? '+' : '!', "HEAP INIT");
    return result;
}

bool __wrap_exception_init(void) {
    bool result;

    trace_stage(8U, '>', "EXCEPTIONS");
    result = __real_exception_init();
    if (result) {
        early_containment_active = false;
    }
    trace_stage(8U, result ? '+' : '!', "EXCEPTIONS");
    return result;
}

void __wrap_syscall_test_run(void) {
    trace_stage(9U, '>', "DESKTOP HARNESS");
    __real_syscall_test_run();
    trace_stage(9U, '+', "DESKTOP HARNESS");
}

bool __wrap_boring_input_init(void) {
    bool result;

    trace_stage(10U, '>', "INPUT CORE");
    result = __real_boring_input_init();
    trace_stage(10U, result ? '+' : '!', "INPUT CORE");
    return result;
}

bool __wrap_irq_init(void) {
    bool result;

    trace_stage(11U, '>', "IRQ INIT");
    result = __real_irq_init();
    trace_stage(11U, result ? '+' : '!', "IRQ INIT");
    return result;
}

bool __wrap_timer_init(uint32_t frequency_hz) {
    bool result;

    trace_stage(12U, '>', "PIT TIMER");
    result = __real_timer_init(frequency_hz);
    trace_stage(12U, result ? '+' : '!', "PIT TIMER");
    return result;
}

bool __wrap_xhci_init(struct xhci_state *state) {
    bool result;

    trace_stage(13U, '>', "XHCI CONTROLLER");
    result = __real_xhci_init(state);
    trace_stage(13U, result ? '+' : '!', "XHCI CONTROLLER");
    return result;
}

bool __wrap_xhci_address_connected(struct xhci_state *state) {
    bool result;

    trace_stage(14U, '>', "XHCI ADDRESS");
    result = __real_xhci_address_connected(state);
    trace_stage(14U, result ? '+' : '!', "XHCI ADDRESS");
    return result;
}

bool __wrap_xhci_discover_descriptors(struct xhci_state *state) {
    bool result;

    trace_stage(15U, '>', "XHCI DESCRIPTORS");
    result = __real_xhci_discover_descriptors(state);
    trace_stage(15U, result ? '+' : '!', "XHCI DESCRIPTORS");
    return result;
}

bool __wrap_xhci_configure_hid_devices_mixed(struct xhci_state *state) {
    bool result;

    trace_stage(16U, '>', "XHCI HID CONFIG");
    result = __real_xhci_configure_hid_devices_mixed(state);
    trace_stage(16U, result ? '+' : '!', "XHCI HID CONFIG");
    return result;
}

bool __wrap_usb_mass_storage_init(struct xhci_state *state) {
    bool result;

    trace_stage(17U, '>', "USB MASS STORAGE");
    result = __real_usb_mass_storage_init(state);
    trace_stage(17U, result ? '+' : '!', "USB MASS STORAGE");
    return result;
}

enum vfs_result __wrap_boringfs_vfs_create_writable(
    const struct block_device *device, uint64_t id,
    struct boringfs_vfs **out,
    struct boringfs_validation_error *error) {
    enum vfs_result result;

    trace_stage(18U, '>', "BORINGFS ROOT");
    result = __real_boringfs_vfs_create_writable(
        device, id, out, error);
    trace_stage(18U, result == VFS_RESULT_OK ? '+' : '!', "BORINGFS ROOT");
    return result;
}

bool __wrap_process_set_name(struct process *process, const char *name) {
    uint32_t stage_number = 0U;
    const char *label = NULL;
    bool result;

    if (ends_with(name, "boring-init")) {
        stage_number = 19U;
        label = "BORING-INIT START";
    } else if (ends_with(name, "boring-display")) {
        stage_number = 20U;
        label = "BORING-DISPLAY START";
    } else if (ends_with(name, "boringwm")) {
        stage_number = 24U;
        label = "BORING-WM START";
    } else if (ends_with(name, "boring-terminal")) {
        stage_number = 27U;
        label = "TERMINAL START";
    }
    if (stage_number != 0U) {
        trace_stage(stage_number, '>', label);
    }
    result = __real_process_set_name(process, name);
    if (stage_number != 0U) {
        trace_stage(stage_number, result ? '+' : '!', label);
    }
    return result;
}

enum boring_framebuffer_user_result __wrap_boring_framebuffer_user_claim(
    uint64_t pid, struct boring_display_scanout_info *info) {
    enum boring_framebuffer_user_result result;

    trace_stage(21U, '>', "DISPLAY FB CLAIM");
    result = __real_boring_framebuffer_user_claim(pid, info);
    trace_stage(21U,
                result == BORING_FRAMEBUFFER_USER_OK ? '+' : '!',
                "DISPLAY FB CLAIM");
    return result;
}

enum boring_ipc_result __wrap_boring_ipc_service_register(
    struct process *process, const char *name, size_t length,
    uint32_t *out) {
    uint32_t stage_number = 0U;
    const char *label = NULL;
    enum boring_ipc_result result;

    if (same_bytes(name, length, "boring.display", 14U)) {
        stage_number = 22U;
        label = "DISPLAY SERVICE";
    } else if (same_bytes(name, length, "boring.wm", 9U)) {
        stage_number = 25U;
        label = "WM SERVICE";
    }
    if (stage_number != 0U) {
        trace_stage(stage_number, '>', label);
    }
    result = __real_boring_ipc_service_register(
        process, name, length, out);
    if (stage_number != 0U) {
        trace_stage(stage_number,
                    result == BORING_IPC_RESULT_OK ? '+' : '!',
                    label);
    }
    if ((stage_number == 25U) && (result == BORING_IPC_RESULT_OK)) {
        wm_ready = true;
    }
    return result;
}

enum boring_framebuffer_user_result __wrap_boring_framebuffer_user_present(
    struct process *process, uint32_t handle) {
    enum boring_framebuffer_user_result result;
    uint32_t stage_number;
    const char *label;

    if (desktop_presented) {
        return __real_boring_framebuffer_user_present(process, handle);
    }
    stage_number = wm_ready ? 26U : 23U;
    label = wm_ready ? "DESKTOP PRESENT" : "DISPLAY INITIAL PRESENT";
    present_guard(stage_number, label);
    result = __real_boring_framebuffer_user_present(process, handle);
    if (result != BORING_FRAMEBUFFER_USER_OK) {
        trace_stage(stage_number, '!', label);
        return result;
    }
    if (framebuffer_writes_proven && !witness_surface(fb)) {
        if (serial_ready) {
            serial_write_string(
                "M61 RETAINED FRAMEBUFFER WITNESS FAILED\n");
        }
        emergency_halt();
    }
    if (wm_ready) {
        desktop_presented = true;
    }
    /* Keep the final diagnostic line visible over an otherwise black idle WM. */
    trace_stage(stage_number, '+', label);
    return result;
}

void __wrap_x86_64_exception_dispatch(
    const struct x86_64_trap_frame *frame) {
    if (early_containment_active || framebuffer_write_active) {
        if (serial_ready) {
            serial_write_string(
                early_containment_active ?
                    "M61 EARLY FAULT CONTAINED vector=" :
                    "M61 FRAMEBUFFER WRITE FAULT CONTAINED vector=");
            serial_write_u64(frame == NULL ? UINT64_MAX : frame->vector);
            serial_write_string(" framebuffer_write_active=");
            serial_write_string(framebuffer_write_active ? "yes\n" : "no\n");
        }
        emergency_halt();
    }
    fatal_screen(frame);
    __real_x86_64_exception_dispatch(frame);
}
