#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

USER_CPPFLAGS='-Iuser/runtime/include -Ikernel/include -DBORING_M54_USB_ONLY_DESKTOP=1 -DBORING_M61_PHYSICAL_BREADCRUMBS=1'
USER_CFLAGS='-Iuser/runtime/include -Ikernel/include -std=c11 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -fno-builtin -fno-asynchronous-unwind-tables -fno-unwind-tables -m64 -mno-red-zone -mno-80387 -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes'
HOST_CFLAGS='-std=c11 -fno-builtin -fno-tree-loop-distribute-patterns -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes'

rm -rf build/user
make TEST_MODE=m36-desktop RUNTIME_USER_CPPFLAGS="$USER_CPPFLAGS" \
    boringfsck user-boringfetch user-shell user-boring-terminal user-boringwm \
    user-boring-edit user-cat

python3 tests/m61-empty-desktop-verifier.py
strings build/user/boringwm.elf | \
    grep -Fqx 'wm: Super+Return spawned /bin/boring-terminal'
if strings build/user/boringwm.elf | \
    grep -Fq 'M61 PHYSICAL: automatic terminal spawn'; then
    echo 'M61 build FAILED: automatic terminal spawn string remains' >&2
    exit 1
fi
printf '%s\n' 'M61 exact BoringWM ELF empty desktop/manual launcher: PASS'

mkdir -p build/user/boring-init-desktop
cc $USER_CFLAGS -c user/boring-init/desktop.c \
    -o build/user/boring-init-desktop/main.o
ld -nostdlib -static --build-id=none -z max-page-size=0x1000 \
   -T user/boring-init/linker.ld \
   build/user/runtime/entry.o build/user/runtime/syscall.o \
   build/user/runtime/memory.o build/user/runtime/string.o \
   build/user/runtime/ipc.o build/user/boring-init-desktop/main.o \
   -o build/user/boring-init-desktop.elf

BUILDER=build/boringfs-m61-bundle
cc -Ilibs/boringfs/include $HOST_CFLAGS \
   tests/boringfs-m39-bundle.c libs/boringfs/codec.c libs/boringfs/validate.c \
   -o "$BUILDER"
OUT=build/m61-bundle
rm -rf "$OUT"
mkdir -p "$OUT"
"$BUILDER" "$OUT/boringos-root.img" build/user/boringfetch.elf \
    build/user/boring-terminal.elf build/user/boring-shell.elf \
    build/user/boring-display-wm.elf build/user/boringwm.elf \
    build/user/boring-edit.elf build/user/cat.elf > "$OUT/geometry.txt"
build/boringfsck "$OUT/boringos-root.img" > "$OUT/boringfsck-before.txt"
grep -Fqx 'Status: VALID' "$OUT/boringfsck-before.txt"
build/boringfsck --cat /bin/boring-terminal "$OUT/boringos-root.img" \
    > "$OUT/boring-terminal.embedded.elf"
build/boringfsck --cat /bin/boring-shell "$OUT/boringos-root.img" \
    > "$OUT/boring-shell.embedded.elf"
test -s "$OUT/boring-terminal.embedded.elf"
test -s "$OUT/boring-shell.embedded.elf"
cmp -s build/user/boring-terminal.elf "$OUT/boring-terminal.embedded.elf"
cmp -s build/user/boring-shell.elf "$OUT/boring-shell.embedded.elf"
TERMINAL_ELF_MAGIC=$(od -An -tx1 -N4 "$OUT/boring-terminal.embedded.elf" | tr -d ' \n')
SHELL_ELF_MAGIC=$(od -An -tx1 -N4 "$OUT/boring-shell.embedded.elf" | tr -d ' \n')
test "$TERMINAL_ELF_MAGIC" = 7f454c46
test "$SHELL_ELF_MAGIC" = 7f454c46
printf '%s\n' 'M61 BoringFS /bin/boring-terminal + /bin/boring-shell ELF identity: PASS'
if build/boringfsck --cat /persist/m61.txt "$OUT/boringos-root.img" \
    >"$OUT/preseed-check.stdout" 2>"$OUT/preseed-check.stderr"; then
    echo 'M61 build FAILED: /persist/m61.txt is pre-seeded' >&2
    exit 1
fi
printf '%s\n' 'M61 fresh BoringFS image: /persist/m61.txt absent: PASS'

cc -Ikernel/include $HOST_CFLAGS -DBORING_SERIAL_TEST=1 \
   tests/m61-serial-failopen-host-test.c kernel/arch/x86_64/serial.c \
   -o build/m61-serial-failopen-host-test
build/m61-serial-failopen-host-test | tee "$OUT/serial-failopen-host.txt"

python3 tests/m61-boot-console-safety.py | \
    tee "$OUT/boot-console-safety.txt"
