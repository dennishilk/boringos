#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "${ROOT}"

rm -rf build/user
make TEST_MODE=m36-desktop RUNTIME_USER_CPPFLAGS='-Iuser/runtime/include -Ikernel/include -DBORING_BOUNDED_DESKTOP_ACCEPTANCE=1 -DBORING_M54_USB_ONLY_DESKTOP=1' user-boringwm user-boring-terminal user-boringfetch
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
    TEST_CPPFLAGS='-DBORING_M36_DESKTOP_ACCEPTANCE=1 -DBORING_M37_DESKTOP_ACCEPTANCE=1 -DBORING_M54_USB_ONLY_DESKTOP=1' \
    TEST_HARNESS_C='kernel/core/m37_desktop_test.c kernel/core/m37_desktop_test_adapter.c' \
    BOOT_USER_ELF=build/user/boring-init-desktop.elf \
    BOOT_USER_NAME=boring-init.elf \
    BOOT_EXTRA_USER_ELF= BOOT_EXTRA_USER_NAME= \
    BOOT_EXTRA2_USER_ELF= BOOT_EXTRA2_USER_NAME= \
    BOOT_EXTRA3_USER_ELF= BOOT_EXTRA4_USER_ELF= \
    BOOT_LIMINE_CONF=limine-m37-desktop.conf \
    build/boringos.iso

RAW_STDOUT=build/m37-iso-audit-xorriso.stdout
RAW_STDERR=build/m37-iso-audit-xorriso.stderr
if ! xorriso -indev build/boringos.iso \
    -find /boot/user -type f >"${RAW_STDOUT}" 2>"${RAW_STDERR}"; then
    printf '%s\n' 'M37 ISO audit: xorriso file listing failed.' >&2
    printf '%s\n' '--- xorriso stdout ---' >&2
    cat "${RAW_STDOUT}" >&2
    printf '%s\n' '--- xorriso stderr ---' >&2
    cat "${RAW_STDERR}" >&2
    exit 1
fi
printf '%s\n' 'M37 ISO audit: raw xorriso stdout:'
cat "${RAW_STDOUT}"
printf '%s\n' 'M37 ISO audit: raw xorriso stderr:'
cat "${RAW_STDERR}"

AUDIT_DIR=build/m37-iso-audit
rm -rf "${AUDIT_DIR}"
mkdir -p "${AUDIT_DIR}"
if ! xorriso -osirrox on -indev build/boringos.iso \
    -extract /boot/user "${AUDIT_DIR}" \
    >build/m37-iso-audit-extract.stdout \
    2>build/m37-iso-audit-extract.stderr; then
    printf '%s\n' 'M37 ISO audit: extracting /boot/user from ISO failed.' >&2
    cat build/m37-iso-audit-extract.stdout >&2
    cat build/m37-iso-audit-extract.stderr >&2
    exit 1
fi

FILES=$(find "${AUDIT_DIR}" -type f -printf '/boot/user/%P\n' | LC_ALL=C sort -u)
printf '%s\n' 'M37 ISO audit: normalized /boot/user files:'
if [ -n "${FILES}" ]; then
    printf '%s\n' "${FILES}"
else
    printf '%s\n' '<none>'
fi
if [ "${FILES}" != '/boot/user/boring-init.elf' ]; then
    printf '%s\n' 'M37 ISO audit FAILED: expected exactly /boot/user/boring-init.elf.' >&2
    exit 1
fi
printf '%s\n' 'M37 ISO audit: exactly one userspace boot module verified.'
printf '%s\n' 'M37 desktop init audit and one-module ISO build passed.'
