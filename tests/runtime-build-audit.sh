#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ELF="${ROOT}/build/user/runtime-smoke.elf"

make -C "${ROOT}" user-runtime

HEADER=$(readelf -hW "${ELF}")
PROGRAMS=$(readelf -lW "${ELF}")
SECTIONS=$(readelf -SW "${ELF}")
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
    echo "expected exactly 3 PT_LOAD entries, got ${LOAD_COUNT}" >&2
    exit 1
fi

if printf '%s\n' "${PROGRAMS}" | grep -Eq '^  (INTERP|DYNAMIC|TLS)'; then
    echo 'unexpected PT_INTERP/PT_DYNAMIC/PT_TLS in runtime smoke executable' >&2
    exit 1
fi

TEXT_LINE=$(printf '%s\n' "${PROGRAMS}" | grep '^  LOAD' | grep '0x0000000040000000')
RODATA_LINE=$(printf '%s\n' "${PROGRAMS}" | grep '^  LOAD' | grep '0x0000000040001000')
DATA_LINE=$(printf '%s\n' "${PROGRAMS}" | grep '^  LOAD' | grep '0x0000000040002000')

printf '%s\n' "${TEXT_LINE}" | grep -Eq ' R E +0x1000$'
printf '%s\n' "${RODATA_LINE}" | grep -Eq ' R +0x1000$'
printf '%s\n' "${DATA_LINE}" | grep -Eq ' RW +0x1000$'

if printf '%s\n' "${PROGRAMS}" | grep '^  LOAD' | grep -Eq ' W E '; then
    echo 'runtime smoke contains a writable+executable PT_LOAD' >&2
    exit 1
fi

DATA_FILESZ=$(printf '%s\n' "${DATA_LINE}" | awk '{print $5}')
DATA_MEMSZ=$(printf '%s\n' "${DATA_LINE}" | awk '{print $6}')
DATA_FILESZ_DEC=$(printf '%d' "${DATA_FILESZ}")
DATA_MEMSZ_DEC=$(printf '%d' "${DATA_MEMSZ}")
if [ "${DATA_FILESZ_DEC}" -ge "${DATA_MEMSZ_DEC}" ]; then
    echo "runtime data PT_LOAD does not contain real BSS: filesz=${DATA_FILESZ} memsz=${DATA_MEMSZ}" >&2
    exit 1
fi

printf '%s\n' "${SECTIONS}" | grep -Eq '\.data +PROGBITS'
printf '%s\n' "${SECTIONS}" | grep -Eq '\.bss +NOBITS'
if printf '%s\n' "${SECTIONS}" | grep -Eq '\.(tdata|tbss) '; then
    echo 'unexpected TLS section in runtime smoke executable' >&2
    exit 1
fi

if printf '%s\n' "${DYNAMIC}" | grep -Fq '(NEEDED)'; then
    echo 'runtime smoke has a dynamic library dependency' >&2
    exit 1
fi
printf '%s\n' "${DYNAMIC}" | grep -Fq 'There is no dynamic section in this file.'
printf '%s\n' "${RELOCS}" | grep -Fq 'There are no relocations in this file.'

if [ -n "${UNDEFINED}" ]; then
    echo 'runtime smoke has unresolved external symbols:' >&2
    printf '%s\n' "${UNDEFINED}" >&2
    exit 1
fi

for symbol in _start boring_main boring_getpid boring_debug_write boring_memcpy boring_memset boring_strlen; do
    if ! printf '%s\n' "${SYMBOLS}" | grep -Eq " [Tt] ${symbol}$"; then
        echo "missing required runtime symbol: ${symbol}" >&2
        exit 1
    fi
done

if printf '%s\n' "${SYMBOLS}" | grep -Eq '(__libc_start_main|__stack_chk_fail|(^| )_init$|(^| )_fini$| printf$| snprintf$| malloc$| free$)'; then
    echo 'unexpected host CRT/libc-style symbol in runtime smoke executable' >&2
    exit 1
fi

printf '%s\n' 'Native runtime smoke build audit passed.'
printf 'Runtime smoke size: %s bytes\n' "$(wc -c < "${ELF}")"
printf '%s\n' "${TEXT_LINE}"
printf '%s\n' "${RODATA_LINE}"
printf '%s\n' "${DATA_LINE}"
