#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ELF="${ROOT}/build/user/boringfetch.elf"

make -C "${ROOT}" user-boringfetch

HEADER=$(readelf -hW "${ELF}")
PROGRAMS=$(readelf -lW "${ELF}")
DYNAMIC=$(readelf -dW "${ELF}")
RELOCS=$(readelf -rW "${ELF}")
SYMBOLS=$(nm -n "${ELF}")
UNDEFINED=$(nm -u "${ELF}" || true)

printf '%s\n' "${HEADER}" | grep -Fq 'Class:                             ELF64'
printf '%s\n' "${HEADER}" | grep -Fq "Data:                              2's complement, little endian"
printf '%s\n' "${HEADER}" | grep -Fq 'Type:                              EXEC (Executable file)'
printf '%s\n' "${HEADER}" | grep -Fq 'Machine:                           Advanced Micro Devices X86-64'
printf '%s\n' "${HEADER}" | grep -Fq 'Entry point address:               0x40000000'

LOAD_COUNT=$(printf '%s\n' "${PROGRAMS}" | grep -c '^  LOAD')
if [ "${LOAD_COUNT}" -ne 3 ]; then
    echo "expected exactly 3 boringfetch PT_LOAD entries, got ${LOAD_COUNT}" >&2
    exit 1
fi
if printf '%s\n' "${PROGRAMS}" | grep -Eq '^  (INTERP|DYNAMIC|TLS)'; then
    echo 'unexpected PT_INTERP/PT_DYNAMIC/PT_TLS in boringfetch' >&2
    exit 1
fi
if printf '%s\n' "${PROGRAMS}" | grep '^  LOAD' | grep -Eq ' W E '; then
    echo 'boringfetch contains a writable+executable PT_LOAD' >&2
    exit 1
fi
printf '%s\n' "${DYNAMIC}" | grep -Fq 'There is no dynamic section in this file.'
printf '%s\n' "${RELOCS}" | grep -Fq 'There are no relocations in this file.'
if [ -n "${UNDEFINED}" ]; then
    echo 'boringfetch has unresolved external symbols:' >&2
    printf '%s\n' "${UNDEFINED}" >&2
    exit 1
fi
for symbol in _start boring_main boring_console_write boring_system_info boring_exit; do
    if ! printf '%s\n' "${SYMBOLS}" | grep -Eq " [Tt] ${symbol}$"; then
        echo "missing required boringfetch symbol: ${symbol}" >&2
        exit 1
    fi
done
if printf '%s\n' "${SYMBOLS}" | grep -Eq '(__libc_start_main|__stack_chk_fail| printf$| snprintf$| malloc$| free$| fopen$| open$| close$)'; then
    echo 'unexpected host CRT/libc/file-I/O symbol in boringfetch' >&2
    exit 1
fi
echo 'standalone boringfetch build audit passed.'
printf 'boringfetch size: %s bytes\n' "$(wc -c < "${ELF}")"
printf '%s\n' "${PROGRAMS}" | grep '^  LOAD'
