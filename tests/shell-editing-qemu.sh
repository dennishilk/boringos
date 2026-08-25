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
    count=$(grep -Ec '^boring@boringos:/[^$]*\$ ' "${LOG}" 2>/dev/null || true)
    printf '%s\n' "${count:-0}"
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
stdbuf -o0 tr -d '\r' < "${SERIAL_BASE}.out" >"${LOG}" &
CAT_PID=$!
"${QEMU}" -M q35 -cpu "${QEMU_CPU}" -m 128M \
    -cdrom "${ROOT}/build/boringos.iso" -boot d -display none \
    -serial "pipe:${SERIAL_BASE}" -monitor none -no-reboot -no-shutdown \
    >/dev/null 2>"${QEMU_LOG}" &
PID=$!
wait_prompt 1
grep -Fq 'boring@boringos:/$ ' "${LOG}" || fail 'missing identity prompt'

send() {
    printf '%s\n' "$1" >&3
    wait_prompt $((PROMPT + 1))
}

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

# Right Arrow and Backspace both edit at the current bounded cursor.
printf 'hlp\033[H\033[Ce\n' >&3
wait_prompt $((PROMPT + 1))
printf 'helpp\b\n' >&3
wait_prompt $((PROMPT + 1))

# Unknown bounded CSI sequences are consumed and must not leak printable bytes.
printf 'help\033[999~\n' >&3
wait_prompt $((PROMPT + 1))

# History navigation recalls real commands and Down restores the draft line.
send 'echo history-one'
send 'echo history-two'
printf '\033[A\n' >&3
wait_prompt $((PROMPT + 1))
printf 'echo draft\033[A\033[B\n' >&3
wait_prompt $((PROMPT + 1))
send 'history'

HISTORY_TWO=$(grep -Fxc 'history-two' "${LOG}" 2>/dev/null || true)
[ "${HISTORY_TWO}" -ge 2 ] || fail 'Up did not recall the latest command'
grep -Fqx 'draft' "${LOG}" || fail 'Down did not restore the draft command'
grep -Eq '^[0-9]+  history$' "${LOG}" || fail 'history output is not real'

# Command completion and real readdir-backed path completion.
send 'mkdir TEST'
send 'mkdir TEAM'
send 'touch README.txt'
send 'write README.txt completion-file'
send 'touch alpha-one'
send 'write alpha-one common-prefix-file'
send 'touch alpha-two'

printf 'boringf\t\n' >&3
wait_prompt $((PROMPT + 1))
grep -Fqx '    ____             BoringOS' "${LOG}" ||
    fail 'boringf TAB did not execute boringfetch'

printf 'cd TES\t\n' >&3
wait_prompt $((PROMPT + 1))
grep -Fq 'boring@boringos:/TEST$ ' "${LOG}" ||
    fail 'directory TAB completion did not change real CWD'
send 'cd ..'

printf 'cat REA\t\n' >&3
wait_prompt $((PROMPT + 1))
grep -Fqx 'completion-file' "${LOG}" ||
    fail 'regular-file TAB completion failed'

printf 'cat a\tone\n' >&3
wait_prompt $((PROMPT + 1))
grep -Fqx 'common-prefix-file' "${LOG}" ||
    fail 'multiple-match common-prefix completion failed'

printf 'echo zero-match\t\n' >&3
wait_prompt $((PROMPT + 1))
grep -Fqx 'zero-match' "${LOG}" || fail 'zero-match TAB changed command text'

HELP_COUNT=$(grep -Fxc 'Filesystem:' "${LOG}" 2>/dev/null || true)
[ "${HELP_COUNT}" -ge 6 ] || fail "expected six successful edited help commands, got ${HELP_COUNT}"
if grep -Fq 'command not found:' "${LOG}"; then
    fail 'ANSI escape suffix leaked into parsed command'
fi
kill -0 "${PID}" 2>/dev/null || fail 'QEMU did not remain alive'

echo 'BoringOS shell ANSI editing regression passed.'
