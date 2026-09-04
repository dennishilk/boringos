#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT="${ROOT}/build/m37-bundle-test"
BUILDER="${ROOT}/build/boringfs-m37-bundle"
IMAGE="${OUT}/boringos-root.img"
BUNDLE_EXTRA_USER_CPPFLAGS=${M37_BUNDLE_EXTRA_USER_CPPFLAGS:-}
rm -rf "${OUT}"
mkdir -p "${OUT}"
make -C "${ROOT}" TEST_MODE=m36-desktop \
    RUNTIME_USER_CPPFLAGS="-Iuser/runtime/include -Ikernel/include -DBORING_BOUNDED_DESKTOP_ACCEPTANCE=1 ${BUNDLE_EXTRA_USER_CPPFLAGS}" \
    boringfsck user-boringfetch user-shell user-boring-terminal user-boringwm
cc -I"${ROOT}/libs/boringfs/include" -std=c11 -fno-builtin \
   -fno-tree-loop-distribute-patterns -Wall -Wextra -Wpedantic -Werror \
   -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes \
   "${ROOT}/tests/boringfs-m37-bundle.c" "${ROOT}/libs/boringfs/codec.c" \
   "${ROOT}/libs/boringfs/validate.c" -o "${BUILDER}"
"${BUILDER}" "${IMAGE}" "${ROOT}/build/user/boringfetch.elf" \
    "${ROOT}/build/user/boring-terminal.elf" \
    "${ROOT}/build/user/boring-shell.elf" \
    "${ROOT}/build/user/boring-display-wm.elf" \
    "${ROOT}/build/user/boringwm.elf" | tee "${OUT}/geometry.txt"
"${ROOT}/build/boringfsck" "${IMAGE}" | tee "${OUT}/boringfsck.txt" | \
    grep -Fqx 'Status: VALID'
BLOCKS=$(awk '/^M37 BoringFS measured geometry:/ {print $5}' "${OUT}/geometry.txt")
LOWER=$(awk '/^M37 lower bound:/ {print $4}' "${OUT}/geometry.txt")
case "${BLOCKS}" in ''|*[!0-9]*) exit 1 ;; esac
case "${LOWER}" in ''|*[!0-9]*) exit 1 ;; esac
[ $((LOWER + 1)) -eq "${BLOCKS}" ]
[ "$(stat -c %s "${IMAGE}")" -eq $((BLOCKS * 4096)) ]
grep -Fqx "Blocks: ${BLOCKS}" "${OUT}/boringfsck.txt"
for name in boringfetch boring-terminal boring-shell boring-display boringwm; do
    "${ROOT}/build/boringfsck" --cat "/bin/${name}" "${IMAGE}" > \
        "${OUT}/${name}.extracted"
    case "${name}" in
        boring-display) SOURCE_ELF="${ROOT}/build/user/boring-display-wm.elf" ;;
        *) SOURCE_ELF="${ROOT}/build/user/${name}.elf" ;;
    esac
    cmp "${SOURCE_ELF}" "${OUT}/${name}.extracted"
done
sha256sum "${IMAGE}" "${ROOT}/build/user/boringfetch.elf" \
    "${ROOT}/build/user/boring-terminal.elf" \
    "${ROOT}/build/user/boring-shell.elf" \
    "${ROOT}/build/user/boring-display-wm.elf" \
    "${ROOT}/build/user/boringwm.elf" > "${OUT}/SHA256SUMS"
printf '%s\n' 'M37 minimal BoringFS desktop session bundle passed.'
