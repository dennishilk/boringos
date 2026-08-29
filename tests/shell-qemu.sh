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
FINAL_SLICE="${TMPDIR_PATH}/final-slice.log"
PID=
CAT_PID=
SERIAL_FD_OPEN=0
PROMPT=0

cleanup() {
    if [ "${SERIAL_FD_OPEN}" -eq 1 ]; then
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

fail_dump() {
    message=$1
    echo "${message}" >&2
    cat "${LOG}" >&2 2>/dev/null || true
    cat "${QEMU_LOG}" >&2 2>/dev/null || true
    exit 1
}

failure_seen() {
    grep -Eiq 'boring-shell: FAILED|boring-init: FAILED|boring-shell acceptance FAILED|BoringKernel syscall fatal|Fatal exception: controlled halt|triple fault|reboot' "${LOG}" 2>/dev/null
}

prompt_count() {
    (grep -Eo 'boring@boringos:/[^$]*\$ ' "${LOG}" 2>/dev/null || true) |
        wc -l | tr -d ' '
}

wait_for_prompt() {
    target=$1
    attempt=0
    while [ "${attempt}" -lt 300 ]; do
        if failure_seen; then
            fail_dump "failure marker while waiting for prompt ${target}"
        fi
        count=$(prompt_count)
        if [ "${count}" -ge "${target}" ]; then
            PROMPT=${target}
            return 0
        fi
        if ! kill -0 "${PID}" 2>/dev/null; then
            fail_dump "QEMU exited while waiting for prompt ${target}"
        fi
        attempt=$((attempt + 1))
        sleep 0.1
    done
    fail_dump "timed out waiting for prompt ${target}"
}

wait_for_line() {
    expected=$1
    attempt=0
    while [ "${attempt}" -lt 200 ]; do
        if grep -Fqx "${expected}" "${LOG}" 2>/dev/null; then
            return 0
        fi
        if failure_seen; then
            fail_dump "failure marker while waiting for line: ${expected}"
        fi
        if ! kill -0 "${PID}" 2>/dev/null; then
            fail_dump "QEMU exited while waiting for line: ${expected}"
        fi
        attempt=$((attempt + 1))
        sleep 0.1
    done
    fail_dump "timed out waiting for line: ${expected}"
}

send_command() {
    command=$1
    printf '%s\n' "${command}" >&3
    wait_for_prompt $((PROMPT + 1))
}

make -C "${ROOT}" TEST_MODE=shell

mkfifo "${SERIAL_IN}" "${SERIAL_OUT}"
exec 3<> "${SERIAL_IN}"
SERIAL_FD_OPEN=1
stdbuf -o0 tr -d '\r' < "${SERIAL_OUT}" > "${LOG}" &
CAT_PID=$!

"${QEMU}" \
    -M q35 \
    -cpu "${QEMU_CPU}" \
    -m 128M \
    -cdrom "${ROOT}/build/boringos.iso" \
    -boot d \
    -display none \
    -serial "pipe:${SERIAL_BASE}" \
    -monitor none \
    -no-reboot \
    -no-shutdown \
    > /dev/null 2> "${QEMU_LOG}" &
PID=$!

wait_for_prompt 1
grep -Fq 'boring@boringos:/$ ' "${LOG}" ||
    fail_dump 'missing real root CWD identity prompt'

for line in \
    'BoringKernel 0.0.50-dev' \
    'boring-shell launch:' \
    '  boot-modules-found: PASS' \
    '  init-module-found: PASS' \
    '  shell-module-found: PASS' \
    '  init-elf64-image: PASS' \
    '  shell-elf64-image: PASS' \
    '  nx-enabled: PASS' \
    '  process-syscall-init: PASS' \
    '  root-ramfs-vfs: PASS' \
    '  init-process-pid1: PASS' \
    '  init-cwd-root: PASS' \
    '  load-init-image: PASS' \
    '  init-memory-protections: PASS' \
    '  register-boring-shell: PASS' \
    '  pre-launch-process-state: PASS' \
    '  root-path-release: PASS' \
    'boring-init process PID: 1' \
    'Entering boring-init at CPL3.' \
    'boring-init: starting' \
    'boring-init: pid 1' \
    'boring-init: online' \
    'boring-init: launching boring-shell' \
    'boring-launch: caller pid 1' \
    'boring-launch: child pid 2' \
    'boring-launch: independent address space' \
    'boring-launch: shell entry executable' \
    'boring-launch: shell stack rw-nx' \
    'boring-launch: higher-half supervisor-only' \
    'boring-launch: cwd inherited' \
    'boring-launch: pid 1 remains alive' \
    'boring-launch: handoff via SYSRETQ' \
    'boring-shell: starting' \
    'boring-shell: pid 2' \
    'boring-shell ready.'
do
    if ! grep -Fqx "${line}" "${LOG}"; then
        fail_dump "missing shell acceptance line: ${line}"
    fi
done

if ! grep -Eq '^boring-init module size: [1-9][0-9]* bytes$' "${LOG}" ||
   ! grep -Eq '^boring-shell module size: [1-9][0-9]* bytes$' "${LOG}" ||
   ! grep -Eq '^boring-init process root: 0x[0-9A-F]{16}$' "${LOG}" ||
   ! grep -Eq '^boring-launch: child root 0x[0-9A-F]{16}$' "${LOG}"; then
    fail_dump 'missing shell runtime identity evidence'
fi

INIT_ROOT=$(sed -n 's/^boring-init process root: \(0x[0-9A-F][0-9A-F]*\)$/\1/p' "${LOG}" | tail -n 1)
CHILD_ROOT=$(sed -n 's/^boring-launch: child root \(0x[0-9A-F][0-9A-F]*\)$/\1/p' "${LOG}" | tail -n 1)
if [ -z "${INIT_ROOT}" ] || [ -z "${CHILD_ROOT}" ] ||
   [ "${INIT_ROOT}" = "${CHILD_ROOT}" ]; then
    fail_dump 'boring-init and boring-shell do not have distinct address-space roots'
fi

send_command 'pwd'
wait_for_line '/'
send_command 'echo Hallo-von-BoringOS'
wait_for_line 'Hallo-von-BoringOS'
send_command 'hostname'
wait_for_line 'boringos'
send_command 'whoami'
wait_for_line 'boring'
send_command 'uname'
wait_for_line 'BoringOS BoringKernel 0.0.50-dev x86_64'
send_command 'ps'
wait_for_line 'PID PPID STATE NAME'
wait_for_line '1 0 WAITING boring-init'
wait_for_line '2 1 RUNNING boring-shell'
send_command 'help'
wait_for_line 'Filesystem:'
wait_for_line '  ls cd pwd mkdir rmdir touch write rm'
wait_for_line 'Shell:'
wait_for_line '  clear echo history help exit logout'
wait_for_line 'System:'
wait_for_line '  uname hostname whoami ps'
wait_for_line 'Programs (/bin):'
wait_for_line '  boringfetch cat'
send_command 'clear'

send_command 'mkdir Test'
TEST_BEFORE=$(grep -Fxc 'Test' "${LOG}" 2>/dev/null || true)
send_command 'ls'
TEST_AFTER=$(grep -Fxc 'Test' "${LOG}" 2>/dev/null || true)
if [ "${TEST_AFTER}" -ne $((TEST_BEFORE + 1)) ]; then
    fail_dump 'ls did not independently emit exact RAMFS entry Test after mkdir'
fi

send_command 'mkdir Test'
wait_for_line 'mkdir: already exists'

send_command 'cd Test'
grep -Fq 'boring@boringos:/Test$ ' "${LOG}" ||
    fail_dump 'prompt did not use real /Test CWD'
send_command 'pwd'
wait_for_line '/Test'
send_command 'touch hello.txt'
send_command 'write hello.txt Hallo-von-BoringOS'
HELLO_BEFORE=$(grep -Fxc 'hello.txt' "${LOG}" 2>/dev/null || true)
send_command 'ls'
HELLO_AFTER=$(grep -Fxc 'hello.txt' "${LOG}" 2>/dev/null || true)
if [ "${HELLO_AFTER}" -ne $((HELLO_BEFORE + 1)) ]; then
    fail_dump 'written RAMFS file was not visible through real readdir'
fi
send_command 'mkdir Inner'
send_command 'rm Inner'
wait_for_line 'rm: is a directory'
INNER_BEFORE=$(grep -Fxc 'Inner' "${LOG}" 2>/dev/null || true)
send_command 'ls'
INNER_AFTER=$(grep -Fxc 'Inner' "${LOG}" 2>/dev/null || true)
if [ "${INNER_AFTER}" -ne $((INNER_BEFORE + 1)) ]; then
    fail_dump 'nested ls did not independently emit exact RAMFS entry Inner'
fi

send_command 'cd ..'
send_command 'rmdir Test'
wait_for_line 'rmdir: directory not empty'

send_command 'cd Test'
send_command 'rm hello.txt'
send_command 'rmdir Inner'
send_command 'cd ..'
send_command 'rmdir Test'

FINAL_OFFSET=$(wc -c < "${LOG}")
send_command 'ls'
tail -c "+$((FINAL_OFFSET + 1))" "${LOG}" > "${FINAL_SLICE}"
if grep -Fqx 'Test' "${FINAL_SLICE}"; then
    fail_dump 'final ls still observed Test after real rmdir'
fi

send_command 'history'
grep -Eq '^[0-9]+  history$' "${LOG}" ||
    fail_dump 'history did not expose the real bounded command list'

if failure_seen; then
    fail_dump 'unexpected failure after final shell prompt'
fi
if ! kill -0 "${PID}" 2>/dev/null; then
    fail_dump 'boring-shell did not remain alive through the final prompt'
fi

cat "${LOG}"
if [ -s "${QEMU_LOG}" ]; then
    cat "${QEMU_LOG}" >&2
fi

echo 'BoringKernel boring-shell verification passed.'
