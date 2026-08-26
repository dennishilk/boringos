#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD="${ROOT}/build/m34-bundle-test"
BUILDER="${ROOT}/build/boringfs-m34-bundle"
IMAGE="${BUILD}/boringos-root.img"
HIST="${BUILD}/historical-64.img"
M32="${BUILD}/m32-80.img"
M33="${BUILD}/m33-96.img"

fail() {
    echo "$1" >&2
    exit 1
}

rm -rf "${BUILD}"
mkdir -p "${BUILD}"

make -C "${ROOT}" \
    boringfs-fixture boringfsck \
    user-boringfetch user-cat user-input-test user-memory-test user-ipc-test \
    user-boring-display user-display-clients

cc -I"${ROOT}/libs/boringfs/include" \
   -std=c11 -fno-builtin -fno-tree-loop-distribute-patterns \
   -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow \
   -Wstrict-prototypes -Wmissing-prototypes \
   "${ROOT}/tests/boringfs-m34-bundle.c" \
   "${ROOT}/libs/boringfs/codec.c" \
   "${ROOT}/libs/boringfs/validate.c" \
   -o "${BUILDER}"

BF="${ROOT}/build/user/boringfetch.elf"
CAT="${ROOT}/build/user/cat.elf"
INPUT="${ROOT}/build/user/input-test.elf"
MEMORY="${ROOT}/build/user/memory-test.elf"
IPC="${ROOT}/build/user/ipc-test.elf"
DISPLAY="${ROOT}/build/user/boring-display.elf"
CLIENT_A="${ROOT}/build/user/display-client-a.elf"
CLIENT_B="${ROOT}/build/user/display-client-b.elf"
FIXTURE="${ROOT}/build/boringfs-fixture"
FSCK="${ROOT}/build/boringfsck"

"${FIXTURE}" "${HIST}" valid >/dev/null
"${FIXTURE}" "${M32}" valid "${BF}" "${CAT}" "${INPUT}" "${MEMORY}" >/dev/null
"${FIXTURE}" "${M33}" valid "${BF}" "${CAT}" "${INPUT}" "${MEMORY}" "${IPC}" >/dev/null

[ "$(stat -c %s "${HIST}")" -eq $((64 * 4096)) ] || fail 'historical fixture is no longer exactly 64 blocks'
[ "$(stat -c %s "${M32}")" -eq $((80 * 4096)) ] || fail 'M32 fixture is no longer exactly 80 blocks'
[ "$(stat -c %s "${M33}")" -eq $((96 * 4096)) ] || fail 'M33 fixture is no longer exactly 96 blocks'
[ $((80 - 64)) -eq 16 ] || fail 'historical-to-M32 fixture step is no longer 16 blocks'
[ $((96 - 80)) -eq 16 ] || fail 'M32-to-M33 fixture step is no longer 16 blocks'

"${BUILDER}" "${IMAGE}" valid \
    "${BF}" "${CAT}" "${INPUT}" "${MEMORY}" "${IPC}" \
    "${DISPLAY}" "${CLIENT_A}" "${CLIENT_B}" | tee "${BUILD}/geometry.txt"

[ "$(stat -c %s "${IMAGE}")" -eq $((96 * 4096)) ] ||
    fail 'M34 fixture did not retain the proven 96-block geometry'
"${FSCK}" "${IMAGE}" | tee "${BUILD}/boringfsck.txt" | grep -Fqx 'Status: VALID' ||
    fail 'M34 bundle failed boringfsck validation'
grep -Fqx 'Blocks: 96' "${BUILD}/boringfsck.txt" ||
    fail 'M34 boringfsck block geometry witness missing'
grep -Fqx 'M34 fixture blocks: 96' "${BUILD}/geometry.txt" ||
    fail 'M34 explicit geometry builder witness missing'
grep -Fqx 'M33 fixture blocks: 96' "${BUILD}/geometry.txt" ||
    fail 'M33 preserved geometry builder witness missing'
grep -Fqx 'M32 lower-bound fixture blocks: 80' "${BUILD}/geometry.txt" ||
    fail 'M34 lower-bound geometry witness missing'
grep -Fqx 'M32 complete-bundle capacity: rejected' "${BUILD}/geometry.txt" ||
    fail '80-block complete-bundle rejection witness missing'
grep -Eq '^M33 ipc-test block range: 80-[0-9]+$' "${BUILD}/geometry.txt" ||
    fail 'M33 IPC lower-bound range witness missing'

M33_FREE=$(awk -F': ' '/^M33 free blocks before display:/ {print $2}' "${BUILD}/geometry.txt")
M34_REQUIRED=$(awk -F': ' '/^M34 display blocks required:/ {print $2}' "${BUILD}/geometry.txt")
M34_FREE=$(awk -F': ' '/^M34 free blocks after display:/ {print $2}' "${BUILD}/geometry.txt")
case "${M33_FREE}" in ''|*[!0-9]*) fail 'M33 free-block geometry witness is not numeric' ;; esac
case "${M34_REQUIRED}" in ''|*[!0-9]*) fail 'M34 required-block geometry witness is not numeric' ;; esac
case "${M34_FREE}" in ''|*[!0-9]*) fail 'M34 remaining-block geometry witness is not numeric' ;; esac
[ "${M34_REQUIRED}" -le "${M33_FREE}" ] ||
    fail 'real M34 display trio does not fit the retained 96-block geometry'
[ $((M33_FREE - M34_REQUIRED)) -eq "${M34_FREE}" ] ||
    fail 'M34 96-block allocation accounting is inconsistent'

for spec in \
    '/bin/boringfetch:'"${BF}" \
    '/bin/cat:'"${CAT}" \
    '/bin/input-test:'"${INPUT}" \
    '/bin/memory-test:'"${MEMORY}" \
    '/bin/ipc-test:'"${IPC}" \
    '/bin/boring-display:'"${DISPLAY}" \
    '/bin/display-client-a:'"${CLIENT_A}" \
    '/bin/display-client-b:'"${CLIENT_B}"; do
    path=${spec%%:*}
    source=${spec#*:}
    extracted="${BUILD}/$(basename "${path}").extracted"
    "${FSCK}" --cat "${path}" "${IMAGE}" > "${extracted}" ||
        fail "boringfsck could not read ${path} from M34 bundle"
    cmp -s "${source}" "${extracted}" ||
        fail "M34 bundle bytes differ for ${path}"
done

sha256sum "${IMAGE}" \
    "${DISPLAY}" "${CLIENT_A}" "${CLIENT_B}" > "${BUILD}/SHA256SUMS"

printf '%s\n' 'M34 BoringFS bundle integration passed.'
printf '%s\n' '80-block capacity rejection and 96-block fit proved with the real complete bundle.'
printf '%s\n' 'Fixture geometries: historical=64 M32=80 M33=96 M34=96 blocks (retained by measured fit).'
printf 'M34 root image: %s\n' "${IMAGE}"
