#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
QEMU=${QEMU:-qemu-system-x86_64}
QEMU_CPU=${QEMU_CPU:-qemu64,apic=off}
TMPDIR_PATH=$(mktemp -d)
IMAGE="${TMPDIR_PATH}/boringos-root.img"
LOG="${TMPDIR_PATH}/serial.log"
QEMU_LOG="${TMPDIR_PATH}/qemu.log"
PIPE_BASE="${TMPDIR_PATH}/serial"
PID=
CAT_PID=
FD_OPEN=0
PROMPT=0

stop_vm() {
    if [ -n "${PID}" ] && kill -0 "${PID}" 2>/dev/null; then
        kill "${PID}" 2>/dev/null || true
        wait "${PID}" 2>/dev/null || true
    fi
    PID=
    if [ "${FD_OPEN}" -eq 1 ]; then exec 3>&-; FD_OPEN=0; fi
    if [ -n "${CAT_PID}" ] && kill -0 "${CAT_PID}" 2>/dev/null; then
        kill "${CAT_PID}" 2>/dev/null || true
        wait "${CAT_PID}" 2>/dev/null || true
    fi
    CAT_PID=
}
cleanup() { stop_vm; rm -rf "${TMPDIR_PATH}"; }
trap cleanup EXIT INT TERM
fail() { echo "$1" >&2; cat "${LOG}" >&2 2>/dev/null || true; cat "${QEMU_LOG}" >&2 2>/dev/null || true; exit 1; }
prompt_count() {
    count=$(grep -Ec '^boring@boringos:/[^$]*\$ ' "${LOG}" 2>/dev/null || true)
    printf '%s\n' "${count:-0}"
}
wait_prompt() {
    target=$1; attempt=0
    while [ "${attempt}" -lt 400 ]; do
        if grep -Eiq 'acceptance FAILED|boring-shell: FAILED|boring-init: FAILED|Fatal exception|BoringKernel syscall fatal' "${LOG}" 2>/dev/null; then
            fail "failure marker"
        fi
        if [ "$(prompt_count)" -ge "${target}" ]; then PROMPT=${target}; return 0; fi
        kill -0 "${PID}" 2>/dev/null || fail "QEMU exited"
        attempt=$((attempt + 1)); sleep 0.1
    done
    fail "prompt timeout"
}
send() { printf '%s\n' "$1" >&3; wait_prompt $((PROMPT + 1)); }

make -C "${ROOT}" boringfs-fixture boringfsck user-boringfetch user-cat
"${ROOT}/build/boringfs-fixture" "${IMAGE}" valid \
    "${ROOT}/build/user/boringfetch.elf" \
    "${ROOT}/build/user/cat.elf" >/dev/null
"${ROOT}/build/boringfsck" "${IMAGE}" | grep -Fqx 'Status: VALID' ||
    fail 'seeded descriptor acceptance image is invalid'
make -C "${ROOT}" TEST_MODE=persistent-root

mkfifo "${PIPE_BASE}.in" "${PIPE_BASE}.out"
exec 3<> "${PIPE_BASE}.in"; FD_OPEN=1
stdbuf -o0 tr -d '\r' < "${PIPE_BASE}.out" > "${LOG}" & CAT_PID=$!
"${QEMU}" -M q35 -cpu "${QEMU_CPU}" -m 128M -cdrom "${ROOT}/build/boringos.iso" -boot d \
    -drive "file=${IMAGE},if=none,format=raw,id=boringdisk" \
    -device "virtio-blk-pci,drive=boringdisk,disable-legacy=on" \
    -display none -serial "pipe:${PIPE_BASE}" -monitor none -no-reboot -no-shutdown \
    >/dev/null 2> "${QEMU_LOG}" & PID=$!
wait_prompt 1

grep -Fqx 'boring-shell: pid 2' "${LOG}" || fail 'shell is not PID 2'
send 'mkdir fdtest'
send 'cd fdtest'
send 'touch hello.txt'
send 'write hello.txt BoringOS-FD-test'
send 'cat hello.txt'

grep -Fq 'BoringOS-FD-test' "${LOG}" || fail 'standalone cat did not reproduce file bytes'
for line in \
    'boring-launch: caller pid 2' \
    'boring-launch: child pid 3' \
    'boring-launch: independent address space' \
    'boring-launch: VFS executable source /bin/cat' \
    'fd-open: pid 3 path hello.txt fd 3' \
    'fd-read: pid 3 fd 3 bytes 16' \
    'fd-write: pid 3 fd 1 bytes 16' \
    'fd-read: pid 3 fd 3 EOF' \
    'fd-close: pid 3 fd 3' \
    'boring-exit: child pid 3 status 0 is zombie' \
    'boring-waitpid: reaped child pid 3'; do
    grep -Fqx "${line}" "${LOG}" || fail "missing descriptor acceptance marker: ${line}"
done

send 'ps'
if grep -Fqx '3 2 ZOMBIE cat' "${LOG}"; then fail 'cat zombie remained after WAITPID'; fi

send 'cat does-not-exist'
grep -Fqx 'cat: cannot open' "${LOG}" || fail 'missing-path cat failure was not controlled'
grep -Fqx 'boring-exit: child pid 4 status 1 is zombie' "${LOG}" || fail 'missing-path cat status was not preserved'
grep -Fqx 'boring-waitpid: reaped child pid 4' "${LOG}" || fail 'missing-path cat child was not reaped'

send '/bin/cat hello.txt'
grep -Fqx 'boring-launch: child pid 5' "${LOG}" || fail 'explicit /bin/cat path did not launch'
grep -Fqx 'boring-waitpid: reaped child pid 5' "${LOG}" || fail 'explicit /bin/cat child was not reaped'

send 'boringfetch'
grep -Fqx 'boring-launch: VFS executable source /bin/boringfetch' "${LOG}" || fail 'M28 boringfetch regressed'
grep -Fqx 'boring-waitpid: reaped child pid 6' "${LOG}" || fail 'boringfetch child was not reaped'

send 'ps'
if grep -Eq ' ZOMBIE (cat|boringfetch)$' "${LOG}"; then fail 'external child zombie leaked'; fi
"${ROOT}/build/boringfsck" "${IMAGE}" | grep -Fqx 'Status: VALID' || fail 'post-acceptance image is invalid'

stop_vm
echo 'Native descriptor/stdout and standalone cat QEMU acceptance passed.'
