#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT="${ROOT}/build/m36-bundle-test"
BUILDER="${ROOT}/build/boringfs-m36-bundle"
IMAGE="${OUT}/boringos-root.img"
rm -rf "${OUT}"
mkdir -p "${OUT}"
make -C "${ROOT}" boringfsck user-boringfetch user-shell user-boring-terminal
cc -I"${ROOT}/libs/boringfs/include" -std=c11 -fno-builtin \
   -fno-tree-loop-distribute-patterns -Wall -Wextra -Wpedantic -Werror \
   -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes \
   "${ROOT}/tests/boringfs-m36-bundle.c" "${ROOT}/libs/boringfs/codec.c" \
   "${ROOT}/libs/boringfs/validate.c" -o "${BUILDER}"
"${BUILDER}" "${IMAGE}" "${ROOT}/build/user/boringfetch.elf" \
    "${ROOT}/build/user/boring-terminal.elf" "${ROOT}/build/user/boring-shell.elf" \
    | tee "${OUT}/geometry.txt"
"${ROOT}/build/boringfsck" "${IMAGE}" | tee "${OUT}/boringfsck.txt" | grep -Fqx 'Status: VALID'
BLOCKS=$(awk '/^M36 BoringFS measured geometry:/ {print $5}' "${OUT}/geometry.txt")
LOWER=$(awk '/^M36 lower bound:/ {print $4}' "${OUT}/geometry.txt")
case "${BLOCKS}" in ''|*[!0-9]*) exit 1 ;; esac
case "${LOWER}" in ''|*[!0-9]*) exit 1 ;; esac
[ $((LOWER + 1)) -eq "${BLOCKS}" ]
[ "$(stat -c %s "${IMAGE}")" -eq $((BLOCKS * 4096)) ]
grep -Fqx "Blocks: ${BLOCKS}" "${OUT}/boringfsck.txt"
for name in boringfetch boring-terminal boring-shell; do
    "${ROOT}/build/boringfsck" --cat "/bin/${name}" "${IMAGE}" > "${OUT}/${name}.extracted"
    cmp "${ROOT}/build/user/${name}.elf" "${OUT}/${name}.extracted"
done
sha256sum "${IMAGE}" "${ROOT}/build/user/boringfetch.elf" \
    "${ROOT}/build/user/boring-terminal.elf" "${ROOT}/build/user/boring-shell.elf" \
    > "${OUT}/SHA256SUMS"
printf '%s\n' 'M36 minimal BoringFS desktop bundle passed.'