make framebuffer-host-test | tee "$OUT/framebuffer-host.txt"
make vmm-framebuffer-host-test | tee "$OUT/vmm-framebuffer-host.txt"
make boot-console-host-test | tee "$OUT/boot-console-host.txt"
cc -Ikernel/include $HOST_CFLAGS -DBORING_BOOT_CONSOLE_TEST=1 \
   -c kernel/core/boot_console.c -o "$OUT/boot-console-dependency-audit.o"
if nm -u "$OUT/boot-console-dependency-audit.o" | \
   grep -Eq '[[:space:]](malloc|calloc|realloc|free|heap_[A-Za-z0-9_]*)$'; then
    echo 'M61 boot console FAILED: early history has a heap dependency' >&2
    exit 1
fi
printf '%s\n' 'M61 boot console bounded static history/no-heap audit: PASS'

cc -Ikernel/include $HOST_CFLAGS tests/m61-block-slice-host-test.c \
   kernel/core/block_device.c kernel/core/block_slice.c \
   -o build/m61-block-slice-host-test
build/m61-block-slice-host-test | tee "$OUT/block-slice-host.txt"

MALFORMED="$OUT/malformed-root.img"
cp "$OUT/boringos-root.img" "$MALFORMED"
dd if=/dev/zero of="$MALFORMED" bs=4096 count=1 conv=notrunc status=none
if build/boringfsck "$MALFORMED" >"$OUT/malformed.stdout" 2>"$OUT/malformed.stderr"; then
    echo 'M61 build FAILED: malformed BoringFS root accepted' >&2
    exit 1
fi
rm -f "$MALFORMED"
printf '%s\n' 'M61 malformed/absent expected BoringFS root: REJECTED'

rm -rf build/kernel build/iso_root
rm -f build/kernel.elf build/boringos.iso build/.test-mode
make TEST_MODE=m36-desktop \
    TEST_CPPFLAGS='-DBORING_M36_DESKTOP_ACCEPTANCE=1 -DBORING_M37_DESKTOP_ACCEPTANCE=1 -DBORING_M54_USB_ONLY_DESKTOP=1 -DBORING_M61_PHYSICAL_BREADCRUMBS=1' \
    TEST_HARNESS_C='kernel/core/m61_desktop_test.c kernel/core/m37_desktop_test_adapter.c kernel/core/block_slice.c kernel/core/xhci_mixed.c kernel/arch/x86_64/xhci_mixed.c kernel/core/usb_mass_storage.c kernel/core/m61_physical_breadcrumbs.c' \
    LD='ld --wrap=serial_init --wrap=boring_cpu_inventory_init --wrap=boring_pci_inventory_init --wrap=boring_smbios_boot_init --wrap=pmm_init --wrap=vmm_init --wrap=heap_init --wrap=exception_init --wrap=syscall_test_run --wrap=boring_input_init --wrap=irq_init --wrap=timer_init --wrap=xhci_init --wrap=xhci_address_connected --wrap=xhci_discover_descriptors --wrap=xhci_configure_hid_devices_mixed --wrap=usb_mass_storage_init --wrap=boringfs_vfs_create_writable --wrap=process_set_name --wrap=boring_framebuffer_user_claim --wrap=boring_framebuffer_user_present --wrap=boring_ipc_service_register --wrap=x86_64_exception_dispatch' \
    BOOT_USER_ELF=build/user/boring-init-desktop.elf \
    BOOT_USER_NAME=boring-init.elf \
    BOOT_EXTRA_USER_ELF= BOOT_EXTRA_USER_NAME= \
    BOOT_EXTRA2_USER_ELF= BOOT_EXTRA2_USER_NAME= \
    BOOT_EXTRA3_USER_ELF= BOOT_EXTRA4_USER_ELF= \
    BOOT_LIMINE_CONF=limine-m61-usb.conf \
    build/kernel.elf build/deps/limine-binary/limine

nm build/kernel.elf | grep -Fq 'boring_m61_physical_breadcrumbs_enabled'
python3 tests/m61-display-manager-reply-gap.py
python3 tests/m61-runtime-hid-bisector.py
cc -Ikernel/include $HOST_CFLAGS -ffunction-sections -fdata-sections \
   tests/m61-hid-report-completion-host.c kernel/core/xhci.c \
   -Wl,--gc-sections -o build/m61-hid-report-completion-host
build/m61-hid-report-completion-host
grep -Fqx 'timeout: 5' limine-m61-usb.conf
grep -Fqx 'mouse: no' limine-m61-usb.conf
printf '%s\n' 'M61 physical framebuffer breadcrumbs: ENABLED'
printf '%s\n' 'M61 display-manager reply POST C4-C8: ENABLED'
printf '%s\n' 'M61 physical empty desktop witness: ENABLED'
printf '%s\n' 'M61 USB Limine bounded autoboot: 5 seconds, mouse countdown cancellation disabled'
printf '%s\n' 'M61 USB-root desktop build passed.'
