#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
QEMU=${QEMU:-qemu-system-x86_64}
QEMU_CPU=${QEMU_CPU:-qemu64,apic=off}
TMPDIR_PATH=$(mktemp -d)
SERIAL_BASE="${TMPDIR_PATH}/serial"
LOG="${TMPDIR_PATH}/serial.log"
QEMU_LOG="${TMPDIR_PATH}/qemu.log"
SLICE="${TMPDIR_PATH}/slice.log"
PID=
CAT_PID=
FD_OPEN=0
PROMPT=0

cleanup() {
    if [ "${FD_OPEN}" -eq 1 ]; then exec 3>&-; fi
    if [ -n "${PID}" ] && kill -0 "${PID}" 2>/dev/null; then
        kill "${PID}" 2>/dev/null || true
        wait "${PID}" 2>/dev/null || true
    fi
    if [ -n "${CAT_PID}" ] && kill -0 "${CAT_PID}" 2>/dev/null; then
        kill "${CAT_PID}" 2>/dev/null || true
        wait "${CAT_PID}" 2>/dev/null || true
    fi
    rm -rf "${TMPDIR_PATH}"
}
trap cleanup EXIT INT TERM

fail() {
    echo "$1" >&2
    cat "${LOG}" >&2 2>/dev/null || true
    cat "${QEMU_LOG}" >&2 2>/dev/null || true
    exit 1
}

failure_seen() {
    grep -Eiq 'boring-shell: FAILED|boring-init: FAILED|BoringKernel syscall fatal|Fatal exception|triple fault|reboot' "${LOG}" 2>/dev/null
}

prompt_count() {
    (grep -Ec '^boring@boringos:/[^$]*\$ ' "${LOG}" 2>/dev/null || true)
}

wait_prompt() {
    target=$1
    attempt=0
    while [ "${attempt}" -lt 800 ]; do
        failure_seen && fail "failure marker while waiting for prompt ${target}"
        if [ "$(prompt_count)" -ge "${target}" ]; then
            PROMPT=${target}
            return 0
        fi
        kill -0 "${PID}" 2>/dev/null || fail 'QEMU exited'
        attempt=$((attempt + 1))
        sleep 0.05
    done
    fail "prompt timeout at ${target}"
}

make -C "${ROOT}" TEST_MODE=shell
mkfifo "${SERIAL_BASE}.in" "${SERIAL_BASE}.out"
exec 3<> "${SERIAL_BASE}.in"
FD_OPEN=1
stdbuf -o0 tr -d '\r' < "${SERIAL_BASE}.out" > "${LOG}" &
CAT_PID=$!

"${QEMU}" -M q35 -cpu "${QEMU_CPU}" -m 128M \
    -cdrom "${ROOT}/build/boringos.iso" -boot d -display none \
    -serial "pipe:${SERIAL_BASE}" -monitor none -no-reboot -no-shutdown \
    >/dev/null 2>"${QEMU_LOG}" &
PID=$!

wait_prompt 1
grep -Fqx 'boring-shell: pid 2' "${LOG}" || fail 'initial shell PID is not 2'

old_pid=2
cycle=1
while [ "${cycle}" -le 6 ]; do
    if [ $((cycle % 2)) -eq 0 ]; then command=logout; else command=exit; fi
    new_pid=$((old_pid + 1))
    printf '%s\n' "${command}" >&3
    wait_prompt $((PROMPT + 1))

    grep -Fqx "boring-exit: child pid ${old_pid} status 0 is zombie" "${LOG}" ||
        fail "old PID ${old_pid} was not made a real zombie"
    grep -Fqx "boring-waitpid: reaped child pid ${old_pid}" "${LOG}" ||
        fail "old PID ${old_pid} was not reaped"
    grep -Fqx 'boring-init: shell exited with status 0; respawning' "${LOG}" ||
        fail 'boring-init did not observe the preserved status'
    grep -Fqx "boring-shell: pid ${new_pid}" "${LOG}" ||
        fail "replacement shell PID ${new_pid} did not start"

    offset=$(wc -c < "${LOG}")
    printf 'ps\n' >&3
    wait_prompt $((PROMPT + 1))
    tail -c "+$((offset + 1))" "${LOG}" > "${SLICE}"
    grep -Fqx '1 0 WAITING boring-init' "${SLICE}" ||
        fail 'PID 1 disappeared after respawn'
    grep -Fqx "${new_pid} 1 RUNNING boring-shell" "${SLICE}" ||
        fail "new PID ${new_pid} is not the running shell"
    if grep -Eq '^[0-9]+ 1 ZOMBIE boring-shell$' "${SLICE}"; then
        fail 'reaped zombie remained visible in ps'
    fi
    if grep -Eq "^${old_pid} 1 " "${SLICE}"; then
        fail "old PID ${old_pid} remained in the process snapshot"
    fi

    old_pid=${new_pid}
    cycle=$((cycle + 1))
done

EXIT_COUNT=$(grep -Fxc 'boring-init: shell exited with status 0; respawning' "${LOG}" 2>/dev/null || true)
REAP_COUNT=$(grep -Fc 'boring-waitpid: reaped child pid ' "${LOG}" 2>/dev/null || true)
[ "${EXIT_COUNT}" -eq 6 ] || fail "expected 6 init observations, got ${EXIT_COUNT}"
[ "${REAP_COUNT}" -eq 6 ] || fail "expected 6 reaps, got ${REAP_COUNT}"

printf 'echo lifecycle-still-interactive\n' >&3
wait_prompt $((PROMPT + 1))
grep -Fqx 'lifecycle-still-interactive' "${LOG}" ||
    fail 'system was not interactive after repeated respawn'

echo 'BoringOS shell exit/wait/reap/respawn lifecycle passed.'
