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
prompt_count() { (grep -Fo 'boring> ' "${LOG}" 2>/dev/null || true) | wc -l | tr -d ' '; }
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
    cat "${base}.out" > "${LOG}" & CAT_PID=$!
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
for line in 'BoringKernel 0.0.26-dev' '  mount-at-root: PASS' 'BoringFS root mounted.' 'boring-init: pid 1' 'boring-shell: pid 2' 'boring-shell ready.'; do
    grep -Fqx "${line}" "${LOG}" || fail "missing root boot marker: ${line}"
done
send 'touch persistence.txt'
send 'write persistence.txt boring-root-persistence'
send 'cat persistence.txt'
grep -Fq 'boring-root-persistenceboring> ' "${LOG}" || fail 'first boot read-back failed'
stop_vm
"${ROOT}/build/boringfsck" "${IMAGE}" | grep -Fqx 'Status: VALID' || fail 'host validation failed'

start_vm second
send 'cat persistence.txt'
grep -Fq 'boring-root-persistenceboring> ' "${LOG}" || fail 'reboot persistence failed'
send 'boringfetch'
for line in '    ____             BoringOS' '  / __  / __ \/ ___/ OS: BoringOS' ' / /_/ / /_/ / /    Kernel: BoringKernel 0.0.26-dev' '/_____/\____/_/      Arch: x86_64' '                     Root FS: BoringFS' '                     Shell: boring-shell'; do
    grep -Fqx "${line}" "${LOG}" || fail "missing boringfetch line: ${line}"
done
grep -Eq '^                     Memory usable: [1-9][0-9]* bytes$' "${LOG}" || fail 'usable memory is not real'
grep -Eq '^                     Memory free: [1-9][0-9]* bytes$' "${LOG}" || fail 'free memory is not real'
stop_vm
echo 'Persistent BoringFS root reboot verification passed.'
