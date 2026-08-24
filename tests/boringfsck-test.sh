#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD="${ROOT}/build/boringfsck-test"
FORMATTER="${ROOT}/build/mkboringfs"
FSCK="${ROOT}/build/boringfsck"
VALID_SMALL="${BUILD}/valid-small.img"
VALID_LARGE="${BUILD}/valid-large.img"
READ_ONLY="${BUILD}/valid-read-only.img"
ZERO="${BUILD}/zero.img"
TRUNC_1="${BUILD}/trunc-1.img"
TRUNC_127="${BUILD}/trunc-127.img"
TRUNC_4095="${BUILD}/trunc-4095.img"
TRUNC_BLOCKS="${BUILD}/trunc-blocks.img"
BAD_MAGIC="${BUILD}/bad-magic.img"
BAD_VERSION="${BUILD}/bad-version.img"
BAD_FEATURE="${BUILD}/bad-feature.img"
BAD_LAYOUT="${BUILD}/bad-layout.img"
BAD_BITMAP="${BUILD}/bad-bitmap.img"
BAD_ROOT="${BUILD}/bad-root.img"
LEAK="${BUILD}/allocation-leak.img"

fail() {
    echo "boringfsck host test FAILED: $1" >&2
    exit 1
}

run_capture() {
    expected=$1
    log=$2
    shift 2

    set +e
    "$@" > "${log}" 2>&1
    actual=$?
    set -e
    if [ "${actual}" -ne "${expected}" ]; then
        cat "${log}" >&2 2>/dev/null || true
        fail "expected exit ${expected}, got ${actual}: $*"
    fi
}

sha256_of() {
    sha256sum "$1" | awk '{print $1}'
}

expect_valid_unchanged() {
    image=$1
    log=$2
    before=$(sha256_of "${image}")
    run_capture 0 "${log}" "${FSCK}" "${image}"
    after=$(sha256_of "${image}")
    [ "${before}" = "${after}" ] || fail "checker mutated valid image ${image}"
    grep -Fqx 'Status: VALID' "${log}" > /dev/null ||
        fail "valid image did not report VALID: ${image}"
}

expect_corrupt_unchanged() {
    image=$1
    reason=$2
    log=$3
    before=$(sha256_of "${image}")
    run_capture 1 "${log}" "${FSCK}" "${image}"
    after=$(sha256_of "${image}")
    [ "${before}" = "${after}" ] || fail "checker mutated corrupt image ${image}"
    grep -Fqx 'Status: CORRUPT' "${log}" > /dev/null ||
        fail "corrupt image did not report CORRUPT: ${image}"
    grep -Fqx "Reason: ${reason}" "${log}" > /dev/null || {
        cat "${log}" >&2
        fail "unexpected validator reason for ${image}"
    }
}

