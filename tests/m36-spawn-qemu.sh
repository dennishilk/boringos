#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
QEMU=${QEMU:-qemu-system-x86_64}
QEMU_CPU=${QEMU_CPU:-qemu64,apic=off}
EVIDENCE="${ROOT}/build/m36-spawn-reference"
mkdir -p "${EVIDENCE}"
TMPDIR_PATH=$(mktemp -d)
IMAGE="${TMPDIR_PATH}/boringos-m36-root.img"
LOG="${TMPDIR_PATH}/serial.log"
QEMU_LOG="${TMPDIR_PATH}/qemu.log"
PID=

cleanup() {
    if [ -n "${PID}" ] && kill -0 "${PID}" 2>/dev/null; then
        kill "${PID}" 2>/dev/null || true
        wait "${PID}" 2>/dev/null || true
    fi
    if [ -f "${LOG}" ]; then cp "${LOG}" "${EVIDENCE}/serial.log"; fi
    if [ -f "${QEMU_LOG}" ]; then cp "${QEMU_LOG}" "${EVIDENCE}/qemu.log"; fi
    rm -rf "${TMPDIR_PATH}"
}
trap cleanup EXIT INT TERM

fail() {
    echo "$1" >&2
    cat "${LOG}" >&2 2>/dev/null || true
    cat "${QEMU_LOG}" >&2 2>/dev/null || true
    exit 1
}

make -C "${ROOT}" boringfs-fixture boringfsck user-boringfetch user-cat \
    user-input-test user-memory-test user-m36-spawn
"${ROOT}/build/boringfs-fixture" "${IMAGE}" valid \
    "${ROOT}/build/user/boringfetch.elf" \
    "${ROOT}/build/user/cat.elf" \
    "${ROOT}/build/user/input-test.elf" \
    "${ROOT}/build/user/memory-test.elf" \
    "${ROOT}/build/user/m36-spawn-child.elf" >/dev/null
"${ROOT}/build/boringfsck" "${IMAGE}" | grep -Fqx 'Status: VALID' ||
    fail 'M36 BoringFS test image is invalid'

# Uses the repository's pinned Limine 12.5.2 archive + SHA256 rule.
make -C "${ROOT}" TEST_MODE=m36-spawn

"${QEMU}" -M q35 -cpu "${QEMU_CPU}" -m 128M \
    -cdrom "${ROOT}/build/boringos.iso" -boot d \
    -drive "file=${IMAGE},if=none,format=raw,id=boringdisk" \
    -device "virtio-blk-pci,drive=boringdisk,disable-legacy=on" \
    -display none -serial "file:${LOG}" -monitor none -no-reboot -no-shutdown \
    >/dev/null 2>"${QEMU_LOG}" &
PID=$!

attempt=0
while [ "${attempt}" -lt 250 ]; do
    if grep -Fq 'M36 QEMU SPAWN acceptance passed.' "${LOG}" 2>/dev/null; then
        break
    fi
    if grep -Eq 'M36 spawn QEMU FAILED|Fatal exception|Kernel panic' "${LOG}" 2>/dev/null; then
        fail 'M36 QEMU gate emitted a failure marker'
    fi
    kill -0 "${PID}" 2>/dev/null || fail 'QEMU exited before M36 SPAWN acceptance'
    attempt=$((attempt + 1))
    sleep 0.1
done
[ "${attempt}" -lt 250 ] || fail 'M36 QEMU SPAWN acceptance timeout'

for line in \
    'BoringKernel 0.0.44-dev' \
    'M36 Scheduler + Ring3 + BoringFS + PTY + SPAWN acceptance:' \
    'm36-spawn-test: scheduler active' \
    'm36-spawn-test: real BoringFS root mounted' \
    'm36-parent: CPL3 scheduler parent running' \
    'boring-spawn: VFS executable source /bin/ipc-test' \
    'boring-spawn: independent address space' \
    'boring-spawn: argc 3' \
    'boring-spawn: stdio child 0<-4 1<-4 2<-4' \
    'child: argc-argv PASS' \
    'child: startup stack pages PASS' \
    'child: stdout PASS' \
    'child: stderr PASS' \
    'child: no-fd-inheritance PASS' \
    'child: stdin-block-ready' \
    'm36-parent: child reached blocking stdin' \
    'child: stdin wake PASS' \
    'child: stderr-after-wake PASS' \
    'child: exiting' \
    'boring-spawn: child pid 2 exited status 23' \
    'boring-spawn: reaped child pid 2 task/process cleanup complete' \
    'm36-parent: foreground PTY/SPAWN/WAITPID PASS' \
    'boring-spawn: argc 2' \
    'child: detached stdout PASS' \
    'child: detached stderr PASS' \
    'boring-spawn: child pid 3 exited status 31' \
    'boring-spawn: reaped child pid 3 task/process cleanup complete' \
    'm36-parent: detached auto-reap PASS' \
    'M36 QEMU SPAWN acceptance passed.'; do
    grep -Fqx "${line}" "${LOG}" || fail "missing M36 acceptance marker: ${line}"
done

grep -Eq '^m36-spawn-test: Ring3 parent pid 1 cr3 0x[0-9a-f]+$' "${LOG}" ||
    fail 'missing scheduler-started Ring3 parent/address-space marker'
grep -Eq '^boring-spawn: parent pid 1 child pid 2 task [1-9][0-9]* foreground$' "${LOG}" ||
    fail 'missing foreground child process/task marker'
grep -Eq '^boring-spawn: parent pid 1 child pid 3 task [1-9][0-9]* detached$' "${LOG}" ||
    fail 'missing detached child process/task marker'

kill "${PID}" 2>/dev/null || true
wait "${PID}" 2>/dev/null || true
PID=
printf '%s\n' 'M36 real Scheduler/Ring3/BoringFS/PTY/SPAWN QEMU acceptance passed.' > "${EVIDENCE}/SUCCESS.txt"
echo 'M36 real Scheduler/Ring3/BoringFS/PTY/SPAWN QEMU acceptance passed.'
