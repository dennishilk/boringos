#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
QEMU=${QEMU:-qemu-system-x86_64}
QEMU_CPU=${QEMU_CPU:-qemu64,apic=off}
LOG=$(mktemp)
PID=
EXPECTED_CR2=0xFFFFFF0000000000

cleanup() {
    if [ -n "${PID}" ] && kill -0 "${PID}" 2>/dev/null; then
        kill "${PID}" 2>/dev/null || true
        wait "${PID}" 2>/dev/null || true
    fi
    rm -f "${LOG}"
}
trap cleanup EXIT INT TERM

make -C "${ROOT}" TEST_MODE=pagefault

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
while [ "${attempt}" -lt 100 ]; do
    if grep -Fqx 'Fatal exception: controlled halt.' "${LOG}" 2>/dev/null; then
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
    'BoringKernel 0.0.57-dev' \
    'BoringKernel physical memory test passed.' \
    'BoringKernel virtual memory test passed.' \
    'BoringKernel heap test passed.' \
    'IDT: loaded' \
    'Exceptions: online' \
    'BoringKernel exception infrastructure test passed.' \
    'Exception test mode: pagefault' \
    "Expected CR2: ${EXPECTED_CR2}" \
    'Triggering real Page Fault.' \
    'BoringKernel exception' \
    'Vector: 14' \
    'Name: Page Fault' \
    'Error code: 0x0000000000000000' \
    "CR2: ${EXPECTED_CR2}" \
    'Page fault mapping: non-present' \
    'Page fault access: read' \
    'Page fault privilege: supervisor' \
    'Page fault reserved-bit violation: no' \
    'Page fault instruction fetch: no' \
    'Fatal exception: controlled halt.'
do
    if ! grep -Fqx "${line}" "${LOG}"; then
        echo "missing page-fault line: ${line}" >&2
        status=1
    fi
done

if ! grep -Eq '^RIP: 0x[0-9A-F]{16}$' "${LOG}"; then
    echo 'missing page-fault RIP' >&2
    status=1
fi
if ! grep -Eq '^RSP: 0x[0-9A-F]{16}$' "${LOG}"; then
    echo 'missing page-fault RSP' >&2
    status=1
fi
if grep -Fq 'Vector: 0' "${LOG}" ||
   grep -Fq 'Page Fault test setup FAILED' "${LOG}" ||
   grep -Eiq 'Exception handling: FAILED|triple fault|reboot' "${LOG}"; then
    echo 'unexpected exception path during page-fault test' >&2
    status=1
fi

cat "${LOG}"

if [ "${status}" -ne 0 ]; then
    echo 'BoringKernel page-fault verification FAILED.' >&2
    exit "${status}"
fi

echo 'BoringKernel page-fault verification passed.'
