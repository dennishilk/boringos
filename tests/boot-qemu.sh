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
    if grep -Fqx 'BoringKernel exception infrastructure test passed.' "${LOG}" 2>/dev/null; then
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
    'BoringKernel 0.0.5-dev' \
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
    'BoringKernel exception infrastructure test passed.'
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
if ! grep -Eq '^Active root table: [1-9][0-9]*$' "${LOG}"; then
    echo 'missing or invalid runtime CR3 root-table value' >&2
    status=1
fi
if ! grep -Eq '^HHDM offset: [1-9][0-9]*$' "${LOG}"; then
    echo 'missing or invalid runtime HHDM offset' >&2
    status=1
fi
if ! grep -Eq '^Test virtual address: [1-9][0-9]*$' "${LOG}"; then
    echo 'missing or invalid VMM test virtual address' >&2
    status=1
fi
if ! grep -Eq '^Page-table frames allocated: [0-9]+$' "${LOG}"; then
    echo 'missing runtime page-table allocation count' >&2
    status=1
fi
if ! grep -Eq '^Test physical frame: [1-9][0-9]*$' "${LOG}"; then
    echo 'missing runtime VMM test physical frame' >&2
    status=1
fi
if ! grep -Eq '^Translation result: [1-9][0-9]*$' "${LOG}"; then
    echo 'missing runtime VMM translation result' >&2
    status=1
fi
if ! grep -Eq '^Virtual base: [1-9][0-9]*$' "${LOG}"; then
    echo 'missing or invalid heap virtual base' >&2
    status=1
fi
if ! grep -Eq '^Virtual limit: [1-9][0-9]*$' "${LOG}"; then
    echo 'missing or invalid heap virtual limit' >&2
    status=1
fi
if ! grep -Eq '^Free payload: [1-9][0-9]* bytes$' "${LOG}"; then
    echo 'missing or invalid initial heap free-payload value' >&2
    status=1
fi
if ! grep -Eq '^Initial PMM frames consumed: [1-9][0-9]*$' "${LOG}"; then
    echo 'missing or invalid initial heap PMM-frame count' >&2
    status=1
fi
if ! grep -Eq '^Initial page-table frames: [0-9]+$' "${LOG}"; then
    echo 'missing initial heap page-table-frame count' >&2
    status=1
fi
if ! grep -Eq '^Growth mappings created: [1-9][0-9]*$' "${LOG}"; then
    echo 'heap self-test did not prove mapping growth' >&2
    status=1
fi
if ! grep -Eq '^Growth PMM frames consumed: [1-9][0-9]*$' "${LOG}"; then
    echo 'heap self-test did not prove PMM-backed growth' >&2
    status=1
fi
if ! grep -Eq '^Final mapped pages: [3-9][0-9]*$' "${LOG}"; then
    echo 'heap self-test did not retain grown mapped capacity' >&2
    status=1
fi
if ! grep -Eq '^Final free bytes: [1-9][0-9]*$' "${LOG}"; then
    echo 'missing or invalid final heap free-byte count' >&2
    status=1
fi
if ! grep -Eq '^IDTR base: 0x[0-9A-F]{16}$' "${LOG}"; then
    echo 'missing or invalid loaded IDTR base' >&2
    status=1
fi
if ! grep -Eq '^Code selector: 0x[0-9A-F]{16}$' "${LOG}" ||
   grep -Fqx 'Code selector: 0x0000000000000000' "${LOG}"; then
    echo 'missing or invalid kernel code selector' >&2
    status=1
fi

if grep -Eiq 'PMM self-test FAILED|Physical memory manager: FAILED|VMM: FAILED|VMM self-test FAILED|Kernel heap: FAILED|Heap self-test FAILED|Exception handling: FAILED|heap corruption|BoringKernel exception|Fatal exception|page fault|general protection fault' "${LOG}"; then
    echo 'kernel reported a failure during normal boot' >&2
    status=1
fi

cat "${LOG}"

if [ "${status}" -ne 0 ]; then
    echo 'BoringKernel QEMU boot verification FAILED.' >&2
    exit "${status}"
fi

echo 'BoringKernel QEMU boot verification passed.'
