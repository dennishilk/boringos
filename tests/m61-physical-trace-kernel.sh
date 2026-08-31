#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

# Re-link only the M61 physical kernel after the established runtime/root build.
# This keeps the diagnostic surface narrow and avoids rebuilding unrelated CI.
#
# The POST witness is intentionally generated only inside this M61 diagnostic
# build. It does not add a normal BoringOS POST subsystem or change non-M61
# runtime behavior.
POST_SOURCE=kernel/core/m61_post80_generated.c
cat > "$POST_SOURCE" <<'EOF_POST_C'
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Undo M61 build-time redirections for this hook implementation itself. */
#ifdef boring_kernel_entry
#undef boring_kernel_entry
#endif
#ifdef __real_serial_init
#undef __real_serial_init
#endif
#ifdef __real_boring_cpu_inventory_init
#undef __real_boring_cpu_inventory_init
#endif
#ifdef __real_boring_pci_inventory_init
#undef __real_boring_pci_inventory_init
#endif
#ifdef __real_boring_smbios_boot_init
#undef __real_boring_smbios_boot_init
#endif
#ifdef __real_pmm_init
#undef __real_pmm_init
#endif
#ifdef __real_vmm_init
#undef __real_vmm_init
#endif
#ifdef __real_heap_init
#undef __real_heap_init
#endif
#ifdef __real_irq_init
#undef __real_irq_init
#endif
#ifdef __real_usb_mass_storage_init
#undef __real_usb_mass_storage_init
#endif
#ifdef __real_boringfs_vfs_create_writable
#undef __real_boringfs_vfs_create_writable
#endif
#ifdef __real_process_set_name
#undef __real_process_set_name
#endif
#ifdef __real_boring_framebuffer_user_present
#undef __real_boring_framebuffer_user_present
#endif

#include <boring/boot_protocol.h>
#include <boring/boringfs_vfs.h>
#include <boring/framebuffer.h>
#include <boring/framebuffer_user.h>
#include <boring/graphics.h>
#include <boring/heap.h>
#include <boring/io.h>
#include <boring/irq.h>
#include <boring/process.h>
#include <boring/serial.h>
#include <boring/usb_mass_storage.h>

#ifndef BORING_M61_PHYSICAL_BREADCRUMBS
#error "M61 POST-port witness must stay candidate-build gated"
#endif

#define M61_POST_PORT 0x80U
#define M61_NAME_LIMIT 96U
#define M61_POST(code) \
    x86_64_out8((uint16_t)M61_POST_PORT, (uint8_t)(code))

enum m61_post_code {
    M61_POST_KERNEL_ENTRY = 0x61,
    M61_POST_EARLY_CONTAINMENT_SERIAL = 0x62,
    M61_POST_MEMORY_RUNTIME = 0x63,
    M61_POST_EXCEPTION_IRQ = 0x64,
    M61_POST_USB_STORAGE = 0x65,
    M61_POST_USB_ROOT = 0x66,
    M61_POST_BORING_INIT = 0x67,
    M61_POST_BORING_DISPLAY = 0x68,
    M61_POST_BORING_WM = 0x69,
    M61_POST_AUTO_TERMINAL = 0x6a,
    M61_POST_DESKTOP_PRESENT = 0x6f,

    M61_POST_FB_PROBE_BOOT_INIT_BEFORE = 0x70,
    M61_POST_FB_PROBE_BOOT_INIT_AFTER = 0x71,
    M61_POST_CPU_INVENTORY_BEFORE = 0x72,
    M61_POST_CPU_INVENTORY_AFTER = 0x73,
    M61_POST_PCI_INVENTORY_BEFORE = 0x74,
    M61_POST_PCI_INVENTORY_AFTER = 0x75,
    M61_POST_SMBIOS_BEFORE = 0x76,
    M61_POST_SMBIOS_AFTER = 0x77,
    M61_POST_ENTRY_FB_BOOT_INIT_BEFORE = 0x78,
    M61_POST_ENTRY_FB_BOOT_INIT_AFTER = 0x79,
    M61_POST_PMM_INIT_BEFORE = 0x7a,
    M61_POST_PMM_INIT_AFTER = 0x7b,
    M61_POST_VMM_INIT_BEFORE = 0x7c,
    M61_POST_VMM_INIT_AFTER = 0x7d,
    M61_POST_HEAP_INIT_BEFORE = 0x7e,
    M61_POST_HEAP_INIT_AFTER = 0x7f,

