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

#include <boring/boringfs_vfs.h>
#include <boring/framebuffer_user.h>
#include <boring/heap.h>
#include <boring/io.h>
#include <boring/irq.h>
#include <boring/process.h>
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
    M61_POST_DESKTOP_PRESENT = 0x6f
};

const char boring_m61_post_port80_enabled[] =
    "M61 diagnostic POST port 0x80 witness enabled";
const uint8_t boring_m61_post_sequence[] = {
    0x61U, 0x62U, 0x63U, 0x64U, 0x65U, 0x66U,
    0x67U, 0x68U, 0x69U, 0x6aU, 0x6fU
};

void boring_kernel_entry(void);
void m61_post_real_boring_kernel_entry(void);
void __real_serial_init(void);
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
bool m61_post_heap_init(void);
bool m61_post_irq_init(void);
bool m61_post_usb_mass_storage_init(struct xhci_state *state);
enum vfs_result m61_post_boringfs_vfs_create_writable(
    const struct block_device *device, uint64_t id,
    struct boringfs_vfs **out, struct boringfs_validation_error *error);
bool m61_post_process_set_name(struct process *process, const char *name);
enum boring_framebuffer_user_result m61_post_boring_framebuffer_user_present(
    struct process *process, uint32_t handle);

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

bool m61_post_heap_init(void) {
    const bool result = __real_heap_init();

    if (result) {
        /* PMM and VMM are already prerequisites at this call boundary. */
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
POST_CPPFLAGS='-Dboring_kernel_entry=m61_post_real_boring_kernel_entry -D__real_serial_init=m61_post_serial_init -D__real_heap_init=m61_post_heap_init -D__real_irq_init=m61_post_irq_init -D__real_usb_mass_storage_init=m61_post_usb_mass_storage_init -D__real_boringfs_vfs_create_writable=m61_post_boringfs_vfs_create_writable -D__real_process_set_name=m61_post_process_set_name -D__real_boring_framebuffer_user_present=m61_post_boring_framebuffer_user_present'
CPPFLAGS="-DBORING_M36_DESKTOP_ACCEPTANCE=1 -DBORING_M37_DESKTOP_ACCEPTANCE=1 -DBORING_M54_USB_ONLY_DESKTOP=1 -DBORING_M61_PHYSICAL_BREADCRUMBS=1 $POST_CPPFLAGS"
if [ "${M61_EARLY_FAULT_TEST:-0}" = 1 ]; then
    CPPFLAGS="$CPPFLAGS -DBORING_M61_EARLY_FAULT_TEST=1"
fi

rm -rf build/kernel build/iso_root
rm -f build/kernel.elf build/boringos.iso build/.test-mode
make TEST_MODE=m36-desktop \
    TEST_CPPFLAGS="$CPPFLAGS" \
    TEST_HARNESS_C='kernel/core/m61_desktop_test.c kernel/core/m37_desktop_test_adapter.c kernel/core/block_slice.c kernel/core/xhci_mixed.c kernel/arch/x86_64/xhci_mixed.c kernel/core/usb_mass_storage.c kernel/core/m61_physical_breadcrumbs.c kernel/core/m61_post80_generated.c' \
    LD='ld --wrap=serial_init --wrap=boring_cpu_inventory_init --wrap=boring_pci_inventory_init --wrap=boring_smbios_boot_init --wrap=pmm_init --wrap=vmm_init --wrap=heap_init --wrap=exception_init --wrap=syscall_test_run --wrap=boring_input_init --wrap=irq_init --wrap=timer_init --wrap=xhci_init --wrap=xhci_address_connected --wrap=xhci_discover_descriptors --wrap=xhci_configure_hid_devices_mixed --wrap=usb_mass_storage_init --wrap=boringfs_vfs_create_writable --wrap=process_set_name --wrap=boring_framebuffer_user_claim --wrap=boring_framebuffer_user_present --wrap=boring_ipc_service_register --wrap=x86_64_exception_dispatch' \
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
    "m61_post_heap_init": ("63",),
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
if out_count < 11:
    raise RuntimeError(f"M61 POST binary has only {out_count} milestone outputs")
print("M61 POST port 0x80 binary acceptance: PASS")
print("M61 POST sequence: 61 62 63 64 65 66 67 68 69 6A 6F")
print("M61 QEMU port 0x80 observer: SKIPPED (pc/q35 owns ioport80 sink)")
EOF_POST_VERIFY

if [ "${M61_EARLY_FAULT_TEST:-0}" = 1 ]; then
    nm build/kernel.elf | grep -Fq 'boring_m61_early_fault_test_enabled'
    printf '%s\n' 'M61 controlled early-fault test kernel: ENABLED'
else
    printf '%s\n' 'M61 safe direct framebuffer boot trace: ENABLED'
fi
printf '%s\n' 'M61 diagnostic POST port 0x80 witness: ENABLED'
