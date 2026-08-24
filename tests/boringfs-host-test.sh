#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD="${ROOT}/build/boringfs-host"
CC=${HOST_CC:-cc}
COMMON_FLAGS='-std=c11 -fno-builtin -fno-tree-loop-distribute-patterns -Wall -Wextra -Werror -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes'
INCLUDE="-I${ROOT}/libs/boringfs/include"
CODEC="${ROOT}/libs/boringfs/codec.c"
VALIDATE="${ROOT}/libs/boringfs/validate.c"
TEST="${ROOT}/tests/boringfs-host-test.c"

rm -rf "${BUILD}"
mkdir -p "${BUILD}"

# Non-sanitized production objects are used for the dependency audit.
${CC} ${COMMON_FLAGS} -O2 ${INCLUDE} -c "${CODEC}" -o "${BUILD}/codec.o"
${CC} ${COMMON_FLAGS} -O2 ${INCLUDE} -c "${VALIDATE}" -o "${BUILD}/validate.o"

UNDEFINED="${BUILD}/undefined.txt"
nm -u "${BUILD}/codec.o" "${BUILD}/validate.o" > "${UNDEFINED}" || true
if grep -Eq ' U (malloc|free|calloc|realloc|fopen|fread|fwrite|mmap|munmap|open|close|read|write|lseek|stat|fstat)$' "${UNDEFINED}"; then
    echo 'BoringFS reusable core has a forbidden host/runtime dependency:' >&2
    cat "${UNDEFINED}" >&2
    exit 1
fi
if grep -ERn '__attribute__[[:space:]]*\(\([[:space:]]*packed|memcpy[[:space:]]*\(|fopen[[:space:]]*\(|fread[[:space:]]*\(|fwrite[[:space:]]*\(|mmap[[:space:]]*\(|malloc[[:space:]]*\(|free[[:space:]]*\(' \
    "${ROOT}/libs/boringfs"; then
    echo 'BoringFS reusable core contains a forbidden raw-struct/I/O/allocation pattern.' >&2
    exit 1
fi
if grep -ERn '#include[[:space:]]*[<"]boring/(vfs|ramfs|process|kernel|pmm|vmm)' \
    "${ROOT}/libs/boringfs"; then
    echo 'BoringFS reusable core depends on BoringKernel/VFS/RAMFS internals.' >&2
    exit 1
fi

echo 'BoringFS core dependency audit: PASS'

${CC} ${COMMON_FLAGS} -O2 ${INCLUDE} \
    "${CODEC}" "${VALIDATE}" "${TEST}" \
    -o "${BUILD}/boringfs-host-test"
"${BUILD}/boringfs-host-test"

echo 'BoringFS normal host test: PASS'

${CC} ${COMMON_FLAGS} -O1 -g -fno-omit-frame-pointer \
    -fsanitize=address,undefined ${INCLUDE} \
    "${CODEC}" "${VALIDATE}" "${TEST}" \
    -o "${BUILD}/boringfs-host-test-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
    "${BUILD}/boringfs-host-test-sanitize"

echo 'BoringFS AddressSanitizer/UndefinedBehaviorSanitizer: PASS'
echo 'BoringFS v0 codec and structural validator host acceptance passed.'
