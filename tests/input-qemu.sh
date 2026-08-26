#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
QEMU=${QEMU:-qemu-system-x86_64}
QEMU_CPU=${QEMU_CPU:-qemu64,apic=off}
TMPDIR_PATH=$(mktemp -d)
IMAGE="${TMPDIR_PATH}/boringos-root.img"
LOG="${TMPDIR_PATH}/serial.log"
QEMU_LOG="${TMPDIR_PATH}/qemu.log"
QMP="${TMPDIR_PATH}/qmp.sock"
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
    while [ "${attempt}" -lt 400 ]; do
        grep -Fq "${needle}" "${LOG}" 2>/dev/null && return 0
        if grep -Eiq 'acceptance FAILED|boring-shell: FAILED|boring-init: FAILED|Fatal exception' "${LOG}" 2>/dev/null; then fail "failure marker while waiting for ${needle}"; fi
        kill -0 "${PID}" 2>/dev/null || fail "QEMU exited while waiting for ${needle}"
        attempt=$((attempt + 1)); sleep 0.1
    done
    fail "timeout waiting for ${needle}"
}
wait_prompt() {
    target=$1; attempt=0
    while [ "${attempt}" -lt 400 ]; do
        if [ "$(prompt_count)" -ge "${target}" ]; then PROMPT=${target}; return 0; fi
        kill -0 "${PID}" 2>/dev/null || fail "QEMU exited waiting for prompt"
        attempt=$((attempt + 1)); sleep 0.1
    done
    fail "prompt timeout"
}
qmp() { python3 "${ROOT}/tests/qmp-input.py" "${QMP}" "$@" || fail "QMP input injection failed: $*"; }
send_and_wait() { printf '%s\n' "$1" >&3; wait_prompt $((PROMPT + 1)); }

make -C "${ROOT}" input-host-test input-test-audit boringfs-fixture boringfsck user-boringfetch user-cat
"${ROOT}/build/boringfs-fixture" "${IMAGE}" valid \
    "${ROOT}/build/user/boringfetch.elf" \
    "${ROOT}/build/user/cat.elf" \
    "${ROOT}/build/user/input-test.elf" >/dev/null
"${ROOT}/build/boringfsck" "${IMAGE}" | grep -Fqx 'Status: VALID' || fail 'input fixture failed boringfsck'
make -C "${ROOT}" TEST_MODE=persistent-root

mkfifo "${PIPE}.in" "${PIPE}.out"
exec 3<> "${PIPE}.in"; FD_OPEN=1
stdbuf -o0 tr -d '\r' < "${PIPE}.out" > "${LOG}" & CAT_PID=$!
"${QEMU}" -M q35 -cpu "${QEMU_CPU}" -m 128M \
    -cdrom "${ROOT}/build/boringos.iso" -boot d \
    -drive "file=${IMAGE},if=none,format=raw,id=boringdisk" \
    -device "virtio-blk-pci,drive=boringdisk,disable-legacy=on" \
    -display none -serial "pipe:${PIPE}" -monitor none \
    -qmp "unix:${QMP},server=on,wait=off" -no-reboot -no-shutdown \
    >/dev/null 2> "${QEMU_LOG}" & PID=$!
wait_prompt 1

for line in \
    'boring-input: i8042 detected' \
    'boring-input: queue 128 events' \
    'boring-input: keyboard irq1 online' \
    'boring-input: mouse irq12 online'; do
    grep -Fqx "${line}" "${LOG}" || fail "missing input init marker: ${line}"
done

# First owner deliberately exits without INPUT_RELEASE. The kernel must clean it.
printf '%s\n' 'input-test --teardown' >&3
wait_for 'input-test: waiting for keyboard/mouse events'
sleep 0.3
if grep -Eq '^KEY |^MOUSE ' "${LOG}"; then fail 'input event appeared before QMP injection'; fi
qmp key a
wait_prompt 2
grep -Fqx 'input-test: exiting without release' "${LOG}" || fail 'teardown mode did not exit without release'
grep -Fqx 'boring-input: owner pid 3 teardown released' "${LOG}" || fail 'owner teardown did not release PID 3'
grep -Fqx 'boring-exit: child pid 3 status 0 is zombie' "${LOG}" || fail 'teardown child did not become zombie'
grep -Fqx 'boring-waitpid: reaped child pid 3' "${LOG}" || fail 'teardown child was not reaped'

# Second process must reclaim ownership without a reboot and witness real input.
printf '%s\n' 'input-test' >&3
wait_for 'input-test: waiting for keyboard/mouse events'
CLAIMS=$(grep -Fc 'input-test: input claimed' "${LOG}" || true)
[ "${CLAIMS}" -ge 2 ] || fail 'second input owner did not reclaim input'
grep -Fqx 'input-test: syscall negatives passed' "${LOG}" || fail 'Ring-3 input syscall negative tests missing'

qmp super-q
wait_for 'KEY DOWN Q mods=SUPER'
qmp super-enter
wait_for 'KEY DOWN ENTER mods=SUPER'
qmp move 20 10
wait_for 'MOUSE MOVE dx='
qmp button left down
wait_for 'MOUSE BUTTON LEFT DOWN'
qmp button left up
wait_for 'MOUSE BUTTON LEFT UP'
wait_prompt 3

for line in \
    'KEY DOWN LEFT_SUPER mods=SUPER' \
    'KEY DOWN Q mods=SUPER' \
    'KEY UP Q mods=SUPER' \
    'KEY UP LEFT_SUPER mods=NONE' \
    'KEY DOWN ENTER mods=SUPER' \
    'MOUSE BUTTON LEFT DOWN' \
    'MOUSE BUTTON LEFT UP' \
    'input-test: witness complete' \
    'input-test: input released'; do
    grep -Fqx "${line}" "${LOG}" || fail "missing Ring-3 witness: ${line}"
done

MOVE_TOTALS=$(awk '
/^MOUSE MOVE dx=-?[0-9]+ dy=-?[0-9]+$/ {
    x=$3; y=$4; sub(/^dx=/,"",x); sub(/^dy=/,"",y); sx+=x; sy+=y
}
END { printf "%d %d", sx, sy }
' "${LOG}")
[ "${MOVE_TOTALS}" = '20 10' ] || fail "mouse movement total mismatch: ${MOVE_TOTALS}"

grep -Fqx 'boring-exit: child pid 4 status 0 is zombie' "${LOG}" || fail 'input witness child did not exit'
grep -Fqx 'boring-waitpid: reaped child pid 4' "${LOG}" || fail 'input witness child was not reaped'
send_and_wait 'ps'
grep -Fqx '1 0 WAITING boring-init' "${LOG}" || fail 'PID1 missing after input children'
grep -Fqx '2 1 RUNNING boring-shell' "${LOG}" || fail 'shell missing after input children'
if grep -Eq '^[34] 2 ZOMBIE input-test$' "${LOG}"; then fail 'input-test zombie remained after waitpid'; fi

echo 'Real PS/2 keyboard and mouse userspace input passed.'
