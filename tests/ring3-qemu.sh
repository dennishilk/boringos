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

make -C "${ROOT}" TEST_MODE=ring3

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
while [ "${attempt}" -lt 120 ]; do
    if grep -Fqx 'BoringKernel Ring 3 test passed.' "${LOG}" 2>/dev/null; then
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
    'BoringKernel 0.0.20-dev' \
    'BoringKernel physical memory test passed.' \
    'BoringKernel virtual memory test passed.' \
    'BoringKernel heap test passed.' \
    'IDT: loaded' \
    'Exceptions: online' \
    'BoringKernel exception infrastructure test passed.' \
    'Ring 3 test:' \
    '  gdt: PASS' \
    '  tss-loaded: PASS' \
    '  user-code-mapped: PASS' \
    '  user-stack-mapped: PASS' \
    '  higher-half-supervisor-only: PASS' \
    'User code VA: 0x0000000040000000' \
    'User stack base: 0x0000000040010000' \
    'User stack top: 0x0000000040011000' \
    'Entering real CPL3 with IRETQ.' \
    'Ring 3 fault evidence:' \
    'Fault vector: 13' \
    'Saved CS: 0x0000000000000023' \
    'Saved SS: 0x000000000000001B' \
    'Hardware user SS: 0x000000000000001B' \
    '  entered-cpl3: PASS' \
    '  privileged-operation-blocked: PASS' \
    '  exception-origin-cpl3: PASS' \
    '  normalized-user-stack-frame: PASS' \
    '  kernel-stack-transition: PASS' \
    '  user-rip-preserved: PASS' \
    '  user-rsp-preserved: PASS' \
    '  user-ss-preserved: PASS' \
    '  user-stack-write: PASS' \
    'BoringKernel Ring 3 test passed.'
do
    if ! grep -Fqx "${line}" "${LOG}"; then
        echo "missing Ring 3 line: ${line}" >&2
        status=1
    fi
done

for pattern in \
    '^GDT base: 0x[0-9A-F]{16}$' \
    '^Kernel CS: 0x0000000000000008$' \
    '^Kernel SS: 0x0000000000000010$' \
    '^User CS: 0x0000000000000023$' \
    '^User SS: 0x000000000000001B$' \
    '^TR: 0x0000000000000028$' \
    '^TSS RSP0: 0x[0-9A-F]{16}$' \
    '^RSP0 stack base: 0x[0-9A-F]{16}$' \
    '^RSP0 stack top: 0x[0-9A-F]{16}$' \
    '^Saved user RIP: 0x000000004000[0-9A-F]{4}$' \
    '^Saved user RSP: 0x0000000040011000$' \
    '^Hardware user RSP: 0x0000000040011000$' \
    '^Kernel handler RSP: 0x[0-9A-F]{16}$'
do
    if ! grep -Eq "${pattern}" "${LOG}"; then
        echo "missing Ring 3 runtime value matching: ${pattern}" >&2
        status=1
    fi
done

if grep -Eiq 'Ring 3 self-test FAILED|Exception handling: FAILED|triple fault|reboot' "${LOG}"; then
    echo 'unexpected Ring 3 failure path' >&2
    status=1
fi

cat "${LOG}"

if [ "${status}" -ne 0 ]; then
    echo 'BoringKernel Ring 3 verification FAILED.' >&2
    exit "${status}"
fi

echo 'BoringKernel Ring 3 verification passed.'
