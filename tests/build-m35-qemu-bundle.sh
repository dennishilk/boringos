#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT="${ROOT}/build/boringos-qemu-x86_64"

# Rebuild exact contents; never distribute a stale fixture from an earlier head.
sh "${ROOT}/tests/m35-bundle-test.sh"
sh "${ROOT}/tests/build-m34-qemu-bundle.sh"
mv "${OUT}/boringos.iso" "${OUT}/boringos-shell.iso"
sed 's@/boringos.iso@/boringos-shell.iso@' "${ROOT}/scripts/run-boringos.sh" > "${OUT}/run-boringos-shell.sh"
cp "${ROOT}/docs/RUNNING.md" "${OUT}/RUNNING-SHELL.md"
cp "${ROOT}/build/m35-bundle-test/boringos-root.img" "${OUT}/boringos-root.img"
cp "${ROOT}/build/m35-bundle-test/geometry.txt" "${OUT}/M35-BORINGFS-GEOMETRY.txt"
cp "${ROOT}/scripts/run-boringwm.sh" "${OUT}/run-boringos.sh"
cp "${ROOT}/docs/RUNNING-M35.md" "${OUT}/README.md"
chmod +x "${OUT}/run-boringos.sh" "${OUT}/run-boringos-shell.sh"

# Distribute normal WM semantics, never the deliberately dying negative binary.
make -C "${ROOT}" TEST_MODE=m35-wm
cp "${ROOT}/build/boringos.iso" "${OUT}/boringos.iso"
"${ROOT}/build/boringfsck" "${OUT}/boringos-root.img" | grep -Fqx 'Status: VALID'
[ "$(stat -c %s "${OUT}/boringos-root.img")" -eq $((96 * 4096)) ]
(
    cd "${OUT}"
    sha256sum boringos.iso boringos-shell.iso boringos-root.img \
        run-boringos.sh run-boringos-shell.sh README.md RUNNING-SHELL.md \
        M34-BORINGFS-GEOMETRY.txt M35-BORINGFS-GEOMETRY.txt > SHA256SUMS
    sha256sum -c SHA256SUMS
)
printf '%s\n' 'Human-runnable M35 WM and persistent-shell QEMU bundle built and verified.'
