#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
QEMU=${QEMU:-qemu-system-x86_64}
QEMU_CPU=${QEMU_CPU:-qemu64,apic=off}
TMPDIR_PATH=$(mktemp -d)
SERIAL_BASE="${TMPDIR_PATH}/serial"
SERIAL_IN="${SERIAL_BASE}.in"
SERIAL_OUT="${SERIAL_BASE}.out"
LOG="${TMPDIR_PATH}/serial.log"
QEMU_LOG="${TMPDIR_PATH}/qemu.log"
PID=
CAT_PID=
FD_OPEN=0
PROMPT=0
CYCLES_SLOW=40
CYCLES_BURST=120
CYCLES_MIXED=40
TOTAL_CYCLES=$((CYCLES_SLOW + CYCLES_BURST + CYCLES_MIXED))
COMMANDS_PER_CYCLE=7

cleanup() {
    if [ "${FD_OPEN}" -eq 1 ]; then
        exec 3>&-
    fi
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
    (grep -Ec '^boring@boringos:/[^$]*\$ ' "${LOG}" 2>/dev/null || true)
}

failure_seen() {
    grep -Eiq 'boring-shell: FAILED|boring-init: FAILED|BoringKernel syscall fatal|Fatal exception|triple fault|reboot' "${LOG}" 2>/dev/null
}

wait_prompt() {
    target=$1
    attempt=0
    while [ "${attempt}" -lt 3600 ]; do
        failure_seen && fail "failure marker while waiting for prompt ${target}"
        if [ "$(prompt_count)" -ge "${target}" ]; then
            PROMPT=${target}
            return 0
        fi
        kill -0 "${PID}" 2>/dev/null || fail "QEMU exited while waiting for prompt ${target}"
        attempt=$((attempt + 1))
        sleep 0.05
    done
    fail "timed out waiting for prompt ${target}"
}

send_command() {
    printf '%s\n' "$1" >&3
    wait_prompt $((PROMPT + 1))
}

send_bytewise() {
    text=$1
    index=1
    length=${#text}
    while [ "${index}" -le "${length}" ]; do
        byte=$(printf '%s' "${text}" | cut -c "${index}")
        printf '%s' "${byte}" >&3
        index=$((index + 1))
        sleep 0.002
    done
    printf '\n' >&3
}

emit_cycle() {
    printf '%s\n' \
        'boringfetch' \
        'boringfetch' \
        'cd TEST' \
        'cd ..' \
        'help' \
        'ls' \
        'boringfetch'
}

check_exact_count() {
    pattern=$1
    expected=$2
    actual=$(grep -Foc "${pattern}" "${LOG}" 2>/dev/null || true)
    [ "${actual}" -eq "${expected}" ] ||
        fail "input corruption: expected ${expected} occurrences of '${pattern}', got ${actual}"
}

make -C "${ROOT}" TEST_MODE=shell
mkfifo "${SERIAL_IN}" "${SERIAL_OUT}"
exec 3<> "${SERIAL_IN}"
FD_OPEN=1
tr -d '\r' < "${SERIAL_OUT}" > "${LOG}" &
CAT_PID=$!

"${QEMU}" -M q35 -cpu "${QEMU_CPU}" -m 128M \
    -cdrom "${ROOT}/build/boringos.iso" -boot d \
    -display none -serial "pipe:${SERIAL_BASE}" -monitor none \
    -no-reboot -no-shutdown >/dev/null 2>"${QEMU_LOG}" &
PID=$!

wait_prompt 1
send_command 'mkdir TEST'

# Character-by-character pacing.
i=0
while [ "${i}" -lt "${CYCLES_SLOW}" ]; do
    for command in 'boringfetch' 'boringfetch' 'cd TEST' 'cd ..' 'help' 'ls' 'boringfetch'; do
        send_bytewise "${command}"
        wait_prompt $((PROMPT + 1))
    done
    i=$((i + 1))
done

# Immediate burst pacing: intentionally queue many complete commands without
# waiting for a prompt between them.
i=0
while [ "${i}" -lt "${CYCLES_BURST}" ]; do
    emit_cycle >&3
    i=$((i + 1))
done
wait_prompt $((PROMPT + CYCLES_BURST * COMMANDS_PER_CYCLE))

# Mixed pacing: burst a cycle, then give the guest a small irregular pause.
i=0
while [ "${i}" -lt "${CYCLES_MIXED}" ]; do
    emit_cycle >&3
    case $((i % 4)) in
        0) sleep 0.001 ;;
        1) sleep 0.004 ;;
        2) sleep 0.010 ;;
        *) : ;;
    esac
    i=$((i + 1))
done
wait_prompt $((PROMPT + CYCLES_MIXED * COMMANDS_PER_CYCLE))

# Echo is our end-to-end byte witness: terminal -> UART -> syscall -> runtime
# -> shell line buffer. Exact counts catch dropped, duplicated, substituted,
# merged, or leaked escape bytes even when a mutated command happens to be
# syntactically valid.
check_exact_count 'boring@boringos:/$ boringfetch' $((TOTAL_CYCLES * 3))
check_exact_count 'boring@boringos:/$ cd TEST' "${TOTAL_CYCLES}"
check_exact_count 'boring@boringos:/TEST$ cd ..' "${TOTAL_CYCLES}"
check_exact_count 'boring@boringos:/$ help' "${TOTAL_CYCLES}"
check_exact_count 'boring@boringos:/$ ls' "${TOTAL_CYCLES}"

# Extend the same end-to-end path with terminal control input. Every sequence
# must be consumed as control input; no CSI/SS3 suffix byte may reach command
# parsing. These cases run after the exact 1,400-command witness above so its
# historical byte-count contract remains directly comparable.
printf 'boringfeth\033[Dc\n' >&3
wait_prompt $((PROMPT + 1))
printf 'hlp\033[H\033[Ce\n' >&3
wait_prompt $((PROMPT + 1))
printf 'helXp\033[D\033[D\033[3~\n' >&3
wait_prompt $((PROMPT + 1))
printf 'elpX\033[Hh\033[F\b\n' >&3
wait_prompt $((PROMPT + 1))
printf 'echo history-key\n\033[A\n' >&3
wait_prompt $((PROMPT + 2))
printf 'echo draft-key\033[A\033[B\n' >&3
wait_prompt $((PROMPT + 1))
printf 'boringf\t\n' >&3
wait_prompt $((PROMPT + 1))
printf 'cd TE\t\n' >&3
wait_prompt $((PROMPT + 1))
send_command 'cd ..'

grep -Fqx 'draft-key' "${LOG}" || fail 'Down history leaked or lost bytes'
HISTORY_KEY_COUNT=$(grep -Fxc 'history-key' "${LOG}" 2>/dev/null || true)
[ "${HISTORY_KEY_COUNT}" -eq 2 ] || fail 'Up history did not replay exactly'

if grep -Eq 'command not found:|^cd: |^ls: ' "${LOG}"; then
    fail 'input stress produced a mutated command or filesystem error'
fi
failure_seen && fail 'input stress ended with a kernel/userspace failure'
kill -0 "${PID}" 2>/dev/null || fail 'QEMU did not remain alive after input stress'

echo "BoringOS shell input stress passed: ${TOTAL_CYCLES} cycles, $((TOTAL_CYCLES * COMMANDS_PER_CYCLE)) commands."
