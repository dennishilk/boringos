#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT="${ROOT}/build/m35-bundle-test"
sh "${ROOT}/tests/m34-bundle-test.sh"
make -C "${ROOT}" user-boringwm
mkdir -p "${OUT}"
cc -I"${ROOT}/libs/boringfs/include" -std=c11 -Wall -Wextra -Wpedantic -Werror \
    -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes \
    "${ROOT}/tests/boringfs-m35-bundle.c" "${ROOT}/libs/boringfs/codec.c" \
    "${ROOT}/libs/boringfs/validate.c" -o "${ROOT}/build/boringfs-m35-bundle"
"${ROOT}/build/boringfs-m35-bundle" "${OUT}/boringos-root.img" \
    "${ROOT}/build/m34-bundle-test/boringos-root.img" \
    "${ROOT}/build/user/boring-display-wm.elf" "${ROOT}/build/user/boringwm.elf" \
    "${ROOT}/build/user/wm-client-a.elf" "${ROOT}/build/user/wm-client-b.elf" \
    "${ROOT}/build/user/wm-client-c.elf" > "${OUT}/geometry.txt"
cat "${OUT}/geometry.txt"
[ "$(stat -c %s "${OUT}/boringos-root.img")" -eq $((96 * 4096)) ]
"${ROOT}/build/boringfsck" "${OUT}/boringos-root.img" > "${OUT}/boringfsck.txt"
grep -Fqx 'Status: VALID' "${OUT}/boringfsck.txt"
for name in boringfetch cat input-test memory-test ipc-test boring-display display-client-a display-client-b boring-display-wm boringwm wm-client-a wm-client-b wm-client-c; do
    "${ROOT}/build/boringfsck" --cat "/bin/${name}" "${OUT}/boringos-root.img" > "${OUT}/${name}.extracted"
    cmp "${ROOT}/build/user/${name}.elf" "${OUT}/${name}.extracted"
done
printf '%s\n' 'M35 BoringFS proof passed: 96 blocks, all 13 /bin files byte-identical; historical fixtures unchanged.'
