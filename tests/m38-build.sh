#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "${ROOT}"

USER_CFLAGS='-Iuser/runtime/include -Ikernel/include -std=c11 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -fno-builtin -fno-asynchronous-unwind-tables -fno-unwind-tables -m64 -mno-red-zone -mno-80387 -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes'
USER_LDFLAGS='-nostdlib -static --build-id=none -z max-page-size=0x1000 -T user/memory-test/linker.ld'

make TEST_MODE=m36-desktop boringfsck user-boringfetch user-shell \
    user-boring-terminal user-boringwm

mkdir -p build/user/boring-init-desktop build/user/boringwm-m38 \
    build/user/boring-display-m38
cc ${USER_CFLAGS} -c user/boring-init/desktop.c \
    -o build/user/boring-init-desktop/main.o
ld -nostdlib -static --build-id=none -z max-page-size=0x1000 \
   -T user/boring-init/linker.ld \
   build/user/runtime/entry.o build/user/runtime/syscall.o \
   build/user/runtime/memory.o build/user/runtime/string.o \
   build/user/runtime/ipc.o build/user/boring-init-desktop/main.o \
   -o build/user/boring-init-desktop.elf

cc ${USER_CFLAGS} -DBORING_BOUNDED_DESKTOP_ACCEPTANCE=1 \
    -DBORING_M38_WM_DEATH_ACCEPTANCE \
    -c user/boringwm/main.c -o build/user/boringwm-m38/main.o
ld ${USER_LDFLAGS} \
    build/user/runtime/entry.o build/user/runtime/syscall.o \
    build/user/runtime/memory.o build/user/runtime/string.o \
    build/user/runtime/ipc.o build/user/runtime/event.o \
    build/user/boringwm/core.o build/user/boringwm-m38/main.o \
    -o build/user/boringwm-m38-death.elf

cc ${USER_CFLAGS} -DBORING_BOUNDED_DESKTOP_ACCEPTANCE=1 \
    -DBORING_M38_DISPLAY_DEATH_ACCEPTANCE \
    -c user/boring-display/server.c -o build/user/boring-display-m38/server.o
ld ${USER_LDFLAGS} \
    build/user/runtime/entry.o build/user/runtime/syscall.o \
    build/user/runtime/memory.o build/user/runtime/string.o \
    build/user/runtime/ipc.o build/user/runtime/event.o \
    build/user/runtime/display.o build/user/boring-display/core.o \
    build/user/boring-display/managed.o build/user/boring-display/wallpaper.o \
    build/user/boring-display-m38/server.o \
    -o build/user/boring-display-m38-death.elf

for elf in build/user/boring-init-desktop.elf \
           build/user/boringwm-m38-death.elf \
           build/user/boring-display-m38-death.elf; do
    readelf -h "${elf}" | grep -Fq 'Class:                             ELF64'
    readelf -h "${elf}" | grep -Fq 'Machine:                           Advanced Micro Devices X86-64'
    if readelf -l "${elf}" | grep -Fq INTERP; then
        printf '%s\n' "M38 audit FAILED: ${elf} has PT_INTERP" >&2
        exit 1
    fi
    if readelf -d "${elf}" 2>/dev/null | grep -Eq 'NEEDED|Shared library'; then
        printf '%s\n' "M38 audit FAILED: ${elf} has dynamic dependencies" >&2
        exit 1
    fi
done

BUILDER=build/boringfs-m38-bundle
cc -Ilibs/boringfs/include -std=c11 -fno-builtin \
   -fno-tree-loop-distribute-patterns -Wall -Wextra -Wpedantic -Werror \
   -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes \
   tests/boringfs-m37-bundle.c libs/boringfs/codec.c libs/boringfs/validate.c \
   -o "${BUILDER}"

