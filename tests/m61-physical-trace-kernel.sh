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

python3 tests/m61-xhci-init-bisector.py
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
#ifdef __real_timer_init
#undef __real_timer_init
#endif
#ifdef __real_xhci_init
#undef __real_xhci_init
#endif
#ifdef __real_xhci_address_connected
#undef __real_xhci_address_connected
#endif
#ifdef __real_xhci_discover_descriptors
#undef __real_xhci_discover_descriptors
#endif
#ifdef __real_xhci_configure_hid_devices_mixed
#undef __real_xhci_configure_hid_devices_mixed
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

#include <boring/block_device.h>
#include <boring/boot_protocol.h>
#include <boring/boringfs_vfs.h>
#include <boring/framebuffer.h>
#include <boring/framebuffer_user.h>
#include <boring/heap.h>
#include <boring/io.h>
#include <boring/irq.h>
#include <boring/process.h>
#include <boring/timer.h>
#include <boring/usb_mass_storage.h>
#include <boring/vmm.h>
#include <boring/xhci.h>
#include <boring/xhci_mixed.h>

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
    M61_POST_TERMINAL_START = 0x6a,
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

    M61_POST_VMM_INIT_FALSE = 0xc0,
    M61_POST_VMM_INIT_TRUE = 0xc1,
    M61_POST_VMM_STATS_FALSE = 0xc2,

    M61_POST_AFTER_IRQ_SUCCESS = 0xe7,
    M61_POST_TIMER_CALL = 0xe8,
    M61_POST_XHCI_INIT_CALL = 0xe9,
    M61_POST_XHCI_ADDRESS_CALL = 0xea,
    M61_POST_XHCI_DESCRIPTORS_CALL = 0xeb,
    M61_POST_XHCI_HID_CONFIG_CALL = 0xec,
    M61_POST_XHCI_HID_CONFIG_TRUE = 0xed,
    M61_POST_BLOCK_DEVICE_INIT_CALL = 0xee,
    M61_POST_BLOCK_DEVICE_INIT_RETURNED = 0xef,
    M61_POST_USB_CALL_ENTRY = 0xf0,
    M61_POST_USB_IMPL_ENTRY = 0xf1,
    M61_POST_USB_STATE_ACCEPTED = 0xf2,
    M61_POST_USB_DISCOVERY_MMIO = 0xf3,
    M61_POST_USB_TRANSPORT_CONFIG = 0xf4,
    M61_POST_USB_SCSI_INIT = 0xf5,
    M61_POST_USB_BLOCK_REGISTER = 0xf6,
    M61_POST_USB_RETURNED_TRUE = 0xf7,
    M61_POST_USB_RETURNED_FALSE = 0xf8,
    M61_POST_USB_FALSE_STATE = 0xf9,
    M61_POST_USB_FALSE_DISCOVERY_MMIO = 0xfa,
    M61_POST_USB_FALSE_TRANSPORT_CONFIG = 0xfb,
    M61_POST_USB_FALSE_SCSI_INIT = 0xfc,
    M61_POST_USB_FALSE_BLOCK_REGISTER = 0xfd,

    M61_POST_VMM_STATS_TRUE = 0xc3,
    M61_POST_XHCI_INIT_RETURNED_FALSE = 0xbf,
    M61_POST_XHCI_INIT_RETURNED_TRUE = 0xfe
};

const char boring_m61_post_port80_enabled[] =
    "M61 diagnostic POST port 0x80 witness enabled";
const uint8_t boring_m61_post_sequence[] = {
    0x61U, 0x62U, 0x63U, 0x64U, 0x65U, 0x66U,
    0x67U, 0x68U, 0x69U, 0x6fU
};
const uint8_t boring_m61_post_62_to_63_sequence[] = {
    0x70U, 0x71U, 0x72U, 0x73U, 0x74U, 0x75U, 0x76U, 0x77U,
    0x78U, 0x79U, 0x7aU, 0x7bU, 0x7cU, 0x7dU, 0x7eU, 0x7fU
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
bool __real_pmm_init(const struct boring_limine_memmap_response *);
bool __real_vmm_init(const struct boring_limine_hhdm_response *,
                     const struct boring_limine_paging_mode_response *,
                     const struct boring_limine_memmap_response *);
bool __real_vmm_get_stats(struct vmm_stats *stats);
uint8_t boring_m61_vmm_failure_reason(void);
bool __real_heap_init(void);
bool __real_irq_init(void);
bool __real_timer_init(uint32_t frequency_hz);
bool __real_xhci_init(struct xhci_state *state);
bool __real_xhci_address_connected(struct xhci_state *state);
bool __real_xhci_discover_descriptors(struct xhci_state *state);
bool __real_xhci_configure_hid_devices_mixed(struct xhci_state *state);
void __real_block_device_init(void);
bool __real_usb_mass_storage_init(struct xhci_state *state);
uint8_t boring_m61_usb_mass_storage_failure_reason(void);
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
bool m61_post_pmm_init(const struct boring_limine_memmap_response *);
bool m61_post_vmm_init(const struct boring_limine_hhdm_response *,
                       const struct boring_limine_paging_mode_response *,
                       const struct boring_limine_memmap_response *);
bool __wrap_vmm_get_stats(struct vmm_stats *stats);
bool m61_post_heap_init(void);
bool m61_post_irq_init(void);
bool m61_post_timer_init(uint32_t frequency_hz);
bool m61_post_xhci_init(struct xhci_state *state);
bool m61_post_xhci_address_connected(struct xhci_state *state);
bool m61_post_xhci_discover_descriptors(struct xhci_state *state);
bool m61_post_xhci_configure_hid_devices_mixed(struct xhci_state *state);
void __wrap_block_device_init(void);
bool m61_post_usb_mass_storage_init(struct xhci_state *state);
enum vfs_result m61_post_boringfs_vfs_create_writable(
    const struct block_device *device, uint64_t id,
    struct boringfs_vfs **out, struct boringfs_validation_error *error);
bool m61_post_process_set_name(struct process *process, const char *name);
enum boring_framebuffer_user_result m61_post_boring_framebuffer_user_present(
    struct process *process, uint32_t handle);

static uint8_t framebuffer_boot_init_calls;
static bool vmm_post_init_stats_pending;
static bool init_posted;
static bool display_posted;
static bool wm_posted;
static bool terminal_posted;
static bool desktop_posted;

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

void m61_post_boring_cpu_inventory_init(void) {
    /* Reaching this proves metadata-only framebuffer acquisition returned. */
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
    if (!result) {
        return result;
    }
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
    if (!result) {
        uint8_t failure_reason;

        M61_POST(M61_POST_VMM_INIT_FALSE);
        failure_reason = boring_m61_vmm_failure_reason();
        if (failure_reason != 0U) {
            M61_POST(failure_reason);
        }
        return result;
    }
    M61_POST(M61_POST_VMM_INIT_TRUE);
    vmm_post_init_stats_pending = true;
    return result;
}

bool __wrap_vmm_get_stats(struct vmm_stats *stats) {
    const bool result = __real_vmm_get_stats(stats);

    if (!vmm_post_init_stats_pending) {
        return result;
    }
    vmm_post_init_stats_pending = false;
    if (!result) {
        M61_POST(M61_POST_VMM_STATS_FALSE);
        return result;
    }
    M61_POST(M61_POST_VMM_STATS_TRUE);
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
        /* Preserve 64, then prove execution resumed beyond that exact point. */
        M61_POST(M61_POST_EXCEPTION_IRQ);
        M61_POST(M61_POST_AFTER_IRQ_SUCCESS);
    }
    return result;
}