mkdir -p "${BUILD}"
rm -f "${BUILD}"/*.img "${BUILD}"/*.log

[ -x "${FORMATTER}" ] || fail 'build/mkboringfs is missing or not executable'
[ -x "${FSCK}" ] || fail 'build/boringfsck is missing or not executable'

"${FSCK}" --help > "${BUILD}/help.log"
grep -F 'Usage:' "${BUILD}/help.log" > /dev/null || fail '--help did not print usage'

run_capture 2 "${BUILD}/missing-argument.log" "${FSCK}"
run_capture 2 "${BUILD}/missing-path.log" "${FSCK}" "${BUILD}/does-not-exist.img"
run_capture 2 "${BUILD}/directory.log" "${FSCK}" "${BUILD}"
: > "${ZERO}"
run_capture 2 "${BUILD}/zero.log" "${FSCK}" "${ZERO}"

# Two real formatter geometries prove the checker is not tied to one fixture.
"${FORMATTER}" --blocks 64 --objects 64 "${VALID_SMALL}"
"${FORMATTER}" --blocks 256 --objects 4096 "${VALID_LARGE}"
expect_valid_unchanged "${VALID_SMALL}" "${BUILD}/valid-small.log"
expect_valid_unchanged "${VALID_LARGE}" "${BUILD}/valid-large.log"
grep -Fqx 'Blocks: 64' "${BUILD}/valid-small.log" > /dev/null || fail 'small block count missing'
grep -Fqx 'Objects: 64' "${BUILD}/valid-small.log" > /dev/null || fail 'small object count missing'
grep -Fqx 'Data start: 4' "${BUILD}/valid-small.log" > /dev/null || fail 'small data start missing'
grep -Fqx 'Blocks: 256' "${BUILD}/valid-large.log" > /dev/null || fail 'large block count missing'
grep -Fqx 'Objects: 4096' "${BUILD}/valid-large.log" > /dev/null || fail 'large object count missing'
grep -Fqx 'Data start: 130' "${BUILD}/valid-large.log" > /dev/null || fail 'large data start missing'

# A read-only target must remain inspectable because boringfsck opens O_RDONLY.
cp "${VALID_SMALL}" "${READ_ONLY}"
chmod 0444 "${READ_ONLY}"
expect_valid_unchanged "${READ_ONLY}" "${BUILD}/valid-read-only.log"
chmod 0644 "${READ_ONLY}"

# Truncated real images must fail cleanly. Zero length is a host/resource
# boundary error; non-empty prefixes reach the shared validator.
dd if="${VALID_SMALL}" of="${TRUNC_1}" bs=1 count=1 status=none
dd if="${VALID_SMALL}" of="${TRUNC_127}" bs=1 count=127 status=none
dd if="${VALID_SMALL}" of="${TRUNC_4095}" bs=1 count=4095 status=none
dd if="${VALID_SMALL}" of="${TRUNC_BLOCKS}" bs=4096 count=63 status=none
expect_corrupt_unchanged "${TRUNC_1}" 'truncated-volume' "${BUILD}/trunc-1.log"
expect_corrupt_unchanged "${TRUNC_127}" 'truncated-volume' "${BUILD}/trunc-127.log"
expect_corrupt_unchanged "${TRUNC_4095}" 'truncated-volume' "${BUILD}/trunc-4095.log"
expect_corrupt_unchanged "${TRUNC_BLOCKS}" 'truncated-volume' "${BUILD}/trunc-blocks.log"

# Controlled corruptions operate on disposable copies of the real M19 image.
cp "${VALID_SMALL}" "${BAD_MAGIC}"
printf 'X' | dd of="${BAD_MAGIC}" bs=1 seek=0 conv=notrunc status=none
expect_corrupt_unchanged "${BAD_MAGIC}" 'bad-magic' "${BUILD}/bad-magic.log"

cp "${VALID_SMALL}" "${BAD_VERSION}"
printf '\002' | dd of="${BAD_VERSION}" bs=1 seek=10 conv=notrunc status=none
expect_corrupt_unchanged "${BAD_VERSION}" 'unsupported-version' "${BUILD}/bad-version.log"

cp "${VALID_SMALL}" "${BAD_FEATURE}"
printf '\001' | dd of="${BAD_FEATURE}" bs=1 seek=52 conv=notrunc status=none
expect_corrupt_unchanged "${BAD_FEATURE}" 'unsupported-feature' "${BUILD}/bad-feature.log"

cp "${VALID_SMALL}" "${BAD_LAYOUT}"
printf '\003\000\000\000' | dd of="${BAD_LAYOUT}" bs=1 seek=28 conv=notrunc status=none
expect_corrupt_unchanged "${BAD_LAYOUT}" 'bad-layout' "${BUILD}/bad-layout.log"

cp "${VALID_SMALL}" "${BAD_BITMAP}"
printf '\016' | dd of="${BAD_BITMAP}" bs=1 seek=4096 conv=notrunc status=none
expect_corrupt_unchanged "${BAD_BITMAP}" 'bad-bitmap' "${BUILD}/bad-bitmap.log"
grep -Fqx 'Block: 0' "${BUILD}/bad-bitmap.log" > /dev/null || fail 'bad bitmap block location missing'

cp "${VALID_SMALL}" "${BAD_ROOT}"
printf '\001' | dd of="${BAD_ROOT}" bs=1 seek=8193 conv=notrunc status=none
expect_corrupt_unchanged "${BAD_ROOT}" 'bad-object-record' "${BUILD}/bad-root.log"
grep -Fqx 'Object: 1' "${BUILD}/bad-root.log" > /dev/null || fail 'root object location missing'

cp "${VALID_SMALL}" "${LEAK}"
printf '\037' | dd of="${LEAK}" bs=1 seek=4096 conv=notrunc status=none
expect_corrupt_unchanged "${LEAK}" 'allocation-leak' "${BUILD}/allocation-leak.log"
grep -Fqx 'Block: 4' "${BUILD}/allocation-leak.log" > /dev/null || fail 'allocation leak block location missing'

VALID_SHA=$(sha256_of "${VALID_LARGE}")
BAD_MAGIC_SHA=$(sha256_of "${BAD_MAGIC}")
LEAK_SHA=$(sha256_of "${LEAK}")

printf '$ build/mkboringfs --blocks 256 --objects 4096 build/boringfsck-test/valid-large.img\n'
printf '$ build/boringfsck build/boringfsck-test/valid-large.img\n'
cat "${BUILD}/valid-large.log"
printf 'Exit: 0\nSHA-256 before/after: %s / %s\nUnchanged: YES\n' "${VALID_SHA}" "${VALID_SHA}"

printf '\n$ corrupt byte 0: magic B -> X\n'
printf '$ build/boringfsck build/boringfsck-test/bad-magic.img\n'
cat "${BUILD}/bad-magic.log"
printf 'Exit: 1\nSHA-256 before/after check: %s / %s\nChecker mutation: NO\n' "${BAD_MAGIC_SHA}" "${BAD_MAGIC_SHA}"

printf '\n$ corrupt version minor at offset 10: 1 -> 2\n'
cat "${BUILD}/bad-version.log"
printf '\n$ corrupt object_table_start at offset 28: 2 -> 3\n'
cat "${BUILD}/bad-layout.log"
printf '\n$ clear mandatory metadata allocation bit for block 0\n'
cat "${BUILD}/bad-bitmap.log"
printf '\n$ corrupt root object type at offset 8193: directory -> regular\n'
cat "${BUILD}/bad-root.log"
printf '\n$ mark unowned data block 4 allocated\n'
cat "${BUILD}/allocation-leak.log"
printf 'Allocation-leak SHA-256 before/after check: %s / %s\nChecker mutation: NO\n' "${LEAK_SHA}" "${LEAK_SHA}"

echo 'boringfsck read-only host verification passed.'