bundle() {
    name=$1
    display_elf=$2
    wm_elf=$3
    out="build/m38-bundle-${name}"
    image="${out}/boringos-root.img"
    rm -rf "${out}"
    mkdir -p "${out}"
    "${BUILDER}" "${image}" build/user/boringfetch.elf \
        build/user/boring-terminal.elf build/user/boring-shell.elf \
        "${display_elf}" "${wm_elf}" > "${out}/geometry.raw"
    sed 's/^M37 /M38 /' "${out}/geometry.raw" | tee "${out}/geometry.txt"
    build/boringfsck "${image}" | tee "${out}/boringfsck.txt" | \
        grep -Fqx 'Status: VALID'
    blocks=$(awk '/^M38 BoringFS measured geometry:/ {print $5}' "${out}/geometry.txt")
    lower=$(awk '/^M38 lower bound:/ {print $4}' "${out}/geometry.txt")
    case "${blocks}" in ''|*[!0-9]*) exit 1 ;; esac
    case "${lower}" in ''|*[!0-9]*) exit 1 ;; esac
    [ $((lower + 1)) -eq "${blocks}" ]
    [ "$(stat -c %s "${image}")" -eq $((blocks * 4096)) ]
    grep -Fqx "Blocks: ${blocks}" "${out}/boringfsck.txt"

    for program in boringfetch boring-terminal boring-shell boring-display boringwm; do
        build/boringfsck --cat "/bin/${program}" "${image}" > \
            "${out}/${program}.extracted"
        case "${program}" in
            boring-display) source=${display_elf} ;;
            boringwm) source=${wm_elf} ;;
            *) source="build/user/${program}.elf" ;;
        esac
        cmp "${source}" "${out}/${program}.extracted"
    done
    sha256sum "${image}" build/user/boringfetch.elf \
        build/user/boring-terminal.elf build/user/boring-shell.elf \
        "${display_elf}" "${wm_elf}" > "${out}/SHA256SUMS"
    printf '%s\n' "M38 ${name} minimal BoringFS desktop bundle passed."
}

bundle normal build/user/boring-display-wm.elf build/user/boringwm.elf
bundle wm-first build/user/boring-display-wm.elf build/user/boringwm-m38-death.elf
bundle display-first build/user/boring-display-m38-death.elf build/user/boringwm.elf

rm -rf build/kernel build/iso_root
rm -f build/kernel.elf build/boringos.iso build/.test-mode
make TEST_MODE=m36-desktop \
    TEST_CPPFLAGS='-DBORING_M36_DESKTOP_ACCEPTANCE=1 -DBORING_M38_DESKTOP_ACCEPTANCE=1' \
    TEST_HARNESS_C='kernel/core/m38_desktop_test.c kernel/core/m38_desktop_test_adapter.c' \
    BOOT_USER_ELF=build/user/boring-init-desktop.elf \
    BOOT_USER_NAME=boring-init.elf \
    BOOT_EXTRA_USER_ELF= BOOT_EXTRA_USER_NAME= \
    BOOT_EXTRA2_USER_ELF= BOOT_EXTRA2_USER_NAME= \
    BOOT_EXTRA3_USER_ELF= BOOT_EXTRA4_USER_ELF= \
    BOOT_LIMINE_CONF=limine-m37-desktop.conf \
    build/boringos.iso

RAW_STDOUT=build/m38-iso-audit-xorriso.stdout
RAW_STDERR=build/m38-iso-audit-xorriso.stderr
if ! xorriso -indev build/boringos.iso -find /boot/user -type f \
    >"${RAW_STDOUT}" 2>"${RAW_STDERR}"; then
    cat "${RAW_STDOUT}" >&2
    cat "${RAW_STDERR}" >&2
    exit 1
fi
AUDIT_DIR=build/m38-iso-audit
rm -rf "${AUDIT_DIR}"
mkdir -p "${AUDIT_DIR}"
xorriso -osirrox on -indev build/boringos.iso -extract /boot/user "${AUDIT_DIR}" \
    >build/m38-iso-audit-extract.stdout 2>build/m38-iso-audit-extract.stderr
FILES=$(find "${AUDIT_DIR}" -type f -printf '/boot/user/%P\n' | LC_ALL=C sort -u)
if [ "${FILES}" != '/boot/user/boring-init.elf' ]; then
    printf '%s\n' 'M38 ISO audit FAILED: expected exactly /boot/user/boring-init.elf.' >&2
    printf '%s\n' "${FILES}" >&2
    exit 1
fi
printf '%s\n' 'M38 ISO audit: exactly one userspace boot module verified.'
sha256sum build/boringos.iso build/user/boring-init-desktop.elf > \
    build/m38-iso-SHA256SUMS
printf '%s\n' 'M38 supervision images and one-module ISO build passed.'