bool m61_post_timer_init(uint32_t frequency_hz) {
    M61_POST(M61_POST_TIMER_CALL);
    return __real_timer_init(frequency_hz);
}

bool m61_post_xhci_init(struct xhci_state *state) {
    bool result;

    M61_POST(M61_POST_XHCI_INIT_CALL);
    result = __real_xhci_init(state);
    if (!result) {
        uint8_t failure_reason;

        M61_POST(M61_POST_XHCI_INIT_RETURNED_FALSE);
        failure_reason = boring_m61_xhci_failure_reason();
        if (failure_reason != 0U) {
            M61_POST(failure_reason);
        }
        return result;
    }
    M61_POST(M61_POST_XHCI_INIT_RETURNED_TRUE);
    return result;
}

bool m61_post_xhci_address_connected(struct xhci_state *state) {
    M61_POST(M61_POST_XHCI_ADDRESS_CALL);
    return __real_xhci_address_connected(state);
}

bool m61_post_xhci_discover_descriptors(struct xhci_state *state) {
    M61_POST(M61_POST_XHCI_DESCRIPTORS_CALL);
    return __real_xhci_discover_descriptors(state);
}

bool m61_post_xhci_configure_hid_devices_mixed(struct xhci_state *state) {
    bool result;

    M61_POST(M61_POST_XHCI_HID_CONFIG_CALL);
    result = __real_xhci_configure_hid_devices_mixed(state);
    if (result) {
        M61_POST(M61_POST_XHCI_HID_CONFIG_TRUE);
    }
    return result;
}

void __wrap_block_device_init(void) {
    M61_POST(M61_POST_BLOCK_DEVICE_INIT_CALL);
    __real_block_device_init();
    M61_POST(M61_POST_BLOCK_DEVICE_INIT_RETURNED);
}

bool m61_post_usb_mass_storage_init(struct xhci_state *state) {
    bool result;

    M61_POST(M61_POST_USB_CALL_ENTRY);
    result = __real_usb_mass_storage_init(state);
    if (!result) {
        uint8_t failure_reason;

        M61_POST(M61_POST_USB_RETURNED_FALSE);
        failure_reason = boring_m61_usb_mass_storage_failure_reason();
        if (failure_reason != 0U) {
            M61_POST(failure_reason);
            /* Keep the dynamic reason POST as a distinct binary witness. */
            __asm__ volatile ("" ::: "memory");
        }
        return result;
    }
    M61_POST(M61_POST_USB_RETURNED_TRUE);
    /* Preserve 65 exclusively as successful USB mass-storage initialization. */
    M61_POST(M61_POST_USB_STORAGE);
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
        M61_POST(M61_POST_TERMINAL_START);
    }
    return true;
}

/*
 * 6F is the first successful present after process naming sets wm_posted.
 * It does not necessarily identify the outer wm_ready final desktop present.
 */
