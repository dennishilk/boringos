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

static void post_code(enum m61_post_code code) {
    x86_64_out8((uint16_t)M61_POST_PORT, (uint8_t)code);
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
    post_code(M61_POST_KERNEL_ENTRY);
    m61_post_real_boring_kernel_entry();
}

void m61_post_serial_init(void) {
    /* Existing M61 wrapper has installed its bounded early exception IDT. */
    __real_serial_init();
    /* COM1 probing is fail-open and has now returned, before framebuffer I/O. */
    post_code(M61_POST_EARLY_CONTAINMENT_SERIAL);
}

bool m61_post_heap_init(void) {
    const bool result = __real_heap_init();

    if (result) {
        /* PMM and VMM are already prerequisites at this call boundary. */
        post_code(M61_POST_MEMORY_RUNTIME);
    }
    return result;
}

bool m61_post_irq_init(void) {
    const bool result = __real_irq_init();

    if (result) {
        /* Normal exception setup precedes the runtime IRQ foundation. */
        post_code(M61_POST_EXCEPTION_IRQ);
    }
    return result;
}

bool m61_post_usb_mass_storage_init(struct xhci_state *state) {
    const bool result = __real_usb_mass_storage_init(state);

    if (result) {
        /* xHCI discovery/configuration precedes successful USB storage. */
        post_code(M61_POST_USB_STORAGE);
    }
    return result;
}

enum vfs_result m61_post_boringfs_vfs_create_writable(
    const struct block_device *device, uint64_t id,
    struct boringfs_vfs **out, struct boringfs_validation_error *error) {
    const enum vfs_result result = __real_boringfs_vfs_create_writable(
        device, id, out, error);

    if (result == VFS_RESULT_OK) {
        post_code(M61_POST_USB_ROOT);
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
        post_code(M61_POST_BORING_INIT);
    } else if (!display_posted && name_ends_with(name, "boring-display")) {
        display_posted = true;
        post_code(M61_POST_BORING_DISPLAY);
    } else if (!wm_posted && name_ends_with(name, "boringwm")) {
        wm_posted = true;
        post_code(M61_POST_BORING_WM);
    } else if (!terminal_posted && name_ends_with(name, "boring-terminal")) {
        terminal_posted = true;
        post_code(M61_POST_AUTO_TERMINAL);
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
        post_code(M61_POST_DESKTOP_PRESENT);
    }
    return result;
}
EOF_POST_C

# QEMU can cheaply observe legacy port 0x80 through isa-debugcon. Patch only
# this job's checkout; the repository's established two-boot acceptance remains
# the harness and the change is idempotent across the early-fault/normal relink.
python3 - <<'EOF_POST_QEMU'
from pathlib import Path

path = Path("tests/m61-bootable-persistent-usb-qemu.py")
text = path.read_text()
marker = "# M61_POST80_QEMU_CAPTURE"
if marker not in text:
    old = '    qemu_log = qemu_log_path.open("w")\n'
    new = (
        '    qemu_log = qemu_log_path.open("w")\n'
        '    # M61_POST80_QEMU_CAPTURE\n'
        '    post80 = out / "post80.bin"\n'
        '    post80.write_bytes(b"")\n')
    if text.count(old) != 1:
        raise RuntimeError("M61 POST QEMU patch: qemu-log anchor mismatch")
    text = text.replace(old, new, 1)

    old = '        "-serial", f"file:{serial}", "-monitor", "none", "-qmp", "stdio",\n'
    new = (
        '        "-serial", f"file:{serial}", "-monitor", "none", "-qmp", "stdio",\n'
        '        "-global", "isa-debugcon.iobase=0x80",\n'
        '        "-debugcon", f"file:{post80}",\n')
    if text.count(old) != 1:
        raise RuntimeError("M61 POST QEMU patch: command anchor mismatch")
    text = text.replace(old, new, 1)

    old = '''    def latest(count=None, after=0):
        return wait(lambda _current: next((frame for frame in reversed(frames())
                                           if frame["frame"] > after and
                                           (count is None or frame["count"] == count)), None),
                    f"WM frame count={count} after={after}")
'''
    new = '''    def latest(count=None, after=0):
        frame = wait(lambda _current: next((item for item in reversed(frames())
                                            if item["frame"] > after and
                                            (count is None or item["count"] == count)), None),
                     f"WM frame count={count} after={after}")
        if count == 1:
            expected_post = bytes((
                0x61, 0x62, 0x63, 0x64, 0x65, 0x66,
                0x67, 0x68, 0x69, 0x6a, 0x6f))
            deadline = time.monotonic() + 5
            while True:
                captured = post80.read_bytes()
                offset = captured.find(expected_post)
                if offset >= 0:
                    (out / "post80-proof.txt").write_text(
                        "POST_PORT80_PROVEN=YES\\n"
                        "POST_SEQUENCE=61,62,63,64,65,66,67,68,69,6A,6F\\n"
                        f"post_sequence_offset={offset}\\n")
                    print("M61 POST port 0x80 sequence: PASS 61 62 63 64 65 66 67 68 69 6A 6F")
                    break
                if time.monotonic() >= deadline:
                    raise RuntimeError(
                        "M61 POST port 0x80 sequence missing; tail=" +
                        captured[-64:].hex(" "))
                time.sleep(0.02)
        return frame
'''
    if text.count(old) != 1:
        raise RuntimeError("M61 POST QEMU patch: latest() anchor mismatch")
    text = text.replace(old, new, 1)
    path.write_text(text)
EOF_POST_QEMU

# Redirect only this diagnostic build's original entry and selected __real_*
# boundaries through the generated port-0x80 hook. The normal trace wrappers
# stay intact and continue to own framebuffer/serial behavior.
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
if [ "${M61_EARLY_FAULT_TEST:-0}" = 1 ]; then
    nm build/kernel.elf | grep -Fq 'boring_m61_early_fault_test_enabled'
    printf '%s\n' 'M61 controlled early-fault test kernel: ENABLED'
else
    printf '%s\n' 'M61 safe direct framebuffer boot trace: ENABLED'
fi
printf '%s\n' 'M61 diagnostic POST port 0x80 witness: ENABLED'
