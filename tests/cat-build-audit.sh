#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ELF="${ROOT}/build/user/cat.elf"

make -C "${ROOT}" user-cat

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
if [ "${LOAD_COUNT}" -ne 3 ]; then
    echo "expected exactly 3 cat PT_LOAD entries, got ${LOAD_COUNT}" >&2
    exit 1
fi
if printf '%s\n' "${PROGRAMS}" | grep -Eq '^  (INTERP|DYNAMIC|TLS)'; then
    echo 'unexpected PT_INTERP/PT_DYNAMIC/PT_TLS in cat' >&2
    exit 1
fi
if printf '%s\n' "${PROGRAMS}" | grep '^  LOAD' | grep -Eq ' W E '; then
    echo 'cat contains a writable+executable PT_LOAD' >&2
    exit 1
fi

DATA_LINE=$(printf '%s\n' "${PROGRAMS}" | grep '^  LOAD' | sed -n '3p')
set -- ${DATA_LINE}
DATA_VADDR=$3
DATA_FILESZ=$5
DATA_MEMSZ=$6
if [ $((DATA_VADDR)) -ne $((0x40002000)) ] ||
   [ $((DATA_FILESZ)) -le 0 ] || [ $((DATA_MEMSZ)) -le 0 ]; then
    echo 'cat writable PT_LOAD is empty or misplaced' >&2
    exit 1
fi
printf '%s\n' "${DATA_LINE}" | grep -Eq ' RW +0x1000$'
printf '%s\n' "${DYNAMIC}" | grep -Fq 'There is no dynamic section in this file.'
printf '%s\n' "${RELOCS}" | grep -Fq 'There are no relocations in this file.'
if [ -n "${UNDEFINED}" ]; then
    echo 'cat has unresolved external symbols:' >&2
    printf '%s\n' "${UNDEFINED}" >&2
    exit 1
fi
for symbol in _start boring_main boring_fd_open boring_fd_read boring_fd_write boring_fd_close boring_exit; do
    if ! printf '%s\n' "${SYMBOLS}" | grep -Eq " [Tt] ${symbol}$"; then
        echo "missing required cat symbol: ${symbol}" >&2
        exit 1
    fi
done
for symbol in boring_fd_open boring_fd_read boring_fd_write boring_fd_close boring_exit; do
    if ! printf '%s\n' "${DISASSEMBLY}" | grep -Eq "call[q]?[[:space:]].*<${symbol}>"; then
        echo "cat does not call required descriptor/runtime function: ${symbol}" >&2
        exit 1
    fi
done
if printf '%s\n' "${DISASSEMBLY}" | grep -Eq 'call[q]?[[:space:]].*<(boring_fs_read|boring_console_write)>'; then
    echo 'cat calls a legacy FS_READ or CONSOLE_WRITE wrapper' >&2
    exit 1
fi
if printf '%s\n' "${SYMBOLS}" | grep -Eq '(__libc_start_main|__stack_chk_fail| printf$| snprintf$| malloc$| free$| fopen$| open$| close$)'; then
    echo 'cat contains a host CRT/libc/file-I/O dependency' >&2
    exit 1
fi
echo 'standalone cat build audit passed.'
printf 'cat size: %s bytes\n' "$(wc -c < "${ELF}")"
printf '%s\n' "${PROGRAMS}" | grep '^  LOAD'
