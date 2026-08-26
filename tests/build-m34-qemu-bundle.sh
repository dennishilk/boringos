#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT="${ROOT}/build/boringos-qemu-x86_64"
M34_ROOT="${ROOT}/build/m34-bundle-test/boringos-root.img"
GEOMETRY="${ROOT}/build/m34-bundle-test/geometry.txt"

fail() {
    echo "$1" >&2
    exit 1
}

# Build the ordinary persistent-root ISO exactly through the established M28+
# path, then replace only the distributable root image with the fully verified
# M34 superset fixture.
make -C "${ROOT}" TEST_MODE=persistent-root
sh "${ROOT}/tests/m34-bundle-test.sh"

[ -f "${ROOT}/build/boringos.iso" ] || fail 'persistent-root ISO missing'
[ -f "${M34_ROOT}" ] || fail 'verified M34 BoringFS root image missing'
[ -f "${GEOMETRY}" ] || fail 'M34 geometry evidence missing'

rm -rf "${OUT}"
mkdir -p "${OUT}"
cp "${ROOT}/build/boringos.iso" "${OUT}/boringos.iso"
cp "${M34_ROOT}" "${OUT}/boringos-root.img"
cp "${ROOT}/scripts/run-boringos.sh" "${OUT}/run-boringos.sh"
cp "${ROOT}/docs/RUNNING.md" "${OUT}/README.md"
cp "${GEOMETRY}" "${OUT}/M34-BORINGFS-GEOMETRY.txt"
chmod +x "${OUT}/run-boringos.sh"

"${ROOT}/build/boringfsck" "${OUT}/boringos-root.img" |
    grep -Fqx 'Status: VALID' || fail 'copied M34 root image failed boringfsck'
[ "$(stat -c %s "${OUT}/boringos-root.img")" -eq $((112 * 4096)) ] ||
    fail 'copied M34 root image lost its 112-block geometry'

(
    cd "${OUT}"
    sha256sum boringos.iso boringos-root.img > SHA256SUMS
    sha256sum -c SHA256SUMS
)

printf '%s\n' 'Human-runnable M34 QEMU bundle built and verified.'
printf 'Bundle path: %s\n' "${OUT}"
