#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
QEMU=${QEMU:-qemu-system-x86_64}
QEMU_CPU=${QEMU_CPU:-qemu64,apic=off}
TMPDIR_PATH=$(mktemp -d)
IMAGE="${TMPDIR_PATH}/boringos-root.img"
LOG="${TMPDIR_PATH}/serial.log"
QEMU_LOG="${TMPDIR_PATH}/qemu.log"
PIPE="${TMPDIR_PATH}/serial"
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
wait_for() {
    needle=$1; attempt=0
    while [ "${attempt}" -lt 500 ]; do
        grep -Fq "${needle}" "${LOG}" 2>/dev/null && return 0
        if grep -Eiq 'acceptance FAILED|memory-test: FAILED|boring-shell: FAILED|boring-init: FAILED|Fatal exception' "${LOG}" 2>/dev/null; then fail "failure marker while waiting for ${needle}"; fi
        kill -0 "${PID}" 2>/dev/null || fail "QEMU exited while waiting for ${needle}"
        attempt=$((attempt + 1)); sleep 0.1
    done
    fail "timeout waiting for ${needle}"
}
wait_prompt() {
    target=$1; attempt=0
    while [ "${attempt}" -lt 500 ]; do
        if [ "$(prompt_count)" -ge "${target}" ]; then PROMPT=${target}; return 0; fi
        kill -0 "${PID}" 2>/dev/null || fail "QEMU exited waiting for prompt"
        attempt=$((attempt + 1)); sleep 0.1
    done
    fail "prompt timeout"
}
send_and_wait() { printf '%s\n' "$1" >&3; wait_prompt $((PROMPT + 1)); }

make -C "${ROOT}" memory-host-test memory-test-audit boringfs-fixture boringfsck user-boringfetch user-cat user-input-test
"${ROOT}/build/boringfs-fixture" "${IMAGE}" valid \
    "${ROOT}/build/user/boringfetch.elf" \
    "${ROOT}/build/user/cat.elf" \
    "${ROOT}/build/user/input-test.elf" \
    "${ROOT}/build/user/memory-test.elf" >/dev/null
"${ROOT}/build/boringfsck" "${IMAGE}" | grep -Fqx 'Status: VALID' || fail 'memory fixture failed boringfsck'
make -C "${ROOT}" TEST_MODE=persistent-root

mkfifo "${PIPE}.in" "${PIPE}.out"
exec 3<> "${PIPE}.in"; FD_OPEN=1
stdbuf -o0 tr -d '\r' < "${PIPE}.out" > "${LOG}" & CAT_PID=$!
"${QEMU}" -M q35 -cpu "${QEMU_CPU}" -m 128M \
    -cdrom "${ROOT}/build/boringos.iso" -boot d \
    -drive "file=${IMAGE},if=none,format=raw,id=boringdisk" \
    -device "virtio-blk-pci,drive=boringdisk,disable-legacy=on" \
    -display none -serial "pipe:${PIPE}" -monitor none \
    -no-reboot -no-shutdown >/dev/null 2> "${QEMU_LOG}" & PID=$!
wait_prompt 1

# First child intentionally leaks every M32 resource category. Exit cleanup must
# remove all of it before the zombie is reaped.
printf '%s\n' 'memory-test --teardown' >&3
wait_for 'memory-test: leaving resources for exit cleanup'
wait_prompt 2
for line in \
    'boring-memory: cleanup pid 3 allocations 1 mappings 1 handles 1 objects 1->0' \
    'boring-exit: child pid 3 status 0 is zombie' \
    'boring-waitpid: reaped child pid 3'; do
    grep -Fqx "${line}" "${LOG}" || fail "missing teardown lifecycle witness: ${line}"
done

# Run the full acceptance in a new process without reboot. This proves process
# cleanup actually releases the bounded metadata/object capacity for reuse.
printf '%s\n' 'memory-test' >&3
wait_for 'memory-test: start'
wait_prompt 3
for line in \
    'memory-test: anonymous rw-nx allocation passed' \
    'memory-test: anonymous zero reuse passed' \
    'memory-test: heap allocator passed' \
    'memory-test: shared dual-alias proof passed' \
    'memory-test: close keeps mapping alive passed' \
    'memory-test: syscall negatives passed' \
    'memory-test: explicit cleanup passed' \
    'memory-test: witness complete' \
    'boring-exit: child pid 4 status 0 is zombie' \
    'boring-waitpid: reaped child pid 4'; do
    grep -Fqx "${line}" "${LOG}" || fail "missing Ring-3 memory witness: ${line}"
done

ACCOUNTING=$(grep -E '^memory-test: pmm before=[0-9]+ during=[0-9]+ after=[0-9]+$' "${LOG}" | tail -n 1 || true)
[ -n "${ACCOUNTING}" ] || fail 'missing PMM accounting witness'
set -- $(printf '%s\n' "${ACCOUNTING}" | sed -E 's/.*before=([0-9]+) during=([0-9]+) after=([0-9]+).*/\1 \2 \3/')
BEFORE=$1; DURING=$2; AFTER=$3
[ "${DURING}" -lt "${BEFORE}" ] || fail "PMM did not decrease during allocation: ${ACCOUNTING}"
[ "${AFTER}" -gt "${DURING}" ] || fail "PMM did not recover after explicit cleanup: ${ACCOUNTING}"
echo "M32 PMM accounting witness: ${ACCOUNTING}"

send_and_wait 'ps'
grep -Fqx '1 0 WAITING boring-init' "${LOG}" || fail 'PID1 missing after memory-test children'
grep -Fqx '2 1 RUNNING boring-shell' "${LOG}" || fail 'shell missing after memory-test children'
if grep -Eq '^[34] 2 ZOMBIE memory-test$' "${LOG}"; then fail 'memory-test zombie remained after waitpid'; fi

echo 'Real Ring-3 userspace memory and shared-buffer acceptance passed.'
