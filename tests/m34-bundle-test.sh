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

"${BUILDER}" "${IMAGE}" valid \
    "${BF}" "${CAT}" "${INPUT}" "${MEMORY}" "${IPC}" \
    "${DISPLAY}" "${CLIENT_A}" "${CLIENT_B}" | tee "${BUILD}/geometry.txt"

[ "$(stat -c %s "${IMAGE}")" -eq $((112 * 4096)) ] ||
    fail 'M34 fixture is not exactly the explicit 112-block geometry'
"${FSCK}" "${IMAGE}" | tee "${BUILD}/boringfsck.txt" | grep -Fqx 'Status: VALID' ||
    fail 'M34 bundle failed boringfsck validation'
grep -Fqx 'Blocks: 112' "${BUILD}/boringfsck.txt" ||
    fail 'M34 boringfsck block geometry witness missing'
grep -Fqx 'M34 fixture blocks: 112' "${BUILD}/geometry.txt" ||
    fail 'M34 explicit geometry builder witness missing'
grep -Fqx 'M33 fixture blocks: 96' "${BUILD}/geometry.txt" ||
    fail 'M33 preserved geometry builder witness missing'

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
printf '%s\n' 'Fixture geometries: historical=64 M32=80 M33=96 M34=112 blocks.'
printf 'M34 root image: %s\n' "${IMAGE}"
