#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT="${ROOT}/build/boringos-m36-qemu-x86_64"
M36_TERMINAL_VARIANT=normal sh "${ROOT}/tests/m36-bundle-test.sh"
make -C "${ROOT}" TEST_MODE=m36-desktop
rm -rf "${OUT}"
mkdir -p "${OUT}"
cp "${ROOT}/build/boringos.iso" "${OUT}/boringos.iso"
cp "${ROOT}/build/m36-bundle-test/boringos-root.img" "${OUT}/boringos-root.img"
cp "${ROOT}/build/m36-bundle-test/geometry.txt" "${OUT}/M36-BORINGFS-GEOMETRY.txt"
cp "${ROOT}/scripts/run-boring-terminal.sh" "${OUT}/run-boringos.sh"
cp "${ROOT}/docs/RUNNING-M36.md" "${OUT}/README.md"
chmod +x "${OUT}/run-boringos.sh"
git -C "${ROOT}" rev-parse HEAD > "${OUT}/SOURCE-COMMIT.txt"
git -C "${ROOT}" rev-parse 'HEAD^{tree}' > "${OUT}/SOURCE-TREE.txt"
git -C "${ROOT}" status --porcelain > "${OUT}/SOURCE-STATUS.txt"
"${ROOT}/build/boringfsck" "${OUT}/boringos-root.img" | grep -Fqx 'Status: VALID'
# The independent negative binary must never enter the runnable bundle.
! strings "${ROOT}/build/user/boring-terminal.elf" | grep -Fq 'test-only unexpected exit'
(
    cd "${OUT}"
    sha256sum boringos.iso boringos-root.img run-boringos.sh README.md \
        M36-BORINGFS-GEOMETRY.txt SOURCE-COMMIT.txt SOURCE-TREE.txt SOURCE-STATUS.txt > SHA256SUMS
    sha256sum -c SHA256SUMS
)
printf '%s\n' 'Human-runnable M36 graphical terminal QEMU bundle built and verified.'
