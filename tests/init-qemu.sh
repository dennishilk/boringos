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

make -C "${ROOT}" TEST_MODE=init

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
    if grep -Fqx 'boring-init: online' "${LOG}" 2>/dev/null; then
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
    'BoringKernel 0.0.52-dev' \
    'BoringKernel physical memory test passed.' \
    'BoringKernel virtual memory test passed.' \
    'BoringKernel heap test passed.' \
    'IDT: loaded' \
    'Exceptions: online' \
    'BoringKernel exception infrastructure test passed.' \
    'boring-init launch:' \
    '  boot-module-found: PASS' \
    '  elf64-init-image: PASS' \
    '  nx-enabled: PASS' \
    '  process-created: PASS' \
    '  load-init-image: PASS' \
    '  entry-executable: PASS' \
    '  user-stack-mapped: PASS' \
    '  higher-half-supervisor-only: PASS' \
    'boring-init ELF entry: 0x0000000040000000' \
    'boring-init process PID: 1' \
    'boring-init user stack top: 0x0000000040011000' \
    'Entering boring-init at CPL3.' \
    'boring-init: starting' \
    'boring-init: pid 1' \
    'boring-init: online'
do
    if ! grep -Fqx "${line}" "${LOG}"; then
        echo "missing boring-init acceptance line: ${line}" >&2
        status=1
    fi
done

if ! grep -Eq '^boring-init module size: [1-9][0-9]* bytes$' "${LOG}"; then
    echo 'missing boring-init module-size evidence' >&2
    status=1
fi

if grep -Eiq 'boring-init: FAILED|boring-init acceptance FAILED|BoringKernel syscall fatal|Fatal exception: controlled halt|triple fault|reboot' "${LOG}"; then
    echo 'unexpected boring-init failure path' >&2
    status=1
fi

cat "${LOG}"

if [ "${status}" -ne 0 ]; then
    echo 'BoringKernel boring-init verification FAILED.' >&2
    exit "${status}"
fi

echo 'BoringKernel boring-init verification passed.'
