#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
QEMU=${QEMU:-qemu-system-x86_64}
QEMU_CPU=${QEMU_CPU:-qemu64,apic=off}
TMPDIR_PATH=$(mktemp -d)
LOG="${TMPDIR_PATH}/serial.log"
PID=
cleanup() {
    if [ -n "${PID}" ] && kill -0 "${PID}" 2>/dev/null; then
        kill "${PID}" 2>/dev/null || true
        wait "${PID}" 2>/dev/null || true
    fi
    rm -rf "${TMPDIR_PATH}"
}
trap cleanup EXIT INT TERM
fail() { echo "$1" >&2; cat "${LOG}" >&2 2>/dev/null || true; exit 1; }

make -C "${ROOT}" TEST_MODE=m33-ipc
"${QEMU}" -M q35 -cpu "${QEMU_CPU}" -m 128M \
    -cdrom "${ROOT}/build/boringos.iso" -boot d \
    -display none -serial "file:${LOG}" -monitor none \
    -no-reboot -no-shutdown >/dev/null 2>&1 & PID=$!

attempt=0
while [ "${attempt}" -lt 600 ]; do
    grep -Fq 'M33 native IPC/service acceptance passed.' "${LOG}" 2>/dev/null && break
    if grep -Eiq 'M33 IPC acceptance FAILED|ipc-test: FAILED|BoringKernel M33 syscall fatal|Fatal exception' "${LOG}" 2>/dev/null; then
        fail 'M33 IPC QEMU failure marker'
    fi
    kill -0 "${PID}" 2>/dev/null || fail 'QEMU exited before M33 IPC acceptance'
    attempt=$((attempt + 1))
    sleep 0.1
done
grep -Fq 'M33 native IPC/service acceptance passed.' "${LOG}" || fail 'M33 IPC acceptance timeout'

for line in \
    'ipc-test: three distinct processes ready' \
    'ipc-test: service registered' \
    'ipc-test: blocking accept wake passed' \
    'ipc-test: negative syscall paths passed' \
    'ipc-test: M32 shared-buffer grant passed' \
    'ipc-test: queued buffer lifetime passed' \
    'ipc-test: FIFO and queue-full transaction passed' \
    'ipc-test: sender retains capability and alias passed' \
    'ipc-test: peer close passed' \
    'ipc-test: blocking receive wake passed' \
    'ipc-test: process-exit service removal passed' \
    'ipc-test: same-name re-registration passed' \
    'ipc-test: IPC and M32 resources reclaimed' \
    'ipc-test: process-local handle isolation passed' \
    'ipc-test: process-exit cleanup passed'; do
    grep -Fqx "${line}" "${LOG}" || fail "missing M33 witness: ${line}"
done

for pid in 1 2 3; do
    grep -Eq "^ipc-test: enter CPL3 pid ${pid} cr3 0x[0-9a-f]+$" "${LOG}" || fail "missing CPL3/CR3 witness for pid ${pid}"
done
CR3_COUNT=$(grep -E '^ipc-test: enter CPL3 pid [123] cr3 0x[0-9a-f]+$' "${LOG}" | sed -E 's/.* cr3 //' | sort -u | wc -l)
[ "${CR3_COUNT}" -eq 3 ] || fail 'M33 processes did not use three distinct CR3 roots'

ACCOUNTING=$(grep -E '^ipc-test: pmm before=[0-9]+ during=[0-9]+ after=[0-9]+$' "${LOG}" | tail -n 1 || true)
[ -n "${ACCOUNTING}" ] || fail 'missing M33 PMM accounting'
set -- $(printf '%s\n' "${ACCOUNTING}" | sed -E 's/.*before=([0-9]+) during=([0-9]+) after=([0-9]+).*/\1 \2 \3/')
[ "$2" -lt "$1" ] || fail "M33 PMM did not decrease: ${ACCOUNTING}"
[ "$3" -gt "$2" ] || fail "M33 PMM did not recover: ${ACCOUNTING}"

echo "M33 PMM witness: ${ACCOUNTING}"
echo 'Real three-process Ring3 M33 IPC acceptance passed.'
