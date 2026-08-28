#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "${ROOT}"

USER_CFLAGS='-Iuser/runtime/include -Ikernel/include -std=c11 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -fno-builtin -fno-asynchronous-unwind-tables -fno-unwind-tables -m64 -mno-red-zone -mno-80387 -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes'

make TEST_MODE=m36-desktop boringfsck user-boringfetch user-shell \
    user-boring-terminal user-boringwm user-boring-edit user-cat

mkdir -p build/user/boring-init-desktop
cc ${USER_CFLAGS} -c user/boring-init/desktop.c \
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
   -o "${BUILDER}"

out=build/m39-bundle
mkdir -p "${out}"
"${BUILDER}" "${out}/boringos-root.img" build/user/boringfetch.elf \
    build/user/boring-terminal.elf build/user/boring-shell.elf \
    build/user/boring-display-wm.elf build/user/boringwm.elf \
    build/user/boring-edit.elf build/user/cat.elf > "${out}/geometry.txt"
build/boringfsck "${out}/boringos-root.img" > "${out}/boringfsck.txt"
grep -Fqx 'Status: VALID' "${out}/boringfsck.txt"
for program in boringfetch boring-terminal boring-shell boring-display boringwm boring-edit cat; do
    build/boringfsck --cat "/bin/${program}" "${out}/boringos-root.img" > "${out}/${program}.extracted"
    source="build/user/${program}.elf"
    if [ "${program}" = boring-display ]; then source=build/user/boring-display-wm.elf; fi
    cmp "${source}" "${out}/${program}.extracted"
done
sh tests/edit-build-audit.sh

rm -rf build/kernel build/iso_root
rm -f build/kernel.elf build/boringos.iso build/.test-mode
make TEST_MODE=m36-desktop \
    TEST_CPPFLAGS='-DBORING_M36_DESKTOP_ACCEPTANCE=1 -DBORING_M38_DESKTOP_ACCEPTANCE=1 -DBORING_M39_EDIT_ACCEPTANCE=1' \
    TEST_HARNESS_C='kernel/core/m38_desktop_test.c kernel/core/m38_desktop_test_adapter.c' \
    BOOT_USER_ELF=build/user/boring-init-desktop.elf \
    BOOT_USER_NAME=boring-init.elf \
    BOOT_EXTRA_USER_ELF= BOOT_EXTRA_USER_NAME= \
    BOOT_EXTRA2_USER_ELF= BOOT_EXTRA2_USER_NAME= \
    BOOT_EXTRA3_USER_ELF= BOOT_EXTRA4_USER_ELF= \
    BOOT_LIMINE_CONF=limine-m37-desktop.conf \
    build/boringos.iso

RAW_STDOUT=build/m39-iso-audit-xorriso.stdout
RAW_STDERR=build/m39-iso-audit-xorriso.stderr
if ! xorriso -indev build/boringos.iso -find /boot/user -type f \
    >"${RAW_STDOUT}" 2>"${RAW_STDERR}"; then
    cat "${RAW_STDOUT}" >&2
    cat "${RAW_STDERR}" >&2
    exit 1
fi
AUDIT_DIR=build/m39-iso-audit
rm -rf "${AUDIT_DIR}"
mkdir -p "${AUDIT_DIR}"
xorriso -osirrox on -indev build/boringos.iso -extract /boot/user "${AUDIT_DIR}" \
    >build/m39-iso-audit-extract.stdout 2>build/m39-iso-audit-extract.stderr
FILES=$(find "${AUDIT_DIR}" -type f -printf '/boot/user/%P\n' | LC_ALL=C sort -u)
if [ "${FILES}" != '/boot/user/boring-init.elf' ]; then
    printf '%s\n' 'M39 ISO audit FAILED: expected exactly /boot/user/boring-init.elf.' >&2
    printf '%s\n' "${FILES}" >&2
    exit 1
fi
printf '%s\n' 'M39 ISO audit: exactly one userspace boot module verified.'
sha256sum build/boringos.iso build/user/boring-init-desktop.elf > \
    build/m39-iso-SHA256SUMS
printf '%s\n' 'M39 writable editor image and one-module ISO build passed.'
