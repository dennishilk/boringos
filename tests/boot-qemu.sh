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

make -C "${ROOT}" TEST_MODE=normal

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
    if grep -Fqx 'BoringKernel hardware interrupt test passed.' "${LOG}" 2>/dev/null; then
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
    'BoringKernel 0.0.6-dev' \
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
    'BoringKernel physical memory test passed.' \
    'Virtual memory manager:' \
    'Paging: x86_64 4-level' \
    'VMM: online' \
    'VMM self-test:' \
    '  frame-allocation: PASS' \
    '  unmapped-check: PASS' \
    '  map: PASS' \
    '  translate: PASS' \
    '  write-read: PASS' \
    '  unmap: PASS' \
    '  frame-release: PASS' \
    'Test pattern: 0x424F52494E474F53' \
    'BoringKernel virtual memory test passed.' \
    'Kernel heap:' \
    'Mapped pages: 2' \
    'Mapped capacity: 8192 bytes' \
    'Alignment: 16 bytes' \
    'Heap: online' \
    'Heap self-test:' \
    '  allocate: PASS' \
    '  alignment: PASS' \
    '  non-overlap: PASS' \
    '  write-read: PASS' \
    '  growth: PASS' \
    '  free: PASS' \
    '  reuse: PASS' \
    '  double-free: PASS' \
    '  invalid-free: PASS' \
    '  bookkeeping: PASS' \
    'Allocation sizes tested: 1,16,64,200,6000,4096 bytes' \
    'Final used bytes: 0' \
    'BoringKernel heap test passed.' \
    'Exception handling:' \
    'IDT entries: 32' \
    'IDTR limit: 4095' \
    'IDT: loaded' \
    'Exceptions: online' \
    'BoringKernel exception infrastructure test passed.' \
    'Hardware interrupts:' \
    'Controller: 8259 PIC' \
    'PIC: remapped' \
    'Master vectors: 32-39' \
    'Slave vectors: 40-47' \
    'Initial master mask: 255' \
    'Initial slave mask: 255' \
    'IRQ0 vector: 32' \
    'Master mask: 254' \
    'Slave mask: 255' \
    'Timer:' \
    'Source: PIT channel 0' \
    'Input frequency: 1193182 Hz' \
    'Requested frequency: 100 Hz' \
    'Divisor: 11932' \
    'Effective frequency: 99998 mHz' \
    'IRQ: 0' \
    'Vector: 32' \
    'Timer: online' \
    'Interrupts: enabled' \
    'IRQ self-test:' \
    '  timer-delivery: PASS' \
    '  repeated-irqs: PASS' \
    '  acknowledgement: PASS' \
    'Unexpected IRQs: 0' \
    'BoringKernel hardware interrupt test passed.'
do
    if ! grep -Fqx "${line}" "${LOG}"; then
        echo "missing serial line: ${line}" >&2
        status=1
    fi
done

for pattern in \
    '^Usable memory: [1-9][0-9]* bytes$' \
    '^Usable frames: [1-9][0-9]*$' \
    '^Active root table: [1-9][0-9]*$' \
    '^HHDM offset: [1-9][0-9]*$' \
    '^Test virtual address: [1-9][0-9]*$' \
    '^Page-table frames allocated: [0-9]+$' \
    '^Test physical frame: [1-9][0-9]*$' \
    '^Translation result: [1-9][0-9]*$' \
    '^Virtual base: [1-9][0-9]*$' \
    '^Virtual limit: [1-9][0-9]*$' \
    '^Free payload: [1-9][0-9]* bytes$' \
    '^Initial PMM frames consumed: [1-9][0-9]*$' \
    '^Initial page-table frames: [0-9]+$' \
    '^Growth mappings created: [1-9][0-9]*$' \
    '^Growth PMM frames consumed: [1-9][0-9]*$' \
    '^Final mapped pages: [3-9][0-9]*$' \
    '^Final free bytes: [1-9][0-9]*$' \
    '^IDTR base: 0x[0-9A-F]{16}$' \
    '^Code selector: 0x[0-9A-F]{16}$'
do
    if ! grep -Eq "${pattern}" "${LOG}"; then
        echo "missing runtime value matching: ${pattern}" >&2
        status=1
    fi
done

TICKS=$(sed -n 's/^Ticks observed: \([0-9][0-9]*\)$/\1/p' "${LOG}" | tail -n 1)
DELIVERIES=$(sed -n 's/^IRQ0 deliveries: \([0-9][0-9]*\)$/\1/p' "${LOG}" | tail -n 1)
if [ -z "${TICKS}" ] || [ "${TICKS}" -lt 10 ]; then
    echo 'timer self-test did not observe at least 10 ticks' >&2
    status=1
fi
if [ -z "${DELIVERIES}" ] || [ "${DELIVERIES}" -lt 10 ]; then
    echo 'IRQ self-test did not observe at least 10 IRQ0 deliveries' >&2
    status=1
fi

if grep -Eiq 'PMM self-test FAILED|Physical memory manager: FAILED|VMM: FAILED|VMM self-test FAILED|Kernel heap: FAILED|Heap self-test FAILED|Exception handling: FAILED|Hardware interrupts: FAILED|Timer: FAILED|Hardware interrupt self-test FAILED|heap corruption|Fatal exception: controlled halt|general protection fault|triple fault|reboot' "${LOG}"; then
    echo 'kernel reported a failure during normal boot' >&2
    status=1
fi

cat "${LOG}"

if [ "${status}" -ne 0 ]; then
    echo 'BoringKernel QEMU boot verification FAILED.' >&2
    exit "${status}"
fi

echo 'BoringKernel QEMU boot verification passed.'
