#include <stdbool.h>
#include <stdint.h>

#include <boring/boot_protocol.h>
#include <boring/cpu_inventory.h>
#include <boring/exception.h>
#include <boring/framebuffer.h>
#include <boring/graphics.h>
#include <boring/heap.h>
#include <boring/pci_inventory.h>
#include <boring/pmm.h>
#include <boring/serial.h>
#include <boring/smbios.h>
#include <boring/syscall_test.h>
#include <boring/vmm.h>

#ifndef BORING_M61_PHYSICAL_BREADCRUMBS
#error "M61 physical breadcrumbs must stay candidate-build gated"
#endif

struct breadcrumb_rgb {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

const char boring_m61_physical_breadcrumbs_enabled[] =
    "M61 physical framebuffer breadcrumbs enabled";

static const struct boring_framebuffer *breadcrumb_surface;

void __real_serial_init(void);
void __wrap_serial_init(void);
void __real_boring_cpu_inventory_init(void);
void __wrap_boring_cpu_inventory_init(void);
void __real_boring_pci_inventory_init(void);
void __wrap_boring_pci_inventory_init(void);
void __real_boring_smbios_boot_init(
    const struct boring_limine_hhdm_response *hhdm,
    const struct boring_limine_memmap_response *memory_map);
void __wrap_boring_smbios_boot_init(
    const struct boring_limine_hhdm_response *hhdm,
    const struct boring_limine_memmap_response *memory_map);
bool __real_pmm_init(const struct boring_limine_memmap_response *memory_map);
bool __wrap_pmm_init(const struct boring_limine_memmap_response *memory_map);
bool __real_vmm_init(
    const struct boring_limine_hhdm_response *hhdm,
    const struct boring_limine_paging_mode_response *paging_mode,
    const struct boring_limine_memmap_response *memory_map);
bool __wrap_vmm_init(
    const struct boring_limine_hhdm_response *hhdm,
    const struct boring_limine_paging_mode_response *paging_mode,
    const struct boring_limine_memmap_response *memory_map);
bool __real_heap_init(void);
bool __wrap_heap_init(void);
bool __real_exception_init(void);
bool __wrap_exception_init(void);
void __real_syscall_test_run(void);
void __wrap_syscall_test_run(void);

static struct breadcrumb_rgb breadcrumb_color(uint64_t checkpoint) {
    switch (checkpoint) {
    case 1ULL:
        return (struct breadcrumb_rgb){255U, 32U, 32U};
    case 2ULL:
        return (struct breadcrumb_rgb){255U, 128U, 0U};
    case 3ULL:
        return (struct breadcrumb_rgb){255U, 255U, 0U};
    case 4ULL:
        return (struct breadcrumb_rgb){0U, 255U, 255U};
    case 5ULL:
        return (struct breadcrumb_rgb){64U, 96U, 255U};
    case 6ULL:
        return (struct breadcrumb_rgb){255U, 0U, 255U};
    case 7ULL:
        return (struct breadcrumb_rgb){255U, 255U, 255U};
    case 8ULL:
        return (struct breadcrumb_rgb){0U, 255U, 0U};
    case 9ULL:
        return (struct breadcrumb_rgb){255U, 64U, 160U};
    case 10ULL:
        return (struct breadcrumb_rgb){160U, 80U, 255U};
    default:
        return (struct breadcrumb_rgb){0U, 0U, 0U};
    }
}

static void breadcrumb_mark(uint64_t checkpoint) {
    const struct breadcrumb_rgb rgb = breadcrumb_color(checkpoint);
    uint32_t packed;
    uint64_t x;

    if ((breadcrumb_surface == 0) || (checkpoint == 0ULL) ||
        (checkpoint > 10ULL)) {
        return;
    }

    packed = boring_color_pack(breadcrumb_surface, rgb.red, rgb.green, rgb.blue);
    x = 8ULL + ((checkpoint - 1ULL) * 28ULL);
    (void)boring_graphics_fill_rect(
        breadcrumb_surface, x, 8ULL, 24ULL, 24ULL, packed);
}

static void breadcrumb_acquire(void) {
    enum boring_framebuffer_status status;
    uint32_t background;

    status = boring_framebuffer_boot_init();
    if (status != BORING_FRAMEBUFFER_STATUS_READY) {
        return;
    }

    breadcrumb_surface = boring_framebuffer_get();
    if (breadcrumb_surface == 0) {
        return;
    }

    background = boring_color_pack(breadcrumb_surface, 16U, 16U, 16U);
    (void)boring_graphics_fill_rect(
        breadcrumb_surface, 0ULL, 0ULL, 292ULL, 40ULL, background);
    breadcrumb_mark(1ULL);
}

void __wrap_serial_init(void) {
    breadcrumb_acquire();
    __real_serial_init();
    breadcrumb_mark(2ULL);
}

void __wrap_boring_cpu_inventory_init(void) {
    __real_boring_cpu_inventory_init();
    breadcrumb_mark(3ULL);
}

void __wrap_boring_pci_inventory_init(void) {
    __real_boring_pci_inventory_init();
    breadcrumb_mark(4ULL);
}

void __wrap_boring_smbios_boot_init(
    const struct boring_limine_hhdm_response *hhdm,
    const struct boring_limine_memmap_response *memory_map) {
    __real_boring_smbios_boot_init(hhdm, memory_map);
    breadcrumb_mark(5ULL);
}

bool __wrap_pmm_init(const struct boring_limine_memmap_response *memory_map) {
    const bool ready = __real_pmm_init(memory_map);
    if (ready) {
        breadcrumb_mark(6ULL);
    }
    return ready;
}

bool __wrap_vmm_init(
    const struct boring_limine_hhdm_response *hhdm,
    const struct boring_limine_paging_mode_response *paging_mode,
    const struct boring_limine_memmap_response *memory_map) {
    const bool ready = __real_vmm_init(hhdm, paging_mode, memory_map);
    if (ready) {
        breadcrumb_mark(7ULL);
    }
    return ready;
}

bool __wrap_heap_init(void) {
    const bool ready = __real_heap_init();
    if (ready) {
        breadcrumb_mark(8ULL);
    }
    return ready;
}

bool __wrap_exception_init(void) {
    const bool ready = __real_exception_init();
    if (ready) {
        breadcrumb_mark(9ULL);
    }
    return ready;
}

void __wrap_syscall_test_run(void) {
    breadcrumb_mark(10ULL);
    __real_syscall_test_run();
}
