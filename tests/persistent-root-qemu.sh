#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
QEMU=${QEMU:-qemu-system-x86_64}
QEMU_CPU=${QEMU_CPU:-qemu64,apic=off}
TMPDIR_PATH=$(mktemp -d)
IMAGE="${TMPDIR_PATH}/boringos-root.img"
LOG="${TMPDIR_PATH}/serial.log"
QEMU_LOG="${TMPDIR_PATH}/qemu.log"
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
prompt_count() { (grep -Ec '^boring@boringos:/[^$]*\$ ' "${LOG}" 2>/dev/null || true); }
wait_prompt() {
    target=$1; attempt=0
    while [ "${attempt}" -lt 400 ]; do
        if grep -Eiq 'acceptance FAILED|boring-shell: FAILED|boring-init: FAILED|Fatal exception' "${LOG}" 2>/dev/null; then fail "failure marker"; fi
        if [ "$(prompt_count)" -ge "${target}" ]; then PROMPT=${target}; return 0; fi
        kill -0 "${PID}" 2>/dev/null || fail "QEMU exited"
        attempt=$((attempt + 1)); sleep 0.1
    done
    fail "prompt timeout"
}
start_vm() {
    phase=$1; base="${TMPDIR_PATH}/serial-${phase}"; LOG="${base}.log"; QEMU_LOG="${TMPDIR_PATH}/qemu-${phase}.log"; PROMPT=0
    mkfifo "${base}.in" "${base}.out"; exec 3<> "${base}.in"; FD_OPEN=1
    stdbuf -o0 tr -d '\r' < "${base}.out" > "${LOG}" & CAT_PID=$!
    "${QEMU}" -M q35 -cpu "${QEMU_CPU}" -m 128M -cdrom "${ROOT}/build/boringos.iso" -boot d \
        -drive "file=${IMAGE},if=none,format=raw,id=boringdisk" \
        -device "virtio-blk-pci,drive=boringdisk,disable-legacy=on" \
        -display none -serial "pipe:${base}" -monitor none -no-reboot -no-shutdown \
        >/dev/null 2> "${QEMU_LOG}" & PID=$!
    wait_prompt 1
}
send() { printf '%s\n' "$1" >&3; wait_prompt $((PROMPT + 1)); }

make -C "${ROOT}" boringfs-fixture boringfsck
"${ROOT}/build/boringfs-fixture" "${IMAGE}" valid >/dev/null
make -C "${ROOT}" TEST_MODE=persistent-root

start_vm first
for line in 'BoringKernel 0.0.27-dev' '  mount-at-root: PASS' 'BoringFS root mounted.' 'boring-init: pid 1' 'boring-shell: pid 2' 'boring-shell ready.'; do
    grep -Fqx "${line}" "${LOG}" || fail "missing root boot marker: ${line}"
done
grep -Fq 'boring@boringos:/$ ' "${LOG}" || fail 'missing root identity prompt'

send 'pwd'
grep -Fqx '/' "${LOG}" || fail 'pwd did not report root'
send 'mkdir TEST'
printf 'cd TE\t\n' >&3
wait_prompt $((PROMPT + 1))
grep -Fq 'boring@boringos:/TEST$ ' "${LOG}" || fail 'TAB cd did not update CWD prompt'
send 'touch hello.txt'
send 'write hello.txt Hallo-von-BoringOS'
send 'cat hello.txt'
grep -Fqx 'Hallo-von-BoringOS' "${LOG}" || fail 'newline-clean cat output failed'
send 'pwd'
grep -Fqx '/TEST' "${LOG}" || fail 'pwd did not report /TEST'
send 'ps'
grep -Fqx '1 0 WAITING boring-init' "${LOG}" || fail 'ps omitted real PID 1'
grep -Fqx '2 1 RUNNING boring-shell' "${LOG}" || fail 'ps omitted real PID 2'
send 'whoami'
grep -Fqx 'boring' "${LOG}" || fail 'whoami identity mismatch'
send 'hostname'
grep -Fqx 'boringos' "${LOG}" || fail 'hostname identity mismatch'
send 'uname'
grep -Fqx 'BoringOS BoringKernel 0.0.27-dev x86_64' "${LOG}" || fail 'uname identity mismatch'
printf 'boringf\t\n' >&3
wait_prompt $((PROMPT + 1))
grep -Fqx '    ____             BoringOS' "${LOG}" || fail 'command TAB did not invoke boringfetch'
send 'cd /'
printf 'cat REA\t\n' >&3
wait_prompt $((PROMPT + 1))
grep -Fqx 'Welcome to BoringOS.' "${LOG}" || fail 'file TAB did not read README.txt'
send 'mkdir persist'
send 'touch persist/a.txt'
send 'write persist/a.txt still-here'
send 'cat persist/a.txt'
grep -Fqx 'still-here' "${LOG}" || fail 'first boot persistence read-back failed'
send 'history'
grep -Eq '^[0-9]+  history$' "${LOG}" || fail 'history command was not recorded'
stop_vm
"${ROOT}/build/boringfsck" "${IMAGE}" | grep -Fqx 'Status: VALID' || fail 'host validation failed'
PERSISTED_SHA=$("${ROOT}/build/boringfsck" --cat /persist/a.txt "${IMAGE}" |
    sha256sum | awk '{print $1}')
EXPECTED_SHA=$(printf 'still-here\n' | sha256sum | awk '{print $1}')
[ "${PERSISTED_SHA}" = "${EXPECTED_SHA}" ] || fail 'persisted bytes/newline mismatch'

start_vm second
send 'cat /persist/a.txt'
grep -Fqx 'still-here' "${LOG}" || fail 'reboot persistence failed'
send 'boringfetch'
for line in '    ____             BoringOS' '  / __  / __ \/ ___/ OS: BoringOS' ' / /_/ / /_/ / /    Kernel: BoringKernel 0.0.27-dev' '/_____/\____/_/      Arch: x86_64' '                     Hostname: boringos' '                     User: boring' '                     Shell: boring-shell' '                     Root FS: BoringFS' '                     Root device: virtio-blk' '                     Processes: 2' '                     PID: 2'; do
    grep -Fqx "${line}" "${LOG}" || fail "missing boringfetch line: ${line}"
done
grep -Eq '^                     Memory: [0-9]+ MiB / [1-9][0-9]* MiB$' "${LOG}" || fail 'memory is not real'
grep -Eq '^                     Free memory: [0-9]+ MiB$' "${LOG}" || fail 'free memory is not real'
grep -Eq '^                     Uptime: [0-9]+ s$' "${LOG}" || fail 'uptime is not real'
stop_vm
echo 'Persistent BoringFS root reboot verification passed.'
