#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
QEMU=${QEMU:-qemu-system-x86_64}
QEMU_CPU=${QEMU_CPU:-qemu64,apic=off}
TMPDIR_PATH=$(mktemp -d)
SERIAL_BASE="${TMPDIR_PATH}/serial"
LOG="${TMPDIR_PATH}/serial.log"
QEMU_LOG="${TMPDIR_PATH}/qemu.log"
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

prompt_count() {
    (grep -Fo 'boring> ' "${LOG}" 2>/dev/null || true) | wc -l | tr -d ' '
}

wait_prompt() {
    target=$1
    attempt=0
    while [ "${attempt}" -lt 600 ]; do
        if grep -Eiq 'boring-shell: FAILED|boring-init: FAILED|BoringKernel syscall fatal|Fatal exception|triple fault|reboot' "${LOG}" 2>/dev/null; then
            fail "failure marker"
        fi
        if [ "$(prompt_count)" -ge "${target}" ]; then
            PROMPT=${target}
            return 0
        fi
        kill -0 "${PID}" 2>/dev/null || fail "QEMU exited"
        attempt=$((attempt + 1))
        sleep 0.05
    done
    fail "prompt timeout"
}

make -C "${ROOT}" TEST_MODE=shell
mkfifo "${SERIAL_BASE}.in" "${SERIAL_BASE}.out"
exec 3<> "${SERIAL_BASE}.in"
FD_OPEN=1
cat "${SERIAL_BASE}.out" >"${LOG}" &
CAT_PID=$!
"${QEMU}" -M q35 -cpu "${QEMU_CPU}" -m 128M \
    -cdrom "${ROOT}/build/boringos.iso" -boot d -display none \
    -serial "pipe:${SERIAL_BASE}" -monitor none -no-reboot -no-shutdown \
    >/dev/null 2>"${QEMU_LOG}" &
PID=$!
wait_prompt 1

# Insert the missing 'c' before the final h using Left Arrow.
printf 'boringfeth\033[Dc\n' >&3
wait_prompt $((PROMPT + 1))
grep -Fqx '    ____             BoringOS' "${LOG}" ||
    fail 'left-arrow insertion did not execute boringfetch'

# Delete the extra X using Left Arrow + Delete (CSI 3~).
printf 'helpX\033[D\033[3~\n' >&3
wait_prompt $((PROMPT + 1))

# Home must permit insertion at the beginning.
printf 'elp\033[Hh\n' >&3
wait_prompt $((PROMPT + 1))

# End must return to append position after an edit at the start.
printf 'elp\033[Hh\033[F\n' >&3
wait_prompt $((PROMPT + 1))

# Unknown bounded CSI sequences are consumed and must not leak printable bytes.
printf 'help\033[999~\n' >&3
wait_prompt $((PROMPT + 1))

HELP_COUNT=$(grep -Fxc 'help' "${LOG}" 2>/dev/null || true)
[ "${HELP_COUNT}" -ge 4 ] || fail "expected four successful edited help commands, got ${HELP_COUNT}"
if grep -Fq 'command not found:' "${LOG}"; then
    fail 'ANSI escape suffix leaked into parsed command'
fi
kill -0 "${PID}" 2>/dev/null || fail 'QEMU did not remain alive'

echo 'BoringOS shell ANSI editing regression passed.'
