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

sh "${ROOT}/tests/elf-build-audit.sh"
make -C "${ROOT}" TEST_MODE=elf

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
    if grep -Fqx 'BoringKernel ELF userspace loader test passed.' "${LOG}" 2>/dev/null; then
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
    'BoringKernel 0.0.48-dev' \
    'BoringKernel physical memory test passed.' \
    'BoringKernel virtual memory test passed.' \
    'BoringKernel heap test passed.' \
    'IDT: loaded' \
    'Exceptions: online' \
    'BoringKernel exception infrastructure test passed.' \
    'ELF userspace loader test:' \
    '  boot-module-found: PASS' \
    '  elf64-header: PASS' \
    '  program-table-bounds: PASS' \
    '  malformed-magic-rejected: PASS' \
    '  elf32-rejected: PASS' \
    '  wrong-endian-rejected: PASS' \
    '  wrong-machine-rejected: PASS' \
    '  unsupported-type-rejected: PASS' \
    '  truncated-phdr-rejected: PASS' \
    '  filesz-memsz-validation: PASS' \
    '  file-range-rejected: PASS' \
    '  virtual-overflow-rejected: PASS' \
    '  higher-half-segment-rejected: PASS' \
    '  overlapping-segment-rejected: PASS' \
    '  wx-segment-rejected: PASS' \
    '  entry-outside-exec-rejected: PASS' \
    '  malformed-no-allocation: PASS' \
    '  nx-enabled: PASS' \
    '  process-created: PASS' \
    '  bounded-stack-mapping: PASS' \
    '  load-segments: PASS' \
    '  segment-permissions: PASS' \
    '  entry-executable: PASS' \
    '  bss-zeroed: PASS' \
    '  user-stack-mapped: PASS' \
    '  higher-half-supervisor-only: PASS' \
    'ELF entry: 0x0000000040000000' \
    'Program headers: 3' \
    'PT_LOAD segments: 3' \
    'Process PID: 1' \
    'User stack base: 0x0000000040010000' \
    'User stack top: 0x0000000040011000' \
    'Entering ELF userspace at CPL3.' \
    'Syscall DEBUG_WRITE: hello from BoringOS ELF userspace' \
    'ELF userspace:' \
    '  entered-cpl3: PASS' \
    '  bss-initial-zero: PASS' \
    '  bss-write: PASS' \
    '  getpid: PASS' \
    '  debug-write: PASS' \
    '  sysret-resume: PASS' \
    '  final-cpl3-proof: PASS' \
    '  final-tss-rsp0: PASS' \
    'ELF GETPID result: 1' \
    'ELF DEBUG_WRITE result: 33' \
    'ELF syscall dispatches: 2' \
    '  cleanup: PASS' \
    'BoringKernel ELF userspace loader test passed.'
do
    if ! grep -Fqx "${line}" "${LOG}"; then
        echo "missing ELF acceptance line: ${line}" >&2
        status=1
    fi
done

for pattern in \
    '^ELF module size: [1-9][0-9]* bytes$' \
    '^Process CR3: 0x[0-9A-F]{16}$' \
    '^PT_LOAD 0: 0x0000000040000000 - 0x000000004000[0-9A-F]{4} filesz=[1-9][0-9]* memsz=[1-9][0-9]* flags=R-X$' \
    '^PT_LOAD 1: 0x0000000040001000 - 0x000000004000[0-9A-F]{4} filesz=[1-9][0-9]* memsz=[1-9][0-9]* flags=R--$' \
    '^PT_LOAD 2: 0x0000000040002000 - 0x000000004000[0-9A-F]{4} filesz=[1-9][0-9]* memsz=[1-9][0-9]* flags=RW-$' \
    '^ELF final fault RIP: 0x000000004000[0-9A-F]{4}$'
do
    if ! grep -Eq "${pattern}" "${LOG}"; then
        echo "missing ELF runtime value matching: ${pattern}" >&2
        status=1
    fi
done

if grep -Eiq 'ELF userspace loader self-test FAILED|BoringKernel syscall fatal|triple fault|reboot' "${LOG}"; then
    echo 'unexpected ELF acceptance failure path' >&2
    status=1
fi

cat "${LOG}"

if [ "${status}" -ne 0 ]; then
    echo 'BoringKernel ELF userspace verification FAILED.' >&2
    exit "${status}"
fi

echo 'BoringKernel ELF userspace verification passed.'
