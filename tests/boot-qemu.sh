#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
QEMU=${QEMU:-qemu-system-x86_64}
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

make -C "${ROOT}"

"${QEMU}" \
    -M q35 \
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
while [ "${attempt}" -lt 100 ]; do
    if grep -Fqx 'BoringKernel physical memory test passed.' "${LOG}" 2>/dev/null; then
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
    'BoringOS booting...' \
    'BoringKernel 0.0.2-dev' \
    'Arch: x86_64' \
    'Hello from BoringKernel.' \
    'Physical memory manager:' \
    'Page size: 4096 bytes' \
    'PMM: online' \
    'PMM self-test:' \
    '  allocate: PASS' \
    '  unique: PASS' \
    '  aligned: PASS' \
    '  usable: PASS' \
    '  free: PASS' \
    '  invalid-free: PASS' \
    '  bookkeeping: PASS' \
    'BoringKernel physical memory test passed.'
do
    if ! grep -Fqx "${line}" "${LOG}"; then
        echo "missing serial line: ${line}" >&2
        status=1
    fi
done

if ! grep -Eq '^Usable memory: [1-9][0-9]* bytes$' "${LOG}"; then
    echo 'missing or invalid runtime usable-memory value' >&2
    status=1
fi

if ! grep -Eq '^Usable frames: [1-9][0-9]*$' "${LOG}"; then
    echo 'missing or invalid runtime usable-frame count' >&2
    status=1
fi

if grep -Fq 'PMM self-test FAILED' "${LOG}"; then
    echo 'kernel reported a PMM self-test failure' >&2
    status=1
fi

if grep -Fq 'Physical memory manager: FAILED' "${LOG}"; then
    echo 'kernel reported PMM initialization failure' >&2
    status=1
fi

cat "${LOG}"

if [ "${status}" -ne 0 ]; then
    echo 'BoringKernel QEMU boot verification FAILED.' >&2
    exit "${status}"
fi

echo 'BoringKernel QEMU boot verification passed.'
