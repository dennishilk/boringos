#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ELF="${ROOT}/build/user/boring-edit.elf"
[ -f "${ELF}" ]
readelf -h "${ELF}" | grep -Fq 'Type:'
readelf -h "${ELF}" | grep -Eq 'EXEC.*Executable file'
readelf -h "${ELF}" | grep -Fq 'Machine:'
readelf -h "${ELF}" | grep -Fq 'Advanced Micro Devices X86-64'
! readelf -d "${ELF}" 2>/dev/null | grep -Eq 'NEEDED|libc|ld-linux'
! nm -u "${ELF}" | grep -v '^$'
nm "${ELF}" | grep -Eq '[[:space:]]_start$'
nm "${ELF}" | grep -Eq '[[:space:]]boring_main$'
printf '%s\n' 'boring-edit ELF audit passed: freestanding static x86_64, no host runtime dependencies.'
