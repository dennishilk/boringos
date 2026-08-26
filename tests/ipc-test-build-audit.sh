#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ELF="${ROOT}/build/user/ipc-test.elf"

make -C "${ROOT}" user-ipc-test

HEADER=$(readelf -hW "${ELF}")
PROGRAMS=$(readelf -lW "${ELF}")
DYNAMIC=$(readelf -dW "${ELF}")
RELOCS=$(readelf -rW "${ELF}")
SYMBOLS=$(nm -n "${ELF}")
UNDEFINED=$(nm -u "${ELF}" || true)
DISASSEMBLY=$(objdump -d "${ELF}")

printf '%s\n' "${HEADER}" | grep -Fq 'Class:                             ELF64'
printf '%s\n' "${HEADER}" | grep -Fq "Data:                              2's complement, little endian"
printf '%s\n' "${HEADER}" | grep -Fq 'Type:                              EXEC (Executable file)'
printf '%s\n' "${HEADER}" | grep -Fq 'Machine:                           Advanced Micro Devices X86-64'
printf '%s\n' "${HEADER}" | grep -Fq 'Entry point address:               0x40000000'

LOAD_COUNT=$(printf '%s\n' "${PROGRAMS}" | grep -c '^  LOAD')
[ "${LOAD_COUNT}" -eq 3 ] || { echo "expected 3 ipc-test PT_LOAD entries, got ${LOAD_COUNT}" >&2; exit 1; }
if printf '%s\n' "${PROGRAMS}" | grep -Eq '^  (INTERP|DYNAMIC|TLS)'; then
    echo 'unexpected PT_INTERP/PT_DYNAMIC/PT_TLS in ipc-test' >&2
    exit 1
fi
if printf '%s\n' "${PROGRAMS}" | grep '^  LOAD' | grep -Eq ' W E '; then
    echo 'ipc-test contains writable+executable PT_LOAD' >&2
    exit 1
fi
printf '%s\n' "${DYNAMIC}" | grep -Fq 'There is no dynamic section in this file.'
printf '%s\n' "${RELOCS}" | grep -Fq 'There are no relocations in this file.'
[ -z "${UNDEFINED}" ] || { echo 'ipc-test has unresolved symbols:' >&2; printf '%s\n' "${UNDEFINED}" >&2; exit 1; }
for symbol in _start boring_main boring_service_register boring_service_connect boring_service_accept boring_ipc_send boring_ipc_receive boring_ipc_close boring_buffer_create boring_buffer_map boring_buffer_unmap boring_buffer_close boring_exit; do
    printf '%s\n' "${SYMBOLS}" | grep -Eq " [Tt] ${symbol}$" || { echo "missing ipc-test symbol: ${symbol}" >&2; exit 1; }
done
for symbol in boring_service_register boring_service_connect boring_service_accept boring_ipc_send boring_ipc_receive boring_ipc_close; do
    printf '%s\n' "${DISASSEMBLY}" | grep -Eq "call[q]?[[:space:]].*<${symbol}>" || { echo "ipc-test does not call ${symbol}" >&2; exit 1; }
done
if printf '%s\n' "${SYMBOLS}" | grep -Eq '(__libc_start_main|__stack_chk_fail| printf$| snprintf$| malloc$| calloc$| realloc$| free$| fopen$| open$| close$)'; then
    echo 'ipc-test contains a host CRT/libc dependency' >&2
    exit 1
fi
echo 'standalone ipc-test build audit passed.'
printf 'ipc-test size: %s bytes\n' "$(wc -c < "${ELF}")"
printf '%s\n' "${PROGRAMS}" | grep '^  LOAD'
