#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD="${ROOT}/build/mkboringfs-test"
TOOL="${ROOT}/build/mkboringfs"
VERIFY="${BUILD}/mkboringfs-verify"
ARTIFACT="${ROOT}/build/boringfs-empty.img"
REPEAT="${BUILD}/boringfs-empty-repeat.img"
SMALL="${BUILD}/boringfs-minimum.img"
CORRUPT="${BUILD}/boringfs-corrupt.img"
VALIDATOR_LOG="${BUILD}/validator-reject.log"
CC=${HOST_CC:-cc}
COMMON_FLAGS='-std=c11 -fno-builtin -fno-tree-loop-distribute-patterns -Wall -Wextra -Werror -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes'
INCLUDE="-I${ROOT}/libs/boringfs/include"
CODEC="${ROOT}/libs/boringfs/codec.c"
VALIDATE="${ROOT}/libs/boringfs/validate.c"

fail() {
    echo "mkboringfs host test FAILED: $1" >&2
    exit 1
}

expect_fail() {
    label=$1
    shift
    if "$@" > /dev/null 2>&1; then
        fail "${label} unexpectedly succeeded"
    fi
}

mkdir -p "${BUILD}"
rm -f "${ARTIFACT}" "${REPEAT}" "${SMALL}" "${CORRUPT}" \
      "${VALIDATOR_LOG}"

if [ ! -x "${TOOL}" ]; then
    make -C "${ROOT}" mkboringfs
fi

# Build an independent raw-byte verifier that also invokes the shared M18
# decoder/validator on the real image file.
# shellcheck disable=SC2086
${CC} ${COMMON_FLAGS} ${INCLUDE} \
    "${ROOT}/tests/mkboringfs-verify.c" "${CODEC}" "${VALIDATE}" \
    -o "${VERIFY}"

"${TOOL}" --help > /dev/null

# Minimum useful v0 geometry: one superblock, one bitmap block and two object
# table blocks for 64 object records. No data allocation is needed for root.
"${TOOL}" --blocks 64 --objects 64 "${SMALL}"
"${VERIFY}" "${SMALL}" 64 64
[ "$(wc -c < "${SMALL}")" -eq 262144 ] || fail 'minimum image size mismatch'

# Human-visible acceptance artifact and deterministic reproduction proof.
"${TOOL}" --blocks 256 --objects 4096 "${ARTIFACT}"
"${TOOL}" --blocks 256 --objects 4096 "${REPEAT}"
"${VERIFY}" "${ARTIFACT}" 256 4096
"${VERIFY}" "${REPEAT}" 256 4096
[ "$(wc -c < "${ARTIFACT}")" -eq 1048576 ] || fail 'acceptance image size mismatch'
cmp -s "${ARTIFACT}" "${REPEAT}" || fail 'same inputs produced different bytes'

SHA_A=$(sha256sum "${ARTIFACT}" | awk '{print $1}')
SHA_B=$(sha256sum "${REPEAT}" | awk '{print $1}')
[ "${SHA_A}" = "${SHA_B}" ] || fail 'deterministic SHA-256 mismatch'

# The shared validator must reject a real corrupted image, demonstrating that
# the M19 acceptance path is still the M18 validator rather than a private
# formatter-only check.
cp "${ARTIFACT}" "${CORRUPT}"
printf 'X' | dd of="${CORRUPT}" bs=1 seek=0 conv=notrunc status=none
if "${VERIFY}" "${CORRUPT}" 256 4096 > /dev/null 2> "${VALIDATOR_LOG}"; then
    fail 'corrupted image unexpectedly validated'
fi
grep -F 'shared validator rejected image:' "${VALIDATOR_LOG}" > /dev/null ||
    fail 'corruption did not reach the shared validator rejection path'

# Formatter input rejection matrix.
expect_fail 'missing arguments' "${TOOL}"
expect_fail 'malformed block count' "${TOOL}" --blocks nope --objects 64 "${BUILD}/bad.img"
expect_fail 'numeric overflow' "${TOOL}" --blocks 18446744073709551616 --objects 64 "${BUILD}/bad.img"
expect_fail 'zero block count' "${TOOL}" --blocks 0 --objects 64 "${BUILD}/bad.img"
expect_fail 'object count zero' "${TOOL}" --blocks 64 --objects 0 "${BUILD}/bad.img"
expect_fail 'object count below minimum' "${TOOL}" --blocks 64 --objects 63 "${BUILD}/bad.img"
expect_fail 'object count above maximum' "${TOOL}" --blocks 1024 --objects 16385 "${BUILD}/bad.img"
expect_fail 'volume too small for metadata' "${TOOL}" --blocks 3 --objects 64 "${BUILD}/bad.img"
expect_fail 'volume beyond v0 maximum' "${TOOL}" --blocks 1048577 --objects 64 "${BUILD}/bad.img"
expect_fail 'output open failure' "${TOOL}" --blocks 64 --objects 64 "${BUILD}/missing-dir/image.img"
[ ! -e "${BUILD}/bad.img" ] || fail 'rejected input left an output image'

printf 'mkboringfs deterministic SHA-256 A: %s\n' "${SHA_A}"
printf 'mkboringfs deterministic SHA-256 B: %s\n' "${SHA_B}"
echo 'mkboringfs cmp A B: identical'
echo 'mkboringfs acceptance header (first 64 bytes):'
od -Ax -tx1 -N64 "${ARTIFACT}"
echo 'mkboringfs acceptance root object (128 bytes):'
od -Ax -tx1 -j 8192 -N128 "${ARTIFACT}"
echo 'mkboringfs host verification passed.'