enum boring_framebuffer_user_result m61_post_boring_framebuffer_user_present(
    struct process *process, uint32_t handle) {
    const enum boring_framebuffer_user_result result =
        __real_boring_framebuffer_user_present(process, handle);

    if ((result == BORING_FRAMEBUFFER_USER_OK) && wm_posted &&
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
POST_CPPFLAGS='-Dboring_kernel_entry=m61_post_real_boring_kernel_entry -D__real_serial_init=m61_post_serial_init -D__real_boring_cpu_inventory_init=m61_post_boring_cpu_inventory_init -D__real_boring_pci_inventory_init=m61_post_boring_pci_inventory_init -D__real_boring_smbios_boot_init=m61_post_boring_smbios_boot_init -D__real_pmm_init=m61_post_pmm_init -D__real_vmm_init=m61_post_vmm_init -D__real_heap_init=m61_post_heap_init -D__real_irq_init=m61_post_irq_init -D__real_timer_init=m61_post_timer_init -D__real_xhci_init=m61_post_xhci_init -D__real_xhci_address_connected=m61_post_xhci_address_connected -D__real_xhci_discover_descriptors=m61_post_xhci_discover_descriptors -D__real_xhci_configure_hid_devices_mixed=m61_post_xhci_configure_hid_devices_mixed -D__real_usb_mass_storage_init=m61_post_usb_mass_storage_init -D__real_boringfs_vfs_create_writable=m61_post_boringfs_vfs_create_writable -D__real_process_set_name=m61_post_process_set_name -D__real_boring_framebuffer_user_present=m61_post_boring_framebuffer_user_present'
CPPFLAGS="-DBORING_M36_DESKTOP_ACCEPTANCE=1 -DBORING_M37_DESKTOP_ACCEPTANCE=1 -DBORING_M54_USB_ONLY_DESKTOP=1 -DBORING_M61_PHYSICAL_BREADCRUMBS=1 $POST_CPPFLAGS"
if [ "${M61_EARLY_FAULT_TEST:-0}" = 1 ]; then
    CPPFLAGS="$CPPFLAGS -DBORING_M61_EARLY_FAULT_TEST=1"
fi

rm -rf build/kernel build/iso_root
rm -f build/kernel.elf build/boringos.iso build/.test-mode
make TEST_MODE=m36-desktop \
    TEST_CPPFLAGS="$CPPFLAGS" \
    TEST_HARNESS_C='kernel/core/m61_desktop_test.c kernel/core/m37_desktop_test_adapter.c kernel/core/block_slice.c kernel/core/xhci_mixed.c kernel/arch/x86_64/xhci_mixed.c kernel/core/usb_mass_storage.c kernel/core/m61_physical_breadcrumbs.c kernel/core/m61_post80_generated.c' \
    LD='ld --wrap=serial_init --wrap=boring_cpu_inventory_init --wrap=boring_pci_inventory_init --wrap=boring_smbios_boot_init --wrap=boring_framebuffer_boot_init --wrap=pmm_init --wrap=vmm_init --wrap=vmm_get_stats --wrap=heap_init --wrap=exception_init --wrap=syscall_test_run --wrap=boring_input_init --wrap=irq_init --wrap=timer_init --wrap=xhci_init --wrap=xhci_address_connected --wrap=xhci_discover_descriptors --wrap=xhci_configure_hid_devices_mixed --wrap=block_device_init --wrap=usb_mass_storage_init --wrap=boringfs_vfs_create_writable --wrap=process_set_name --wrap=boring_framebuffer_user_claim --wrap=boring_framebuffer_user_present --wrap=boring_ipc_service_register --wrap=x86_64_exception_dispatch' \
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
nm build/kernel.elf | grep -Fq 'boring_m61_vmm_failure_reason'
nm build/kernel.elf | grep -Fq 'boring_m61_xhci_failure_reason'
nm build/kernel.elf | grep -Fq 'boring_m61_usb_mass_storage_failure_reason'
nm build/kernel.elf | grep -Fq 'vmm_inspect_mapping'
nm build/kernel.elf | grep -Fq 'vmm_map_framebuffer_region'
nm build/kernel.elf | grep -Fq 'boring_m61_framebuffer_prepare_runtime'
nm build/kernel.elf | grep -Fq 'boring_m61_first_framebuffer_store_active'
nm build/kernel.elf | grep -Fq 'boring_m61_framebuffer_fault_post_codes'

python3 - <<'EOF_POST_VERIFY'
import re
import subprocess
from pathlib import Path

source = Path("kernel/core/m61_post80_generated.c").read_text()
entry_source = Path("kernel/core/entry.c").read_text()
vmm_source = Path("kernel/arch/x86_64/vmm.c").read_text()
vmm_header_source = Path("kernel/include/boring/vmm.h").read_text()
mmio_source = Path("kernel/arch/x86_64/mmio.c").read_text()
framebuffer_source = Path("kernel/core/framebuffer.c").read_text()
graphics_source = Path("kernel/core/graphics.c").read_text()
trace_source = Path("kernel/core/m61_physical_breadcrumbs.c").read_text()
usb_source = Path("kernel/core/usb_mass_storage_impl.inc").read_text()
xhci_source = Path("kernel/arch/x86_64/xhci.c").read_text()
m37_source = Path("kernel/core/m37_desktop_test.c").read_text()
m61_source = Path("kernel/core/m61_desktop_test.c").read_text()
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
    "M61_POST(M61_POST_DESKTOP_PRESENT)",
]
positions = [source.find(item) for item in ordered]
if any(position < 0 for position in positions) or positions != sorted(positions):
    raise RuntimeError("M61 POST source boot milestone sequence is incomplete or reordered")
if "M61_POST(M61_POST_TERMINAL_START)" not in source:
    raise RuntimeError("M61 manual terminal-start POST 6A reservation is missing")

def function_definition(name, text=source, return_type="bool"):
    signature = re.compile(
        rf"\b{re.escape(return_type)}\s+{re.escape(name)}\s*\("
    )
    for match in signature.finditer(text):
        position = match.end()
        depth = 1
        while position < len(text) and depth:
            if text[position] == "(":
                depth += 1
            elif text[position] == ")":
                depth -= 1
            position += 1
        if depth or text[position:].lstrip()[:1] != "{":
            continue
        body_start = text.index("{", position)
        position = body_start + 1
        depth = 1
        while position < len(text) and depth:
            if text[position] == "{":
                depth += 1
            elif text[position] == "}":
                depth -= 1
            position += 1
        if depth:
            raise RuntimeError(f"M61 function definition is incomplete: {name}")
        return text[match.start():position]
    raise RuntimeError(f"M61 function definition missing: {name}")

vmm_init_source = function_definition("m61_post_vmm_init")
vmm_stats_source = function_definition("__wrap_vmm_get_stats")
real_vmm_init_source = function_definition("vmm_init", vmm_source)
for required in (
    "M61_POST(M61_POST_VMM_INIT_BEFORE);",
    "result = __real_vmm_init(hhdm, paging, memmap);",
    "M61_POST(M61_POST_VMM_INIT_AFTER);",
    "M61_POST(M61_POST_VMM_INIT_FALSE);",
    "failure_reason = boring_m61_vmm_failure_reason();",
    "M61_POST(failure_reason);",
    "M61_POST(M61_POST_VMM_INIT_TRUE);",
    "vmm_post_init_stats_pending = true;",
):
    if required not in vmm_init_source:
        raise RuntimeError(f"M61 VMM init source missing: {required}")
if vmm_init_source.index("M61_POST(M61_POST_VMM_INIT_BEFORE);") > vmm_init_source.index("result = __real_vmm_init(hhdm, paging, memmap);"):
    raise RuntimeError("M61 VMM 7C is not before real vmm_init")
if vmm_init_source.index("result = __real_vmm_init(hhdm, paging, memmap);") > vmm_init_source.index("M61_POST(M61_POST_VMM_INIT_AFTER);"):
    raise RuntimeError("M61 VMM 7D is not after real vmm_init")
false_post = vmm_init_source.index("M61_POST(M61_POST_VMM_INIT_FALSE);")
reason_read = vmm_init_source.index("failure_reason = boring_m61_vmm_failure_reason();")
reason_post = vmm_init_source.index("M61_POST(failure_reason);")
false_return = vmm_init_source.index("return result;", reason_post)
true_post = vmm_init_source.index("M61_POST(M61_POST_VMM_INIT_TRUE);")
if not (false_post < reason_read < reason_post < false_return < true_post):
    raise RuntimeError("M61 VMM false reason is not replayed after C0 and before false return")
if "failure_reason" in vmm_init_source[true_post:]:
    raise RuntimeError("M61 VMM true path replays a false reason")
for required in (
    "const bool result = __real_vmm_get_stats(stats);",
    "if (!vmm_post_init_stats_pending)",
    "vmm_post_init_stats_pending = false;",
    "M61_POST(M61_POST_VMM_STATS_FALSE);",
    "M61_POST(M61_POST_VMM_STATS_TRUE);",
):
    if required not in vmm_stats_source:
        raise RuntimeError(f"M61 VMM stats source missing: {required}")
if not re.search(
    r"if\s*\(\s*!result\s*\)\s*\{\s*"
    r"M61_POST\(M61_POST_VMM_STATS_FALSE\);\s*return result;\s*\}\s*"
    r"M61_POST\(M61_POST_VMM_STATS_TRUE\);",
    vmm_stats_source,
):
    raise RuntimeError("M61 VMM stats result POST branches are not separated")
if "framebuffer" in vmm_init_source or "framebuffer" in vmm_stats_source:
    raise RuntimeError("M61 VMM result bisector gained framebuffer dependency")
if not re.search(
    r"if\s*\(\s*!vmm_init\([\s\S]*?\)\s*\|\|\s*!vmm_get_stats\(&vmm_stats\)\s*\)",
    entry_source,
):
    raise RuntimeError("normal VMM short-circuit gate changed")

reason_codes = [*range(0xD0, 0xE0), *range(0xE0, 0xE7)]
reason_literals = [f"0x{code:02X}U" for code in reason_codes]
if len(reason_codes) != 23 or len(set(reason_codes)) != 23:
    raise RuntimeError("M61 VMM reason inventory is not exactly 23 unique codes")
for literal in reason_literals:
    if vmm_source.count(f"VMM_M61_REJECT({literal})") != 1:
        raise RuntimeError(f"M61 VMM reason missing or duplicated: {literal}")
for code in reason_codes:
    if not (0xD0 <= code <= 0xDF or 0xE0 <= code <= 0xE6):
        raise RuntimeError(f"M61 VMM reason outside audited range: 0x{code:02X}")
if "VMM_M61_CLEAR_FAILURE();\n    vmm_reset_state();" not in vmm_source:
    raise RuntimeError("M61 VMM reason state is not cleared at vmm_init entry")
if not re.search(
    r"#ifdef BORING_M61_PHYSICAL_BREADCRUMBS[\s\S]*?"
    r"static uint8_t vmm_m61_failure_reason;[\s\S]*?"
    r"uint8_t boring_m61_vmm_failure_reason\(void\)[\s\S]*?"
    r"#else[\s\S]*?#define VMM_M61_CLEAR_FAILURE\(\) do \{ \} while \(0\)",
    vmm_source,
):
    raise RuntimeError("M61 VMM failure state/accessor is not candidate-gated")
for required in (
    "if (!vmm_validate_memory_map(memory_map)) {\n        return false;\n    }",
    "if (!vmm_test_region_clear_of_hhdm()) {\n        vmm_reset_state();\n        return false;\n    }",
    "vmm_initialized = true;\n    return true;",
):
    if required not in vmm_source:
        raise RuntimeError(f"M61 VMM original result path changed: {required}")
cap_match = re.search(
    r"^#define VMM_MAX_MEMORY_MAP_ENTRIES ([0-9]+)ULL$",
    vmm_source,
    re.MULTILINE,
)
if cap_match is None or int(cap_match.group(1), 10) != 4096:
    raise RuntimeError("M61 VMM memory-map cap is not the intended 4096-entry limit")
if vmm_source.count("VMM_MAX_MEMORY_MAP_ENTRIES") != 2:
    raise RuntimeError("M61 VMM memory-map cap gained unexpected dependent use or storage")
if (
    "if (memory_map->entry_count > VMM_MAX_MEMORY_MAP_ENTRIES) {\n"
    "        VMM_M61_REJECT(0xD6U);\n"
    "    }"
) not in vmm_source:
    raise RuntimeError("M61 VMM D6 memory-map cap rejection changed")
vmm_memory_map_cap = int(cap_match.group(1), 10)
if 4096 > vmm_memory_map_cap:
    raise RuntimeError("M61 VMM entry_count == 4096 is not accepted by the cap")
if not (4097 > vmm_memory_map_cap):
    raise RuntimeError("M61 VMM entry_count == 4097 is not rejected by the cap")
if ("serial_write" in vmm_source or
        "framebuffer" in real_vmm_init_source.lower()):
    raise RuntimeError("M61 VMM failure classification gained serial/framebuffer dependency")

# The physical 90 boundary must remain immediately before the real byte store,
# and 91 must remain reachable only after that store returns.
pre_store_post = graphics_source.find(
    "(uint8_t)M61_NORMAL_FRAMEBUFFER_PRE_POST);")
store_expression = graphics_source.find(
    "surface->address[offset + (uint64_t)byte_index] =")
store_inactive = graphics_source.find(
    "m61_first_normal_framebuffer_store_active = false;")
post_store_post = graphics_source.find(
    "(uint8_t)M61_NORMAL_FRAMEBUFFER_POST_POST);")
if not (0 <= pre_store_post < store_expression < store_inactive < post_store_post):
    raise RuntimeError("M61 real framebuffer 90/store/91 source order changed")
if graphics_source.count(
        "m61_first_normal_framebuffer_store_active = true;") != 1:
    raise RuntimeError("M61 first-store fault seam is not armed exactly once")
if "0x91" in trace_source.lower():
    raise RuntimeError("M61 exception diagnostics can fake POST 91")

fault_codes = {0x30, 0x31, 0x32, 0x33}
claimed_codes = (
    set(range(0x40, 0x5E)) |
    set(range(0x61, 0x80)) |
    set(range(0x80, 0x90)) |
    set(range(0x90, 0x9A)) |
    set(range(0xA0, 0xC4)) |
    set(range(0xD0, 0xFF))
)
if fault_codes & claimed_codes:
    raise RuntimeError("M61 framebuffer fault POST codes collide with frozen meanings")
fault_assignments = {
    name: int(value, 16)
    for name, value in re.findall(
        r"^\s*(M61_FRAMEBUFFER_FAULT_[A-Z_]+)\s*=\s*"
        r"0x([0-9a-fA-F]{2})\s*,?\s*$",
        trace_source,
        re.MULTILINE,
    )
}
if fault_assignments != {
    "M61_FRAMEBUFFER_FAULT_PAGE": 0x30,
    "M61_FRAMEBUFFER_FAULT_GENERAL_PROTECTION": 0x31,
    "M61_FRAMEBUFFER_FAULT_MACHINE_CHECK": 0x32,
    "M61_FRAMEBUFFER_FAULT_OTHER": 0x33,
}:
    raise RuntimeError("M61 framebuffer fault POST map is incomplete")
if not all(token in trace_source for token in (
    "if (frame->vector == 14ULL)",
    "if (frame->vector == 13ULL)",
    "if (frame->vector == 18ULL)",
    "x86_64_out8((uint16_t)0x80U, code);",
    "if (boring_m61_first_framebuffer_store_active())",
    "framebuffer_fault_halt(frame);",
)):
    raise RuntimeError("M61 post-90 exception classification is incomplete")

exception_wrapper = function_definition("__wrap_exception_init", trace_source)
exception_real = exception_wrapper.find("result = __real_exception_init();")
containment_release = exception_wrapper.find("early_containment_active = false;")
normalization_call = exception_wrapper.find(
    "framebuffer_ready = boring_m61_framebuffer_prepare_runtime();")
normalization_report = exception_wrapper.find(
    "serial_framebuffer_normalization(framebuffer_ready);")
if not (0 <= exception_real < containment_release < normalization_call <
        normalization_report):
    raise RuntimeError("M61 framebuffer normalization is not after normal exception init")

acquire_start = trace_source.find("static void acquire_framebuffers(void) {")
acquire_end = trace_source.find("\nstatic bool ends_with", acquire_start)
if (acquire_start < 0 or acquire_end < 0 or
        "boring_graphics_" in trace_source[acquire_start:acquire_end] or
        "boring_m61_framebuffer_prepare_runtime" in
        trace_source[acquire_start:acquire_end]):
    raise RuntimeError("M61 early framebuffer acquisition gained writes/mapping")

normalize_source = function_definition(
    "boring_m61_framebuffer_normalize", framebuffer_source)
map_call = normalize_source.find("vmm_map_framebuffer_region(")
address_rebind = normalize_source.find("surface->address = candidate.address;")
if not (0 <= map_call < address_rebind):
    raise RuntimeError("M61 framebuffer address changes before mapping success")
if "boring_graphics_" in normalize_source:
    raise RuntimeError("M61 framebuffer normalization performs a pixel write")
for required in (
    "vmm_resolve_limine_framebuffer(",
    "diagnostics->memmap_range_match = true;",
    "diagnostics->metadata_preserved =",
    "diagnostics->alias_start_mapping.effective_writable",
    "diagnostics->alias_end_mapping.effective_writable",
    "boring_framebuffer_surface_valid(surface)",
):
    if required not in normalize_source:
        raise RuntimeError(f"M61 framebuffer normalization missing: {required}")

for required in (
    "VMM_PAGE_SIZE_2M", "VMM_PAGE_SIZE_1G",
    "VMM_ENTRY_ADDRESS_MASK_2M", "VMM_ENTRY_ADDRESS_MASK_1G",
    "parent_writable && leaf_writable",
    "vmm_resolve_limine_framebuffer",
    "BORING_LIMINE_MEMMAP_FRAMEBUFFER",
):
    if required not in vmm_source:
        raise RuntimeError(f"M61 large-page framebuffer inspector missing: {required}")
for required in (
    "#define VMM_FRAMEBUFFER_WINDOW_BASE ((uintptr_t)0xffffff0004000000ULL)",
    "#define VMM_FRAMEBUFFER_WINDOW_SIZE ((size_t)0x04000000U)",
):
    if required not in vmm_header_source:
        raise RuntimeError(f"M61 framebuffer MMIO window changed: {required}")
for required in (
    "VMM_PTE_CACHE_DISABLE", "vmm_inspect_mapping(current_virtual, &existing)",
    "vmm_map_framebuffer_region", "VMM_FRAMEBUFFER_WINDOW_PAGES",
    "PCI MMIO and framebuffer windows must not overlap",
):
    if required not in mmio_source:
        raise RuntimeError(f"M61 framebuffer MMIO mapper missing: {required}")

# Audit the exact post-64 path inherited by M61 from the M37/M54 desktop.
input_hardware = function_definition("input_hardware_init", m37_source)
pre_usb_calls = (
    "irq_init()", "timer_init(100U)", "xhci_init(&state)",
    "xhci_address_connected(&state)", "xhci_discover_descriptors(&state)",
    "xhci_configure_hid_devices(&state)", "m54_hid_protocols_ready(&state)",
)
pre_usb_positions = [input_hardware.find(call) for call in pre_usb_calls]
if any(position < 0 for position in pre_usb_positions) or pre_usb_positions != sorted(pre_usb_positions):
    raise RuntimeError("M61 real IRQ-to-xHCI pre-USB call order changed")
mount_root = function_definition("mount_root", m37_source)
if mount_root.find("block_device_init();") < 0 or mount_root.find("virtio_blk_init()") < 0 or mount_root.find("block_device_init();") > mount_root.find("virtio_blk_init()"):
    raise RuntimeError("M61 block-device initialization no longer precedes USB-root init")
root_state = m61_source.find("const struct xhci_state *published = xhci_get_state();")
root_usb = m61_source.find("usb_mass_storage_init(&state)")
if root_state < 0 or root_usb < 0 or root_state > root_usb:
    raise RuntimeError("M61 root no longer obtains published xHCI state before USB storage")

new_post_codes = list(range(0xE7, 0xFE))
if len(new_post_codes) != 23 or len(set(new_post_codes)) != 23:
    raise RuntimeError("M61 USB bisector code inventory is not exactly E7-FD")
if set(new_post_codes) & set(reason_codes):
    raise RuntimeError("M61 USB bisector overlaps the VMM D0-E6 reason namespace")
for code in new_post_codes:
    literal = f"0x{code:02x}"
    if len(re.findall(rf"=\s*{re.escape(literal)}\b", source, re.IGNORECASE)) != 1:
        raise RuntimeError(f"M61 USB bisector enum code missing/duplicated: 0x{code:02X}")

usb_init_source = function_definition("usb_mass_storage_init", usb_source)
progress_codes = [f"0x{code:02X}U" for code in range(0xF1, 0xF7)]
progress_positions = [usb_init_source.find(f"MSC_M61_PROGRESS({code})") for code in progress_codes]
if any(position < 0 for position in progress_positions) or progress_positions != sorted(progress_positions):
    raise RuntimeError("M61 USB major progress breadcrumbs are incomplete or reordered")
usb_reason_codes = [f"0x{code:02X}U" for code in range(0xF9, 0xFE)]
reason_matches = list(re.finditer(r"MSC_M61_FAILURE\((0xF[9A-D]U)\);", usb_init_source))
false_matches = list(re.finditer(r"\breturn\s+false\s*;", usb_init_source))
if [match.group(1) for match in reason_matches] != usb_reason_codes:
    raise RuntimeError("M61 USB false reason map is incomplete or reordered")
if len(false_matches) != 5 or len(reason_matches) != len(false_matches):
    raise RuntimeError("M61 USB top-level false exits are not exactly five classified paths")
previous_false = 0
for reason_match, false_match in zip(reason_matches, false_matches):
    if not (previous_false <= reason_match.start() < false_match.start()):
        raise RuntimeError("M61 USB false exit does not have a preceding reason")
    between = usb_init_source[previous_false:false_match.start()]
    if len(re.findall(r"MSC_M61_FAILURE\(0xF[9A-D]U\);", between)) != 1:
        raise RuntimeError("M61 USB false exit is not classified exactly once")
    previous_false = false_match.end()
for code in usb_reason_codes[1:]:
    reason_position = usb_init_source.find(f"MSC_M61_FAILURE({code})")
    next_false = usb_init_source.find("return false;", reason_position)
    cleanup = usb_init_source.find("usb_mass_storage_cleanup();", reason_position)
    if not (reason_position < cleanup < next_false):
        raise RuntimeError(f"M61 USB reason {code} is not retained before cleanup/false return")
if not re.search(
    r"#if USB_MASS_STORAGE_RUNTIME && defined\(BORING_M61_PHYSICAL_BREADCRUMBS\)"
    r"[\s\S]*?static uint8_t msc_m61_failure_reason;"
    r"[\s\S]*?uint8_t boring_m61_usb_mass_storage_failure_reason\(void\)"
    r"[\s\S]*?#else"
    r"[\s\S]*?#define MSC_M61_BEGIN\(\) do \{ \} while \(0\)"
    r"[\s\S]*?#define MSC_M61_PROGRESS\(code\) do \{ \} while \(0\)"
    r"[\s\S]*?#define MSC_M61_FAILURE\(code\) do \{ \} while \(0\)",
    usb_source,
):
    raise RuntimeError("M61 USB diagnostics are not fully candidate-gated/no-op outside M61")
if any(token in usb_source for token in ("serial_write", "framebuffer")):
    raise RuntimeError("M61 USB diagnostics gained serial/framebuffer dependency")

irq_source = function_definition("m61_post_irq_init")
xhci_post_source = function_definition("m61_post_xhci_init")
hid_source = function_definition("m61_post_xhci_configure_hid_devices_mixed")
usb_post_source = function_definition("m61_post_usb_mass_storage_init")
desktop_post_source = function_definition(
    "m61_post_boring_framebuffer_user_present",
    return_type="enum boring_framebuffer_user_result",
)
if irq_source.find("M61_POST(M61_POST_EXCEPTION_IRQ);") < 0 or irq_source.find("M61_POST(M61_POST_AFTER_IRQ_SUCCESS);") < irq_source.find("M61_POST(M61_POST_EXCEPTION_IRQ);"):
    raise RuntimeError("M61 64 meaning/order changed")
xhci_call_post = xhci_post_source.index("M61_POST(M61_POST_XHCI_INIT_CALL);")
xhci_real_call = xhci_post_source.index("result = __real_xhci_init(state);")
xhci_false = xhci_post_source.index("M61_POST(M61_POST_XHCI_INIT_RETURNED_FALSE);")
xhci_reason_read = xhci_post_source.index("failure_reason = boring_m61_xhci_failure_reason();")
xhci_reason_replay = xhci_post_source.index("M61_POST(failure_reason);")
xhci_false_return = xhci_post_source.index("return result;", xhci_reason_replay)
xhci_true = xhci_post_source.index("M61_POST(M61_POST_XHCI_INIT_RETURNED_TRUE);")
if not (xhci_call_post < xhci_real_call < xhci_false < xhci_reason_read < xhci_reason_replay < xhci_false_return < xhci_true):
    raise RuntimeError("M61 xHCI init false replay/true control-flow ordering changed")
if "failure_reason" in xhci_post_source[xhci_true:]:
    raise RuntimeError("M61 xHCI init true path replays a failure reason")
if xhci_post_source.count("M61_POST(M61_POST_XHCI_INIT_CALL);") != 1:
    raise RuntimeError("M61 E9 xHCI init call meaning is not unique")
if source.count("M61_POST(M61_POST_XHCI_ADDRESS_CALL);") != 1:
    raise RuntimeError("M61 EA xHCI address call meaning is not unique")
hid_call_post = hid_source.find("M61_POST(M61_POST_XHCI_HID_CONFIG_CALL);")
hid_real_call = hid_source.find("result = __real_xhci_configure_hid_devices_mixed(state);")
hid_true_post = hid_source.find("M61_POST(M61_POST_XHCI_HID_CONFIG_TRUE);")
if not (0 <= hid_call_post < hid_real_call < hid_true_post):
    raise RuntimeError("M61 xHCI HID stage does not preserve hard-hang observability")
for required in (
    "M61_POST(M61_POST_USB_CALL_ENTRY);",
    "result = __real_usb_mass_storage_init(state);",
    "M61_POST(M61_POST_USB_RETURNED_FALSE);",
    "failure_reason = boring_m61_usb_mass_storage_failure_reason();",
    "M61_POST(failure_reason);",
    "M61_POST(M61_POST_USB_RETURNED_TRUE);",
    "M61_POST(M61_POST_USB_STORAGE);",
):
    if required not in usb_post_source:
        raise RuntimeError(f"M61 USB wrapper source missing: {required}")
usb_call = usb_post_source.index("result = __real_usb_mass_storage_init(state);")
usb_false = usb_post_source.index("M61_POST(M61_POST_USB_RETURNED_FALSE);")
usb_reason_read = usb_post_source.index("failure_reason = boring_m61_usb_mass_storage_failure_reason();")
usb_reason_replay = usb_post_source.index("M61_POST(failure_reason);")
usb_false_return = usb_post_source.index("return result;", usb_reason_replay)
usb_true = usb_post_source.index("M61_POST(M61_POST_USB_RETURNED_TRUE);")
usb_65 = usb_post_source.index("M61_POST(M61_POST_USB_STORAGE);")
if not (usb_call < usb_false < usb_reason_read < usb_reason_replay < usb_false_return < usb_true < usb_65):
    raise RuntimeError("M61 USB false replay/true/65 control-flow ordering changed")
if "failure_reason" in usb_post_source[usb_true:]:
    raise RuntimeError("M61 USB true path replays a failure reason")
if usb_post_source.count("M61_POST(M61_POST_USB_STORAGE);") != 1:
    raise RuntimeError("M61 65 success meaning is not unique")
if "terminal_posted" in desktop_post_source:
    raise RuntimeError("M61 POST 6F still requires terminal startup")
real_present = desktop_post_source.find("__real_boring_framebuffer_user_present(process, handle)")
wm_gate = desktop_post_source.find("(result == BORING_FRAMEBUFFER_USER_OK) && wm_posted")
desktop_post = desktop_post_source.find("M61_POST(M61_POST_DESKTOP_PRESENT);")
if not (0 <= real_present < wm_gate < desktop_post):
    raise RuntimeError("M61 POST 6F is not after successful real post-WM present")

functions = {
    "boring_kernel_entry": ("61",),
    "m61_post_serial_init": ("62",),
    "__wrap_boring_framebuffer_boot_init": ("70", "71", "78", "79"),
    "m61_post_boring_cpu_inventory_init": ("72", "73"),
    "m61_post_boring_pci_inventory_init": ("74", "75"),
    "m61_post_boring_smbios_boot_init": ("76", "77"),
    "m61_post_pmm_init": ("7a", "7b"),
    "m61_post_vmm_init": ("7c", "7d", "c0", "c1"),
    "__wrap_vmm_get_stats": ("c2", "c3"),
    "m61_post_heap_init": ("7e", "7f", "63"),
    "m61_post_irq_init": ("64", "e7"),
    "m61_post_timer_init": ("e8",),
    "m61_post_xhci_init": ("e9", "bf", "fe"),
    "xhci_init": tuple(f"{code:02x}" for code in (
        *range(0x88, 0x90), *range(0xac, 0xbf))),
    "m61_post_xhci_address_connected": ("ea",),
    "m61_post_xhci_discover_descriptors": ("eb",),
    "m61_post_xhci_configure_hid_devices_mixed": ("ec", "ed"),
    "__wrap_block_device_init": ("ee", "ef"),
    "m61_post_usb_mass_storage_init": ("f0", "f7", "f8", "65"),
    "usb_mass_storage_init_legacy": ("f1", "f2", "f3", "f4", "f5", "f6", "f9", "fa", "fb", "fc", "fd"),
    "m61_post_boringfs_vfs_create_writable": ("66",),
    "m61_post_process_set_name": ("67", "68", "69", "6a"),
    "m61_post_boring_framebuffer_user_present": ("6f",),
}

def has_low_byte_immediate(body, code):
    wanted = int(code, 16)
    for match in re.finditer(r"\$0x([0-9a-f]+)\b", body, re.IGNORECASE):
        if (int(match.group(1), 16) & 0xff) == wanted:
            return True
    return False

out_count = 0
for name, codes in functions.items():
    body = subprocess.check_output(
        ["objdump", "-d", "--disassemble=" + name, "build/kernel.elf"],
        text=True)
    outputs = len(re.findall(r"\bout\b", body))
    dynamic_output = name in (
        "m61_post_vmm_init", "m61_post_usb_mass_storage_init")
    minimum_outputs = len(codes) + (1 if dynamic_output else 0)
    if outputs < minimum_outputs:
        raise RuntimeError(
            f"M61 POST binary hook {name} has only {outputs} out instructions")
    if "$0x80" not in body:
        raise RuntimeError(f"M61 POST binary hook {name} does not target port 0x80")
    for code in codes:
        if not has_low_byte_immediate(body, code):
            raise RuntimeError(
                f"M61 POST binary hook {name} missing code 0x{code.upper()}")
    if name == "m61_post_vmm_init" and "boring_m61_vmm_failure_reason" not in body:
        raise RuntimeError("M61 VMM binary hook does not call the failure-reason accessor")
    if name == "m61_post_xhci_init" and "boring_m61_xhci_failure_reason" not in body:
        raise RuntimeError("M61 xHCI binary hook does not call the failure-reason accessor")
    if name == "m61_post_usb_mass_storage_init" and "boring_m61_usb_mass_storage_failure_reason" not in body:
        raise RuntimeError("M61 USB binary hook does not call the failure-reason accessor")
    out_count += outputs
if out_count < 84:
    raise RuntimeError(f"M61 POST binary has only {out_count} milestone outputs")
print("M61 POST port 0x80 binary acceptance: PASS")
print("M61 POST boot sequence: 61 62 63 64 65 66 67 68 69 6F")
print("M61 POST 6A: reserved for a real terminal process start; not a boot prerequisite")
print("M61 POST 62-to-63 bisector: 70 71 72 73 74 75 76 77 78 79 7A 7B 7C 7D 7E 7F then 63")
print("M61 VMM result bisector: C0 init-false C1 init-true C2 stats-false C3 stats-true")
print("M61 VMM false reasons: D0-DF E0-E6, replayed after C0")
print("M61 64-to-65 USB progress: E7 E8 E9 EA EB EC ED EE EF F0 F1 F2 F3 F4 F5 F6 F7 then 65")
print("M61 xHCI init progress: 88-8F AC-B2; false reasons B3-BE replayed after BF; FE returned-true")
print("M61 USB false: F8 returned-false; reasons F9-FD replayed last")
print("M61 VMM short-circuit source gate: PRESERVED")
print("M61 QEMU port 0x80 observer: SKIPPED (pc/q35 owns ioport80 sink)")
EOF_POST_VERIFY

if [ "${M61_EARLY_FAULT_TEST:-0}" = 1 ]; then
    nm build/kernel.elf | grep -Fq 'boring_m61_early_fault_test_enabled'
    printf '%s\n' 'M61 controlled early-fault test kernel: ENABLED'
else
    printf '%s\n' 'M61 safe native boot console: ENABLED'
fi
printf '%s\n' 'M61 diagnostic POST port 0x80 witness: ENABLED'
