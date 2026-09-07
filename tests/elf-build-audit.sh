#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ELF="${ROOT}/build/user/elf-smoke.elf"

make -C "${ROOT}" user-elf

HEADER=$(readelf -hW "${ELF}")
PROGRAMS=$(readelf -lW "${ELF}")
RELOCS=$(readelf -rW "${ELF}")

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

if printf '%s\n' "${PROGRAMS}" | grep -Eq '^  (INTERP|DYNAMIC)'; then
    echo 'unexpected PT_INTERP/PT_DYNAMIC in ELF smoke executable' >&2
    exit 1
fi

TEXT_LINE=$(printf '%s\n' "${PROGRAMS}" | grep '^  LOAD' | grep '0x0000000040000000')
RODATA_LINE=$(printf '%s\n' "${PROGRAMS}" | grep '^  LOAD' | grep '0x0000000040001000')
DATA_LINE=$(printf '%s\n' "${PROGRAMS}" | grep '^  LOAD' | grep '0x0000000040002000')

printf '%s\n' "${TEXT_LINE}" | grep -Eq ' R E +0x1000$'
printf '%s\n' "${RODATA_LINE}" | grep -Eq ' R +0x1000$'
printf '%s\n' "${DATA_LINE}" | grep -Eq ' RW +0x1000$'

DATA_FILESZ=$(printf '%s\n' "${DATA_LINE}" | awk '{print $5}')
DATA_MEMSZ=$(printf '%s\n' "${DATA_LINE}" | awk '{print $6}')
DATA_FILESZ_DEC=$(printf '%d' "${DATA_FILESZ}")
DATA_MEMSZ_DEC=$(printf '%d' "${DATA_MEMSZ}")
if [ "${DATA_FILESZ_DEC}" -ge "${DATA_MEMSZ_DEC}" ]; then
    echo "data PT_LOAD does not contain real BSS: filesz=${DATA_FILESZ} memsz=${DATA_MEMSZ}" >&2
    exit 1
fi

printf '%s\n' "${RELOCS}" | grep -Fq 'There are no relocations in this file.'

printf '%s\n' 'ELF smoke build audit passed.'
printf 'ELF smoke size: %s bytes\n' "$(wc -c < "${ELF}")"
printf '%s\n' "${TEXT_LINE}"
printf '%s\n' "${RODATA_LINE}"
printf '%s\n' "${DATA_LINE}"
