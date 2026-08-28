#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "${ROOT}"

make TEST_MODE=m36-desktop user-boringwm user-boring-terminal user-boringfetch
mkdir -p build/user/boring-init-desktop
cc -Iuser/runtime/include -Ikernel/include -std=c11 -ffreestanding \
   -fno-stack-protector -fno-pic -fno-pie -fno-builtin \
   -fno-asynchronous-unwind-tables -fno-unwind-tables -m64 -mno-red-zone \
   -mno-80387 -mno-mmx -mno-sse -mno-sse2 -O2 \
   -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow \
   -Wstrict-prototypes -Wmissing-prototypes \
   -c user/boring-init/desktop.c -o build/user/boring-init-desktop/main.o
ld -nostdlib -static --build-id=none -z max-page-size=0x1000 \
   -T user/boring-init/linker.ld \
   build/user/runtime/entry.o build/user/runtime/syscall.o \
   build/user/runtime/memory.o build/user/runtime/string.o \
   build/user/runtime/ipc.o build/user/boring-init-desktop/main.o \
   -o build/user/boring-init-desktop.elf

readelf -h build/user/boring-init-desktop.elf | grep -Fq 'Class:                             ELF64'
readelf -h build/user/boring-init-desktop.elf | grep -Fq 'Machine:                           Advanced Micro Devices X86-64'
if readelf -l build/user/boring-init-desktop.elf | grep -Fq INTERP; then
    printf '%s\n' 'M37 desktop init unexpectedly has PT_INTERP' >&2
    exit 1
fi
if readelf -d build/user/boring-init-desktop.elf 2>/dev/null | grep -Eq 'NEEDED|Shared library'; then
    printf '%s\n' 'M37 desktop init unexpectedly has dynamic dependencies' >&2
    exit 1
fi

rm -rf build/kernel build/iso_root
rm -f build/kernel.elf build/boringos.iso build/.test-mode
make TEST_MODE=m36-desktop \
    TEST_CPPFLAGS='-DBORING_M36_DESKTOP_ACCEPTANCE=1 -DBORING_M37_DESKTOP_ACCEPTANCE=1' \
    TEST_HARNESS_C='kernel/core/m37_desktop_test.c kernel/core/m37_desktop_test_adapter.c' \
    BOOT_USER_ELF=build/user/boring-init-desktop.elf \
    BOOT_USER_NAME=boring-init.elf \
    BOOT_EXTRA_USER_ELF= BOOT_EXTRA_USER_NAME= \
    BOOT_EXTRA2_USER_ELF= BOOT_EXTRA2_USER_NAME= \
    BOOT_EXTRA3_USER_ELF= BOOT_EXTRA4_USER_ELF= \
    BOOT_LIMINE_CONF=limine-m37-desktop.conf \
    build/boringos.iso

FILES=$(xorriso -indev build/boringos.iso -find /boot/user -type f -print 2>/dev/null | \
    sed -n 's/^.*\(\/boot\/user\/[^ ]*\).*$/\1/p' | sort -u)
[ "${FILES}" = '/boot/user/boring-init.elf' ]
printf '%s\n' 'M37 desktop init audit and one-module ISO build passed.'