    M61_POST_ACQUIRE_RESUMED = 0x80,
    M61_POST_FB_COUNT_RETURNED = 0x81,
    M61_POST_READY_SELECTED_GET = 0x82,
    M61_POST_SELECTED_FB_RETURNED = 0x83,
    M61_POST_SELECTED_VALIDATION_ENTERED = 0x84,
    M61_POST_SELECTED_VALIDATION_PASSED = 0x85,
    M61_POST_FIRST_CANDIDATE_OBTAINED = 0x86,
    M61_POST_FIRST_FB_WRITE_BEFORE = 0x87,
    M61_POST_FIRST_FB_WRITE_AFTER = 0x88,
    M61_POST_WITNESS_WRITE_BEFORE = 0x89,
    M61_POST_WITNESS_RETURNED = 0x8a,
    M61_POST_CANDIDATE_LOOP_WITNESS_DONE = 0x8b,
    M61_POST_TRACE_PANEL_WRITE_BEFORE = 0x8c,
    M61_POST_TRACE_PANEL_WRITE_AFTER = 0x8d,
    M61_POST_TRACE_TEXT_RETURNED = 0x8e,
    M61_POST_QMP_HOLD_BEFORE = 0x8f
};

const char boring_m61_post_port80_enabled[] =
    "M61 diagnostic POST port 0x80 witness enabled";
const uint8_t boring_m61_post_sequence[] = {
    0x61U, 0x62U, 0x63U, 0x64U, 0x65U, 0x66U,
    0x67U, 0x68U, 0x69U, 0x6aU, 0x6fU
};
const uint8_t boring_m61_post_62_to_63_sequence[] = {
    0x70U, 0x71U, 0x72U, 0x73U, 0x74U, 0x75U, 0x76U, 0x77U,
    0x78U, 0x79U, 0x7aU, 0x7bU, 0x7cU, 0x7dU, 0x7eU, 0x7fU
};
const uint8_t boring_m61_post_71_to_72_sequence[] = {
    0x80U, 0x81U, 0x82U, 0x83U, 0x84U, 0x85U, 0x86U, 0x87U,
    0x88U, 0x89U, 0x8aU, 0x8bU, 0x8cU, 0x8dU, 0x8eU, 0x8fU
};

void boring_kernel_entry(void);
void m61_post_real_boring_kernel_entry(void);
void __real_serial_init(void);
void __real_boring_cpu_inventory_init(void);
void __real_boring_pci_inventory_init(void);
void __real_boring_smbios_boot_init(
    const struct boring_limine_hhdm_response *,
    const struct boring_limine_memmap_response *);
enum boring_framebuffer_status __real_boring_framebuffer_boot_init(void);
uint64_t __real_boring_m61_framebuffer_count(void);
const struct boring_framebuffer *__real_boring_framebuffer_get(void);
bool __real_boring_framebuffer_surface_valid(
    const struct boring_framebuffer *surface);
bool __real_boring_m61_framebuffer_get(
    uint64_t index, struct boring_framebuffer *surface);
bool __real_boring_graphics_fill_rect(
    const struct boring_framebuffer *surface,
    uint64_t x, uint64_t y, uint64_t width, uint64_t height,
    uint32_t color);
void __real_serial_write_string(const char *value);
bool __real_pmm_init(const struct boring_limine_memmap_response *);
bool __real_vmm_init(const struct boring_limine_hhdm_response *,
                     const struct boring_limine_paging_mode_response *,
                     const struct boring_limine_memmap_response *);
