#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

USER_CPPFLAGS='-Iuser/runtime/include -Ikernel/include -DBORING_BOUNDED_DESKTOP_ACCEPTANCE=1 -DBORING_M54_USB_ONLY_DESKTOP=1'
USER_CFLAGS='-Iuser/runtime/include -Ikernel/include -std=c11 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -fno-builtin -fno-asynchronous-unwind-tables -fno-unwind-tables -m64 -mno-red-zone -mno-80387 -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes'

rm -rf build/user
make TEST_MODE=m36-desktop RUNTIME_USER_CPPFLAGS="$USER_CPPFLAGS" \
    boringfsck user-boringfetch user-shell user-boring-terminal user-boringwm \
    user-boring-edit user-cat
mkdir -p build/user/boring-init-desktop
cc $USER_CFLAGS -c user/boring-init/desktop.c \
    -o build/user/boring-init-desktop/main.o
ld -nostdlib -static --build-id=none -z max-page-size=0x1000 \
   -T user/boring-init/linker.ld \
   build/user/runtime/entry.o build/user/runtime/syscall.o \
   build/user/runtime/memory.o build/user/runtime/string.o \
   build/user/runtime/ipc.o build/user/boring-init-desktop/main.o \
   -o build/user/boring-init-desktop.elf

BUILDER=build/boringfs-m39-bundle
cc -Ilibs/boringfs/include -std=c11 -fno-builtin \
   -fno-tree-loop-distribute-patterns -Wall -Wextra -Wpedantic -Werror \
   -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes \
   tests/boringfs-m39-bundle.c libs/boringfs/codec.c libs/boringfs/validate.c \
   -o "$BUILDER"
OUT=build/m57-bundle
rm -rf "$OUT"
mkdir -p "$OUT"
"$BUILDER" "$OUT/boringos-root.img" build/user/boringfetch.elf \
    build/user/boring-terminal.elf build/user/boring-shell.elf \
    build/user/boring-display-wm.elf build/user/boringwm.elf \
    build/user/boring-edit.elf build/user/cat.elf > "$OUT/geometry.txt"
build/boringfsck "$OUT/boringos-root.img" > "$OUT/boringfsck-before.txt"
grep -Fqx 'Status: VALID' "$OUT/boringfsck-before.txt"

rm -rf build/kernel build/iso_root
rm -f build/kernel.elf build/boringos.iso build/.test-mode
make TEST_MODE=m36-desktop \
    TEST_CPPFLAGS='-DBORING_M36_DESKTOP_ACCEPTANCE=1 -DBORING_M37_DESKTOP_ACCEPTANCE=1 -DBORING_M54_USB_ONLY_DESKTOP=1 -DBORING_M57_AHCI_ROOT=1' \
    TEST_HARNESS_C='kernel/core/m37_desktop_test.c kernel/core/m37_desktop_test_adapter.c kernel/core/ahci.c kernel/arch/x86_64/ahci_hw.c' \
    BOOT_USER_ELF=build/user/boring-init-desktop.elf \
    BOOT_USER_NAME=boring-init.elf \
    BOOT_EXTRA_USER_ELF= BOOT_EXTRA_USER_NAME= \
    BOOT_EXTRA2_USER_ELF= BOOT_EXTRA2_USER_NAME= \
    BOOT_EXTRA3_USER_ELF= BOOT_EXTRA4_USER_ELF= \
    BOOT_LIMINE_CONF=limine-m37-desktop.conf build/boringos.iso

printf '%s\n' 'M57 AHCI persistent-root desktop build passed.'
