#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ELF="${ROOT}/build/user/boring-shell.elf"

make -C "${ROOT}" user-shell

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
    echo "expected exactly 3 boring-shell PT_LOAD entries, got ${LOAD_COUNT}" >&2
    exit 1
fi

if printf '%s\n' "${PROGRAMS}" | grep -Eq '^  (INTERP|DYNAMIC|TLS)'; then
    echo 'unexpected PT_INTERP/PT_DYNAMIC/PT_TLS in boring-shell' >&2
    exit 1
fi

TEXT_LINE=$(printf '%s\n' "${PROGRAMS}" | grep '^  LOAD' | grep '0x0000000040000000')
RODATA_LINE=$(printf '%s\n' "${PROGRAMS}" | grep '^  LOAD' | grep '0x0000000040001000')
DATA_LINE=$(printf '%s\n' "${PROGRAMS}" | grep '^  LOAD' | grep '0x0000000040002000')

printf '%s\n' "${TEXT_LINE}" | grep -Eq ' R E +0x1000$'
printf '%s\n' "${RODATA_LINE}" | grep -Eq ' R +0x1000$'
printf '%s\n' "${DATA_LINE}" | grep -Eq ' RW +0x1000$'

if printf '%s\n' "${PROGRAMS}" | grep '^  LOAD' | grep -Eq ' W E '; then
    echo 'boring-shell contains a writable+executable PT_LOAD' >&2
    exit 1
fi

if printf '%s\n' "${SECTIONS}" | grep -Eq '\.(tdata|tbss) '; then
    echo 'unexpected TLS section in boring-shell' >&2
    exit 1
fi

if printf '%s\n' "${DYNAMIC}" | grep -Fq '(NEEDED)'; then
    echo 'boring-shell has a dynamic library dependency' >&2
    exit 1
fi
printf '%s\n' "${DYNAMIC}" | grep -Fq 'There is no dynamic section in this file.'
printf '%s\n' "${RELOCS}" | grep -Fq 'There are no relocations in this file.'

if [ -n "${UNDEFINED}" ]; then
    echo 'boring-shell has unresolved external symbols:' >&2
    printf '%s\n' "${UNDEFINED}" >&2
    exit 1
fi

for symbol in \
    _start boring_main boring_getpid boring_console_read boring_console_write \
    boring_fs_readdir boring_fs_mkdir boring_fs_rmdir boring_fs_chdir
do
    if ! printf '%s\n' "${SYMBOLS}" | grep -Eq " [Tt] ${symbol}$"; then
        echo "missing required boring-shell symbol: ${symbol}" >&2
        exit 1
    fi
done

if printf '%s\n' "${SYMBOLS}" | grep -Eq '(__libc_start_main|__stack_chk_fail|(^| )_init$|(^| )_fini$| printf$| snprintf$| malloc$| free$| fopen$| open$| close$)'; then
    echo 'unexpected host CRT/libc/file-I/O symbol in boring-shell' >&2
    exit 1
fi

printf '%s\n' 'boring-shell build audit passed.'
printf 'boring-shell size: %s bytes\n' "$(wc -c < "${ELF}")"
printf '%s\n' "${TEXT_LINE}"
printf '%s\n' "${RODATA_LINE}"
printf '%s\n' "${DATA_LINE}"