bool __real_heap_init(void);
bool __real_irq_init(void);
bool __real_usb_mass_storage_init(struct xhci_state *state);
enum vfs_result __real_boringfs_vfs_create_writable(
    const struct block_device *device, uint64_t id,
    struct boringfs_vfs **out, struct boringfs_validation_error *error);
bool __real_process_set_name(struct process *process, const char *name);
enum boring_framebuffer_user_result __real_boring_framebuffer_user_present(
    struct process *process, uint32_t handle);

void m61_post_serial_init(void);
void m61_post_boring_cpu_inventory_init(void);
void m61_post_boring_pci_inventory_init(void);
void m61_post_boring_smbios_boot_init(
    const struct boring_limine_hhdm_response *,
    const struct boring_limine_memmap_response *);
enum boring_framebuffer_status __wrap_boring_framebuffer_boot_init(void);
uint64_t __wrap_boring_m61_framebuffer_count(void);
const struct boring_framebuffer *__wrap_boring_framebuffer_get(void);
bool __wrap_boring_framebuffer_surface_valid(
    const struct boring_framebuffer *surface);
bool __wrap_boring_m61_framebuffer_get(
    uint64_t index, struct boring_framebuffer *surface);
bool __wrap_boring_graphics_fill_rect(
    const struct boring_framebuffer *surface,
    uint64_t x, uint64_t y, uint64_t width, uint64_t height,
    uint32_t color);
void __wrap_serial_write_string(const char *value);
bool m61_post_pmm_init(const struct boring_limine_memmap_response *);
bool m61_post_vmm_init(const struct boring_limine_hhdm_response *,
                       const struct boring_limine_paging_mode_response *,
                       const struct boring_limine_memmap_response *);
bool m61_post_heap_init(void);
bool m61_post_irq_init(void);
bool m61_post_usb_mass_storage_init(struct xhci_state *state);
enum vfs_result m61_post_boringfs_vfs_create_writable(
    const struct block_device *device, uint64_t id,
    struct boringfs_vfs **out, struct boringfs_validation_error *error);
bool m61_post_process_set_name(struct process *process, const char *name);
enum boring_framebuffer_user_result m61_post_boring_framebuffer_user_present(
    struct process *process, uint32_t handle);

static uint8_t framebuffer_boot_init_calls;
static bool fine_acquire_seen;
static bool fine_acquire_active;
static bool fine_first_candidate_obtained;
static bool fine_first_write_seen;
static bool fine_witness_entered;
static bool fine_witness_writes_complete;
static bool fine_witness_returned;
static bool fine_trace_panel_returned;
static bool fine_trace_text_returned;
static bool init_posted;
static bool display_posted;
static bool wm_posted;
static bool terminal_posted;
static bool desktop_posted;

static bool same_string(const char *value, const char *expected) {
    size_t index = 0U;

    if ((value == NULL) || (expected == NULL)) {
        return false;
    }
    while (index < (size_t)M61_NAME_LIMIT) {
        if (value[index] != expected[index]) {
            return false;
        }
        if (value[index] == '\0') {
            return true;
        }
        ++index;
    }
    return false;
}

