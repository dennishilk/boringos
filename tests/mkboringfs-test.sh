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
BAD="${BUILD}/bad.img"
SENTINEL="${BUILD}/existing.img"
PUBLISH_DIR="${BUILD}/publish-destination"
VALIDATOR_LOG="${BUILD}/validator-reject.log"

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
      "${BAD}" "${SENTINEL}" "${VALIDATOR_LOG}"
rm -rf "${PUBLISH_DIR}"

[ -x "${TOOL}" ] || fail 'build/mkboringfs is missing or not executable'
[ -x "${VERIFY}" ] || fail 'mkboringfs verifier is missing or not executable'

"${TOOL}" --help > /dev/null

# Minimum practical v0 geometry: one superblock, one bitmap block and two
# object-table blocks for 64 object records. Root is empty and owns no data.
"${TOOL}" --blocks 64 --objects 64 "${SMALL}"
"${VERIFY}" "${SMALL}" 64 64
[ "$(wc -c < "${SMALL}")" -eq 262144 ] || fail 'minimum image size mismatch'

# Human-visible acceptance artifact. 4096 objects deliberately changes the
# object-table geometry while keeping the CI artifact small.
"${TOOL}" --blocks 256 --objects 4096 "${ARTIFACT}"
"${TOOL}" --blocks 256 --objects 4096 "${REPEAT}"
"${VERIFY}" "${ARTIFACT}" 256 4096
"${VERIFY}" "${REPEAT}" 256 4096
[ "$(wc -c < "${ARTIFACT}")" -eq 1048576 ] || fail 'acceptance image size mismatch'
cmp -s "${ARTIFACT}" "${REPEAT}" || fail 'same inputs produced different bytes'

SHA_A=$(sha256sum "${ARTIFACT}" | awk '{print $1}')
SHA_B=$(sha256sum "${REPEAT}" | awk '{print $1}')
[ "${SHA_A}" = "${SHA_B}" ] || fail 'deterministic SHA-256 mismatch'

# The shared validator must reject a real corrupted image, proving that the
# M19 acceptance path reaches the M18 validator rather than a private checker.
cp "${ARTIFACT}" "${CORRUPT}"
printf 'X' | dd of="${CORRUPT}" bs=1 seek=0 conv=notrunc status=none
if "${VERIFY}" "${CORRUPT}" 256 4096 > /dev/null 2> "${VALIDATOR_LOG}"; then
    fail 'corrupted image unexpectedly validated'
fi
grep -F 'shared validator rejected image:' "${VALIDATOR_LOG}" > /dev/null ||
    fail 'corruption did not reach the shared validator rejection path'

# Formatter input rejection matrix. Rejected invocations must not create BAD.
expect_fail 'missing arguments' "${TOOL}"
expect_fail 'missing --blocks' "${TOOL}" --objects 64 "${BAD}"
expect_fail 'missing --objects' "${TOOL}" --blocks 64 "${BAD}"
expect_fail 'missing output path' "${TOOL}" --blocks 64 --objects 64
expect_fail 'duplicate --blocks' "${TOOL}" --blocks 64 --blocks 64 --objects 64 "${BAD}"
expect_fail 'duplicate --objects' "${TOOL}" --blocks 64 --objects 64 --objects 64 "${BAD}"
expect_fail 'unknown option' "${TOOL}" --blocks 64 --objects 64 --unknown "${BAD}"
expect_fail 'malformed block count' "${TOOL}" --blocks nope --objects 64 "${BAD}"
expect_fail 'malformed object count' "${TOOL}" --blocks 64 --objects nope "${BAD}"
expect_fail 'numeric overflow' "${TOOL}" --blocks 18446744073709551616 --objects 64 "${BAD}"
expect_fail 'zero block count' "${TOOL}" --blocks 0 --objects 64 "${BAD}"
expect_fail 'object count zero' "${TOOL}" --blocks 64 --objects 0 "${BAD}"
expect_fail 'object count below minimum' "${TOOL}" --blocks 64 --objects 63 "${BAD}"
expect_fail 'object count above maximum' "${TOOL}" --blocks 1024 --objects 16385 "${BAD}"
expect_fail 'volume too small for metadata' "${TOOL}" --blocks 3 --objects 64 "${BAD}"
expect_fail 'volume beyond v0 maximum' "${TOOL}" --blocks 1048577 --objects 64 "${BAD}"
expect_fail 'output open failure' "${TOOL}" --blocks 64 --objects 64 "${BUILD}/missing-dir/image.img"
[ ! -e "${BAD}" ] || fail 'rejected input left an output image'

# Failure atomicity: invalid input must not touch an existing destination, and
# a publish-time rename failure must preserve the pre-existing directory and
# remove the temporary file.
printf 'keep-me\n' > "${SENTINEL}"
expect_fail 'existing destination preserved on validation failure' \
    "${TOOL}" --blocks 3 --objects 64 "${SENTINEL}"
[ "$(cat "${SENTINEL}")" = 'keep-me' ] || fail 'failed format replaced existing output'
mkdir "${PUBLISH_DIR}"
expect_fail 'publish rename failure' "${TOOL}" --blocks 64 --objects 64 "${PUBLISH_DIR}"
[ -d "${PUBLISH_DIR}" ] || fail 'publish failure damaged existing destination'
if find "${BUILD}" -maxdepth 1 -name 'publish-destination.tmp.*' -print | grep . > /dev/null; then
    fail 'publish failure left temporary output behind'
fi

printf 'mkboringfs deterministic SHA-256 A: %s\n' "${SHA_A}"
printf 'mkboringfs deterministic SHA-256 B: %s\n' "${SHA_B}"
echo 'mkboringfs cmp A B: identical'
echo 'mkboringfs acceptance header (first 128 bytes):'
od -Ax -tx1 -N128 "${ARTIFACT}"
echo 'mkboringfs acceptance root object (128 bytes):'
od -Ax -tx1 -j 8192 -N128 "${ARTIFACT}"
echo 'mkboringfs host verification passed.'
