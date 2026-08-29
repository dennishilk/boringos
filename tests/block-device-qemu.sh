#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
QEMU=${QEMU:-qemu-system-x86_64}
QEMU_CPU=${QEMU_CPU:-qemu64,apic=off}
LOG=$(mktemp)
PID=

cleanup() {
    if [ -n "${PID}" ] && kill -0 "${PID}" 2>/dev/null; then
        kill "${PID}" 2>/dev/null || true
        wait "${PID}" 2>/dev/null || true
    fi
    rm -f "${LOG}"
}
trap cleanup EXIT INT TERM

make -C "${ROOT}" TEST_MODE=block

"${QEMU}" \
    -M q35 \
    -cpu "${QEMU_CPU}" \
    -m 128M \
    -cdrom "${ROOT}/build/boringos.iso" \
    -boot d \
    -display none \
    -serial "file:${LOG}" \
    -monitor none \
    -no-reboot \
    -no-shutdown &
PID=$!

attempt=0
while [ "${attempt}" -lt 200 ]; do
    if grep -Fqx 'BoringKernel block-device test passed.' "${LOG}" 2>/dev/null; then
        break
    fi
    if ! kill -0 "${PID}" 2>/dev/null; then
        break
    fi
    attempt=$((attempt + 1))
    sleep 0.1
done

status=0
for line in \
    'BoringKernel 0.0.53-dev' \
    'BoringKernel physical memory test passed.' \
    'BoringKernel virtual memory test passed.' \
    'BoringKernel heap test passed.' \
    'BoringKernel exception infrastructure test passed.' \
    'Generic block-device layer:' \
    'Registry:' \
    '  initial-state: PASS' \
    '  register: PASS' \
    '  lookup-index: PASS' \
    '  lookup-name: PASS' \
    '  duplicate-reject: PASS' \
    '  invalid-geometry: PASS' \
    'Device testblk0:' \
    '  logical block size: 512' \
    '  blocks: 64' \
    '  capacity: 32768 bytes' \
    '  initial-read: PASS' \
    '  single-block-write: PASS' \
    '  single-block-read-back: PASS' \
    '  multi-block-write-read-back: PASS' \
    '  neighbor-preservation: PASS' \
    'Bounds:' \
    '  first-valid-block: PASS' \
    '  last-valid-block: PASS' \
    '  exact-end-range: PASS' \
    '  zero-count: PASS' \
    '  null-buffer: PASS' \
    '  first-past-end: PASS' \
    '  range-past-end: PASS' \
    '  overflow-safe-extremes: PASS' \
    '  invalid-device: PASS' \
    '  rejected-request-no-backend-call: PASS' \
    'Read-only testblk-ro:' \
    '  logical block size: 4096' \
    '  blocks: 8' \
    '  write-rejected: PASS' \
    '  backend-not-called: PASS' \
    '  data-unchanged: PASS' \
    'Backend error:' \
    '  propagation: PASS' \
    'Registry capacity:' \
    '  max devices: 8' \
    '  bounded-capacity: PASS' \
    'BoringKernel block-device test passed.'
do
    if ! grep -Fqx "${line}" "${LOG}"; then
        echo "missing block-device acceptance line: ${line}" >&2
        status=1
    fi
done

if grep -Eiq 'block-device test FAILED|BoringKernel syscall fatal|Fatal exception: controlled halt|triple fault|reboot' "${LOG}"; then
    echo 'unexpected block-device acceptance failure path' >&2
    status=1
fi

cat "${LOG}"

if [ "${status}" -ne 0 ]; then
    echo 'BoringKernel block-device verification FAILED.' >&2
    exit "${status}"
fi

echo 'BoringKernel block-device QEMU verification passed.'
