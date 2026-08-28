#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ELF="${ROOT}/build/user/boring-files.elf"
[ -f "${ELF}" ]
readelf -h "${ELF}" | grep -Fq 'Type:'
readelf -h "${ELF}" | grep -Eq 'EXEC.*Executable file'
readelf -h "${ELF}" | grep -Fq 'Machine:'
readelf -h "${ELF}" | grep -Fq 'Advanced Micro Devices X86-64'
! readelf -d "${ELF}" 2>/dev/null | grep -Eq 'NEEDED|libc|ld-linux'
! nm -u "${ELF}" | grep -v '^$'
nm "${ELF}" | grep -Eq '[[:space:]]_start$'
nm "${ELF}" | grep -Eq '[[:space:]]boring_main$'
printf '%s\n' 'boring-files ELF audit passed: freestanding static x86_64, no host runtime dependencies.'
python3 - "${ELF}" "${ROOT}/kernel/include/boring/elf_loader.h" <<'PY'
import re, struct, sys
from pathlib import Path
blob = Path(sys.argv[1]).read_bytes()
phoff = struct.unpack_from('<Q', blob, 32)[0]
phsize, count = struct.unpack_from('<HH', blob, 54)
limit = int(re.search(r'BORING_ELF_MAX_IMAGE_PAGES\s+(\d+)U', Path(sys.argv[2]).read_text()).group(1))
pages = sum((struct.unpack_from('<Q', blob, phoff + i * phsize + 40)[0] + 4095) // 4096
            for i in range(count) if struct.unpack_from('<I', blob, phoff + i * phsize)[0] == 1)
assert pages <= limit, (pages, limit)
print(f'BoringFiles ELF image pages: {pages}/{limit}; bounded directory scratch uses existing userspace allocation.')
PY