static bool name_ends_with(const char *value, const char *ending) {
    size_t value_length = 0U;
    size_t ending_length = 0U;
    size_t index;

    if ((value == NULL) || (ending == NULL)) {
        return false;
    }
    while ((value[value_length] != '\0') &&
           (value_length < (size_t)M61_NAME_LIMIT)) {
        ++value_length;
    }
    while ((ending[ending_length] != '\0') &&
           (ending_length < (size_t)M61_NAME_LIMIT)) {
        ++ending_length;
    }
    if ((value_length >= (size_t)M61_NAME_LIMIT) ||
        (ending_length >= (size_t)M61_NAME_LIMIT) ||
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

void boring_kernel_entry(void) {
    /* First diagnostic instruction after Limine transfers to kernel entry. */
    M61_POST(M61_POST_KERNEL_ENTRY);
    m61_post_real_boring_kernel_entry();
}

void m61_post_serial_init(void) {
    /* Existing M61 wrapper has installed its bounded early exception IDT. */
    __real_serial_init();
    /* COM1 probing is fail-open and has now returned, before framebuffer I/O. */
    M61_POST(M61_POST_EARLY_CONTAINMENT_SERIAL);
}

enum boring_framebuffer_status __wrap_boring_framebuffer_boot_init(void) {
    const uint8_t call_index = framebuffer_boot_init_calls;
    enum boring_framebuffer_status result;

    if (framebuffer_boot_init_calls != UINT8_MAX) {
        ++framebuffer_boot_init_calls;
    }
    if (call_index == 0U) {
        M61_POST(M61_POST_FB_PROBE_BOOT_INIT_BEFORE);
    } else if (call_index == 1U) {
        M61_POST(M61_POST_ENTRY_FB_BOOT_INIT_BEFORE);
    }

    result = __real_boring_framebuffer_boot_init();

    if (call_index == 0U) {
        M61_POST(M61_POST_FB_PROBE_BOOT_INIT_AFTER);
    } else if (call_index == 1U) {
        M61_POST(M61_POST_ENTRY_FB_BOOT_INIT_AFTER);
    }
    return result;
}

uint64_t __wrap_boring_m61_framebuffer_count(void) {
    uint64_t result;

    if (!fine_acquire_seen) {
        fine_acquire_seen = true;
        fine_acquire_active = true;
        /* First operation after the already-proven POST 71 return. */
        M61_POST(M61_POST_ACQUIRE_RESUMED);
    }
    result = __real_boring_m61_framebuffer_count();
    if (fine_acquire_active) {
        M61_POST(M61_POST_FB_COUNT_RETURNED);
    }
    return result;
}

const struct boring_framebuffer *__wrap_boring_framebuffer_get(void) {
    const struct boring_framebuffer *result;

    if (fine_acquire_active) {
        /* The READY branch was taken; selected surface lookup is next. */
        M61_POST(M61_POST_READY_SELECTED_GET);
    }
    result = __real_boring_framebuffer_get();
    if (fine_acquire_active) {
        M61_POST(M61_POST_SELECTED_FB_RETURNED);
    }
    return result;
}

bool __wrap_boring_framebuffer_surface_valid(
    const struct boring_framebuffer *surface) {
    bool result;

    if (fine_acquire_active) {
        M61_POST(M61_POST_SELECTED_VALIDATION_ENTERED);
    }
    result = __real_boring_framebuffer_surface_valid(surface);
    if (fine_acquire_active && result) {
        M61_POST(M61_POST_SELECTED_VALIDATION_PASSED);
    }
    return result;
}

bool __wrap_boring_m61_framebuffer_get(
    uint64_t index, struct boring_framebuffer *surface) {
    const bool result = __real_boring_m61_framebuffer_get(index, surface);

    if (fine_acquire_active && result && !fine_first_candidate_obtained) {
        fine_first_candidate_obtained = true;
        M61_POST(M61_POST_FIRST_CANDIDATE_OBTAINED);
    }
    return result;
}

bool __wrap_boring_graphics_fill_rect(
    const struct boring_framebuffer *surface,
    uint64_t x, uint64_t y, uint64_t width, uint64_t height,
    uint32_t color) {
    const bool tiny_probe = fine_acquire_active && !fine_first_write_seen &&
                            (x == 0ULL) && (y == 0ULL) &&
                            (width == 2ULL) && (height == 2ULL);
    const bool witness_first = fine_acquire_active && !fine_witness_entered &&
                               (x == 4ULL) && (y == 4ULL) &&
                               (width == 8ULL) && (height == 16ULL);
    const bool witness_last = fine_acquire_active &&
                              (x == 60ULL) && (y == 4ULL) &&
                              (width == 8ULL) && (height == 16ULL);
    const bool trace_panel = fine_acquire_active &&
                             (x == 76ULL) && (y == 2ULL) &&
                             (height == 34ULL);
    bool result;

    if (tiny_probe) {
        fine_first_write_seen = true;
        /* Unmistakable first actual framebuffer-memory write boundary. */
        M61_POST(M61_POST_FIRST_FB_WRITE_BEFORE);
    }
    if (witness_first) {
        fine_witness_entered = true;
        /* Reaching this proves tiny_surface_probe returned true. */
        M61_POST(M61_POST_WITNESS_WRITE_BEFORE);
    }
    if (trace_panel) {
        /* Reaching the panel proves the candidate loop and selected witness. */
        M61_POST(M61_POST_CANDIDATE_LOOP_WITNESS_DONE);
        M61_POST(M61_POST_TRACE_PANEL_WRITE_BEFORE);
    }

    result = __real_boring_graphics_fill_rect(
        surface, x, y, width, height, color);

    if (tiny_probe) {
        M61_POST(M61_POST_FIRST_FB_WRITE_AFTER);
    }
    if (witness_last) {
        fine_witness_writes_complete = true;
    }
    if (trace_panel) {
        fine_trace_panel_returned = true;
        M61_POST(M61_POST_TRACE_PANEL_WRITE_AFTER);
    }
    return result;
}

void __wrap_serial_write_string(const char *value) {
    const bool witness_returned =
        fine_acquire_active && fine_witness_writes_complete &&
        !fine_witness_returned;
    const bool trace_text_returned =
        fine_acquire_active && fine_trace_panel_returned &&
        !fine_trace_text_returned;
    const bool final_trace_line =
        fine_acquire_active && fine_trace_text_returned &&
        same_string(value, " x=4 y=4 width=64 height=16\n");

    if (witness_returned) {
        /* This serial call occurs only after witness_surface() returned. */
        fine_witness_returned = true;
        M61_POST(M61_POST_WITNESS_RETURNED);
    }
    if (trace_text_returned) {
        /* No serial calls occur inside trace_text/trace_decimal. */
        fine_trace_text_returned = true;
        M61_POST(M61_POST_TRACE_TEXT_RETURNED);
    }

    __real_serial_write_string(value);

    if (final_trace_line) {
        /* Next source operation is hold_trace_for_qmp(); POST 72 proves return. */
        M61_POST(M61_POST_QMP_HOLD_BEFORE);
        fine_acquire_active = false;
    }
}

void m61_post_boring_cpu_inventory_init(void) {
    /* Reaching this proves the immediate M61 framebuffer probe/witness returned. */
    M61_POST(M61_POST_CPU_INVENTORY_BEFORE);
    __real_boring_cpu_inventory_init();
    M61_POST(M61_POST_CPU_INVENTORY_AFTER);
}

void m61_post_boring_pci_inventory_init(void) {
    M61_POST(M61_POST_PCI_INVENTORY_BEFORE);
    __real_boring_pci_inventory_init();
    M61_POST(M61_POST_PCI_INVENTORY_AFTER);
}

void m61_post_boring_smbios_boot_init(
    const struct boring_limine_hhdm_response *hhdm,
    const struct boring_limine_memmap_response *memmap) {
    M61_POST(M61_POST_SMBIOS_BEFORE);
    __real_boring_smbios_boot_init(hhdm, memmap);
    M61_POST(M61_POST_SMBIOS_AFTER);
}

bool m61_post_pmm_init(const struct boring_limine_memmap_response *memmap) {
    bool result;

    M61_POST(M61_POST_PMM_INIT_BEFORE);
    result = __real_pmm_init(memmap);
    M61_POST(M61_POST_PMM_INIT_AFTER);
    return result;
}

bool m61_post_vmm_init(
    const struct boring_limine_hhdm_response *hhdm,
    const struct boring_limine_paging_mode_response *paging,
    const struct boring_limine_memmap_response *memmap) {
    bool result;

    M61_POST(M61_POST_VMM_INIT_BEFORE);
    result = __real_vmm_init(hhdm, paging, memmap);
    M61_POST(M61_POST_VMM_INIT_AFTER);
    return result;
}

bool m61_post_heap_init(void) {
    bool result;

    M61_POST(M61_POST_HEAP_INIT_BEFORE);
    result = __real_heap_init();
    M61_POST(M61_POST_HEAP_INIT_AFTER);
    if (result) {
        /* Preserve the existing 63 meaning after successful real heap init. */
        M61_POST(M61_POST_MEMORY_RUNTIME);
    }
    return result;
}

bool m61_post_irq_init(void) {
    const bool result = __real_irq_init();

    if (result) {
        /* Normal exception setup precedes the runtime IRQ foundation. */
        M61_POST(M61_POST_EXCEPTION_IRQ);
    }
    return result;
}

bool m61_post_usb_mass_storage_init(struct xhci_state *state) {
    const bool result = __real_usb_mass_storage_init(state);

    if (result) {
        /* xHCI discovery/configuration precedes successful USB storage. */
        M61_POST(M61_POST_USB_STORAGE);
    }
    return result;
}

enum vfs_result m61_post_boringfs_vfs_create_writable(
    const struct block_device *device, uint64_t id,
    struct boringfs_vfs **out, struct boringfs_validation_error *error) {
    const enum vfs_result result = __real_boringfs_vfs_create_writable(
        device, id, out, error);

    if (result == VFS_RESULT_OK) {
        M61_POST(M61_POST_USB_ROOT);
    }
    return result;
}

bool m61_post_process_set_name(struct process *process, const char *name) {
    const bool result = __real_process_set_name(process, name);

    if (!result) {
        return false;
    }
    if (!init_posted && name_ends_with(name, "boring-init")) {
        init_posted = true;
        M61_POST(M61_POST_BORING_INIT);
    } else if (!display_posted && name_ends_with(name, "boring-display")) {
        display_posted = true;
        M61_POST(M61_POST_BORING_DISPLAY);
    } else if (!wm_posted && name_ends_with(name, "boringwm")) {
        wm_posted = true;
        M61_POST(M61_POST_BORING_WM);
    } else if (!terminal_posted && name_ends_with(name, "boring-terminal")) {
        terminal_posted = true;
        M61_POST(M61_POST_AUTO_TERMINAL);
    }
    return true;
}

enum boring_framebuffer_user_result m61_post_boring_framebuffer_user_present(
    struct process *process, uint32_t handle) {
    const enum boring_framebuffer_user_result result =
        __real_boring_framebuffer_user_present(process, handle);

    if ((result == BORING_FRAMEBUFFER_USER_OK) && terminal_posted &&
        !desktop_posted) {
        desktop_posted = true;
        M61_POST(M61_POST_DESKTOP_PRESENT);
    }
    return result;
}
EOF_POST_C

# QEMU pc/q35 already owns port 0x80 as its built-in ioport80 compatibility
# sink, so an isa-debugcon device on the same port is not a trustworthy cheap
# observer. Keep the established QEMU runtime acceptance unchanged and prove
# here that the exact diagnostic kernel contains the real out instructions at
# every intended call boundary. Physical hardware remains the port-0x80 oracle.
POST_CPPFLAGS='-Dboring_kernel_entry=m61_post_real_boring_kernel_entry -D__real_serial_init=m61_post_serial_init -D__real_boring_cpu_inventory_init=m61_post_boring_cpu_inventory_init -D__real_boring_pci_inventory_init=m61_post_boring_pci_inventory_init -D__real_boring_smbios_boot_init=m61_post_boring_smbios_boot_init -D__real_pmm_init=m61_post_pmm_init -D__real_vmm_init=m61_post_vmm_init -D__real_heap_init=m61_post_heap_init -D__real_irq_init=m61_post_irq_init -D__real_usb_mass_storage_init=m61_post_usb_mass_storage_init -D__real_boringfs_vfs_create_writable=m61_post_boringfs_vfs_create_writable -D__real_process_set_name=m61_post_process_set_name -D__real_boring_framebuffer_user_present=m61_post_boring_framebuffer_user_present'
CPPFLAGS="-DBORING_M36_DESKTOP_ACCEPTANCE=1 -DBORING_M37_DESKTOP_ACCEPTANCE=1 -DBORING_M54_USB_ONLY_DESKTOP=1 -DBORING_M61_PHYSICAL_BREADCRUMBS=1 $POST_CPPFLAGS"
if [ "${M61_EARLY_FAULT_TEST:-0}" = 1 ]; then
    CPPFLAGS="$CPPFLAGS -DBORING_M61_EARLY_FAULT_TEST=1"
fi

rm -rf build/kernel build/iso_root
rm -f build/kernel.elf build/boringos.iso build/.test-mode
make TEST_MODE=m36-desktop \
    TEST_CPPFLAGS="$CPPFLAGS" \
    TEST_HARNESS_C='kernel/core/m61_desktop_test.c kernel/core/m37_desktop_test_adapter.c kernel/core/block_slice.c kernel/core/xhci_mixed.c kernel/arch/x86_64/xhci_mixed.c kernel/core/usb_mass_storage.c kernel/core/m61_physical_breadcrumbs.c kernel/core/m61_post80_generated.c' \
    LD='ld --wrap=serial_init --wrap=serial_write_string --wrap=boring_cpu_inventory_init --wrap=boring_pci_inventory_init --wrap=boring_smbios_boot_init --wrap=boring_framebuffer_boot_init --wrap=boring_m61_framebuffer_count --wrap=boring_framebuffer_get --wrap=boring_framebuffer_surface_valid --wrap=boring_m61_framebuffer_get --wrap=boring_graphics_fill_rect --wrap=pmm_init --wrap=vmm_init --wrap=heap_init --wrap=exception_init --wrap=syscall_test_run --wrap=boring_input_init --wrap=irq_init --wrap=timer_init --wrap=xhci_init --wrap=xhci_address_connected --wrap=xhci_discover_descriptors --wrap=xhci_configure_hid_devices_mixed --wrap=usb_mass_storage_init --wrap=boringfs_vfs_create_writable --wrap=process_set_name --wrap=boring_framebuffer_user_claim --wrap=boring_framebuffer_user_present --wrap=boring_ipc_service_register --wrap=x86_64_exception_dispatch' \
    BOOT_USER_ELF=build/user/boring-init-desktop.elf \
    BOOT_USER_NAME=boring-init.elf \
    BOOT_EXTRA_USER_ELF= BOOT_EXTRA_USER_NAME= \
    BOOT_EXTRA2_USER_ELF= BOOT_EXTRA2_USER_NAME= \
    BOOT_EXTRA3_USER_ELF= BOOT_EXTRA4_USER_ELF= \
    BOOT_LIMINE_CONF=limine-m61-usb.conf \
    build/kernel.elf build/deps/limine-binary/limine

nm build/kernel.elf | grep -Fq 'boring_m61_physical_breadcrumbs_enabled'
nm build/kernel.elf | grep -Fq 'boring_m61_post_port80_enabled'
nm build/kernel.elf | grep -Fq 'boring_m61_post_sequence'
nm build/kernel.elf | grep -Fq 'boring_m61_post_62_to_63_sequence'
nm build/kernel.elf | grep -Fq 'boring_m61_post_71_to_72_sequence'

python3 - <<'EOF_POST_VERIFY'
import re
import subprocess
from pathlib import Path

source = Path("kernel/core/m61_post80_generated.c").read_text()
ordered = [
    "M61_POST(M61_POST_KERNEL_ENTRY)",
    "M61_POST(M61_POST_EARLY_CONTAINMENT_SERIAL)",
    "M61_POST(M61_POST_MEMORY_RUNTIME)",
    "M61_POST(M61_POST_EXCEPTION_IRQ)",
    "M61_POST(M61_POST_USB_STORAGE)",
    "M61_POST(M61_POST_USB_ROOT)",
    "M61_POST(M61_POST_BORING_INIT)",
    "M61_POST(M61_POST_BORING_DISPLAY)",
    "M61_POST(M61_POST_BORING_WM)",
    "M61_POST(M61_POST_AUTO_TERMINAL)",
    "M61_POST(M61_POST_DESKTOP_PRESENT)",
]
positions = [source.find(item) for item in ordered]
if any(position < 0 for position in positions) or positions != sorted(positions):
    raise RuntimeError("M61 POST source milestone sequence is incomplete or reordered")

functions = {
    "boring_kernel_entry": ("61",),
    "m61_post_serial_init": ("62",),
    "__wrap_boring_framebuffer_boot_init": ("70", "71", "78", "79"),
    "__wrap_boring_m61_framebuffer_count": ("80", "81"),
    "__wrap_boring_framebuffer_get": ("82", "83"),
    "__wrap_boring_framebuffer_surface_valid": ("84", "85"),
    "__wrap_boring_m61_framebuffer_get": ("86",),
    "__wrap_boring_graphics_fill_rect": ("87", "88", "89", "8b", "8c", "8d"),
    "__wrap_serial_write_string": ("8a", "8e", "8f"),
    "m61_post_boring_cpu_inventory_init": ("72", "73"),
    "m61_post_boring_pci_inventory_init": ("74", "75"),
    "m61_post_boring_smbios_boot_init": ("76", "77"),
    "m61_post_pmm_init": ("7a", "7b"),
    "m61_post_vmm_init": ("7c", "7d"),
    "m61_post_heap_init": ("7e", "7f", "63"),
    "m61_post_irq_init": ("64",),
    "m61_post_usb_mass_storage_init": ("65",),
    "m61_post_boringfs_vfs_create_writable": ("66",),
    "m61_post_process_set_name": ("67", "68", "69", "6a"),
    "m61_post_boring_framebuffer_user_present": ("6f",),
}
out_count = 0
for name, codes in functions.items():
    body = subprocess.check_output(
        ["objdump", "-d", "--disassemble=" + name, "build/kernel.elf"],
        text=True)
    outputs = len(re.findall(r"\bout\b", body))
    if outputs < len(codes):
        raise RuntimeError(
            f"M61 POST binary hook {name} has only {outputs} out instructions")
    if "$0x80" not in body:
        raise RuntimeError(f"M61 POST binary hook {name} does not target port 0x80")
    for code in codes:
        if re.search(rf"\$0x0*{code}\b", body, re.IGNORECASE) is None:
            raise RuntimeError(
                f"M61 POST binary hook {name} missing code 0x{code.upper()}")
    out_count += outputs
if out_count < 43:
    raise RuntimeError(f"M61 POST binary has only {out_count} milestone outputs")
print("M61 POST port 0x80 binary acceptance: PASS")
print("M61 POST existing sequence preserved: 61 62 63 64 65 66 67 68 69 6A 6F")
print("M61 POST 62-to-63 bisector: 70 71 72 73 74 75 76 77 78 79 7A 7B 7C 7D 7E 7F then 63")
print("M61 POST 71-to-72 bisector: 80 81 82 83 84 85 86 87 88 89 8A 8B 8C 8D 8E 8F then 72")
print("M61 first actual framebuffer write witness: POST 87 before 2x2 tiny_surface_probe fill_rect")
print("M61 QEMU port 0x80 observer: SKIPPED (pc/q35 owns ioport80 sink)")
EOF_POST_VERIFY

if [ "${M61_EARLY_FAULT_TEST:-0}" = 1 ]; then
    nm build/kernel.elf | grep -Fq 'boring_m61_early_fault_test_enabled'
    printf '%s\n' 'M61 controlled early-fault test kernel: ENABLED'
else
    printf '%s\n' 'M61 safe direct framebuffer boot trace: ENABLED'
fi
printf '%s\n' 'M61 diagnostic POST port 0x80 witness: ENABLED'
