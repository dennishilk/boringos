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

make -C "${ROOT}" TEST_MODE=syscall

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
    if grep -Fqx 'BoringKernel syscall boundary test passed.' "${LOG}" 2>/dev/null; then
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
    'BoringKernel 0.0.55-dev' \
    'BoringKernel physical memory test passed.' \
    'BoringKernel virtual memory test passed.' \
    'BoringKernel heap test passed.' \
    'IDT: loaded' \
    'Exceptions: online' \
    'BoringKernel exception infrastructure test passed.' \
    'Syscall test:' \
    '  msr-config: PASS' \
    '  gdt-selectors: PASS' \
    '  user-code-mapped: PASS' \
    '  user-stack-mapped: PASS' \
    '  higher-half-supervisor-only: PASS' \
    'User code VA: 0x0000000040000000' \
    'User stack base: 0x0000000040010000' \
    'User stack top: 0x0000000040011000' \
    'User message VA: 0x0000000040010200' \
    'Entering syscall CPL3 payload with IRETQ.' \
    'Syscall DEBUG_WRITE: hello from boring syscall' \
    'Syscall return evidence:' \
    'Final fault vector: 13' \
    'Final saved CS: 0x0000000000000023' \
    'Final saved user RSP: 0x0000000040011000' \
    '  entered-cpl3: PASS' \
    '  syscall-entered-cpl0: PASS' \
    '  syscall-kernel-stack: PASS' \
    '  getpid: PASS' \
    '  valid-user-copy: PASS' \
    '  unmapped-user-pointer-rejected: PASS' \
    '  kernel-pointer-rejected: PASS' \
    '  overflowing-user-range-rejected: PASS' \
    '  oversized-length-rejected: PASS' \
    '  unknown-syscall: PASS' \
    '  fd-invalid-rejected: PASS' \
    '  fd-read-stdout-rejected: PASS' \
    '  fd-write-stdin-rejected: PASS' \
    '  fd-invalid-pointer-rejected: PASS' \
    '  fd-oversized-transfer-rejected: PASS' \
    '  input-non-owner-read-rejected: PASS' \
    '  input-non-owner-release-rejected: PASS' \
    '  input-claim: PASS' \
    '  input-double-claim: PASS' \
    '  input-invalid-pointer-rejected: PASS' \
    '  input-zero-count-rejected: PASS' \
    '  input-oversized-count-rejected: PASS' \
    '  input-release: PASS' \
    '  sysret-cpl3: PASS' \
    '  user-rsp-restored: PASS' \
    '  callee-saved-preserved: PASS' \
    '  final-cpl3-proof: PASS' \
    '  final-tss-rsp0: PASS' \
    'GETPID result: 1' \
    'DEBUG_WRITE result: 25' \
    'Syscall dispatches: 20' \
    'BoringKernel syscall boundary test passed.'
do
    if ! grep -Fqx "${line}" "${LOG}"; then
        echo "missing syscall line: ${line}" >&2
        status=1
    fi
done

for pattern in \
    '^IA32_EFER: 0x[0-9A-F]{16}$' \
    '^IA32_STAR: 0x0010000800000000$' \
    '^IA32_LSTAR: 0xFFFF[0-9A-F]{12}$' \
    '^IA32_FMASK: 0x0000000000044700$' \
    '^Syscall stack base: 0xFFFF[0-9A-F]{12}$' \
    '^Syscall stack top: 0xFFFF[0-9A-F]{12}$' \
    '^Live syscall kernel RSP: 0xFFFF[0-9A-F]{12}$' \
    '^Saved syscall user RSP: 0x0000000040011000$' \
    '^Final saved user RIP: 0x000000004000[0-9A-F]{4}$' \
    '^Final exception kernel RSP: 0xFFFF[0-9A-F]{12}$'
do
    if ! grep -Eq "${pattern}" "${LOG}"; then
        echo "missing syscall runtime value matching: ${pattern}" >&2
        status=1
    fi
done

if grep -Eiq 'Syscall self-test FAILED|BoringKernel syscall fatal|triple fault|reboot' "${LOG}"; then
    echo 'unexpected syscall failure path' >&2
    status=1
fi

cat "${LOG}"

if [ "${status}" -ne 0 ]; then
    echo 'BoringKernel syscall verification FAILED.' >&2
    exit "${status}"
fi

echo 'BoringKernel syscall verification passed.'
