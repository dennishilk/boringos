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

sh "${ROOT}/tests/runtime-build-audit.sh"
make -C "${ROOT}" TEST_MODE=runtime

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
while [ "${attempt}" -lt 150 ]; do
    if grep -Fqx 'BoringKernel native C runtime test passed.' "${LOG}" 2>/dev/null; then
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
    'BoringKernel 0.0.18-dev' \
    'BoringKernel physical memory test passed.' \
    'BoringKernel virtual memory test passed.' \
    'BoringKernel heap test passed.' \
    'IDT: loaded' \
    'Exceptions: online' \
    'BoringKernel exception infrastructure test passed.' \
    'Native userspace runtime test:' \
    '  boot-module-found: PASS' \
    '  elf64-runtime-image: PASS' \
    '  nx-enabled: PASS' \
    '  process-created: PASS' \
    '  load-runtime-image: PASS' \
    '  segment-permissions: PASS' \
    '  entry-executable: PASS' \
    '  bss-loader-zeroed: PASS' \
    '  user-stack-mapped: PASS' \
    '  higher-half-supervisor-only: PASS' \
    'Runtime ELF entry: 0x0000000040000000' \
    'Runtime PT_LOAD segments: 3' \
    'Runtime process PID: 1' \
    'Runtime user stack base: 0x0000000040010000' \
    'Runtime user stack top: 0x0000000040011000' \
    'Entering BoringOS C runtime at CPL3.' \
    'Syscall DEBUG_WRITE: hello from BoringOS C userspace' \
    'Native C userspace:' \
    '  c-entry: PASS' \
    '  initialized-data: PASS' \
    '  bss-initial-zero: PASS' \
    '  bss-write: PASS' \
    '  local-stack: PASS' \
    '  strlen: PASS' \
    '  memset: PASS' \
    '  memcpy: PASS' \
    '  getpid: PASS' \
    '  debug-write: PASS' \
    '  sysret-resume: PASS' \
    '  boring-main-return: PASS' \
    '  final-cpl3-proof: PASS' \
    '  final-tss-rsp0: PASS' \
    'Runtime boring_main return: 42' \
    'Runtime GETPID result: 1' \
    'Runtime DEBUG_WRITE result: 31' \
    'Runtime syscall dispatches: 2' \
    '  cleanup: PASS' \
    'BoringKernel native C runtime test passed.'
do
    if ! grep -Fqx "${line}" "${LOG}"; then
        echo "missing runtime acceptance line: ${line}" >&2
        status=1
    fi
done

for pattern in \
    '^Runtime module size: [1-9][0-9]* bytes$' \
    '^Runtime program headers: [1-9][0-9]*$' \
    '^Runtime process CR3: 0x[0-9A-F]{16}$' \
    '^Runtime PT_LOAD 0: 0x0000000040000000 - 0x000000004000[0-9A-F]{4} filesz=[1-9][0-9]* memsz=[1-9][0-9]* flags=R-X$' \
    '^Runtime PT_LOAD 1: 0x0000000040001000 - 0x000000004000[0-9A-F]{4} filesz=[1-9][0-9]* memsz=[1-9][0-9]* flags=R--$' \
    '^Runtime PT_LOAD 2: 0x0000000040002000 - 0x000000004000[0-9A-F]{4} filesz=[1-9][0-9]* memsz=[1-9][0-9]* flags=RW-$' \
    '^Runtime final fault RIP: 0x000000004000[0-9A-F]{4}$'
do
    if ! grep -Eq "${pattern}" "${LOG}"; then
        echo "missing runtime value matching: ${pattern}" >&2
        status=1
    fi
done

if grep -Eiq 'Native userspace runtime self-test FAILED|BoringKernel syscall fatal|triple fault|reboot' "${LOG}"; then
    echo 'unexpected native runtime acceptance failure path' >&2
    status=1
fi

cat "${LOG}"

if [ "${status}" -ne 0 ]; then
    echo 'BoringKernel native C runtime verification FAILED.' >&2
    exit "${status}"
fi

echo 'BoringKernel native C runtime verification passed.'
