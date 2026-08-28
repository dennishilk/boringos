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

make -C "${ROOT}" TEST_MODE=ramfs

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
    if grep -Fqx 'BoringKernel RAMFS test passed.' "${LOG}" 2>/dev/null; then
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
    'BoringKernel 0.0.42-dev' \
    'BoringKernel physical memory test passed.' \
    'BoringKernel virtual memory test passed.' \
    'BoringKernel heap test passed.' \
    'IDT: loaded' \
    'Exceptions: online' \
    'BoringKernel exception infrastructure test passed.' \
    'RAMFS backend:' \
    '  filesystem-create: PASS' \
    '  root-create: PASS' \
    '  root-vfs-init: PASS' \
    'Directory semantics:' \
    '  mkdir: PASS' \
    '  nested-mkdir: PASS' \
    '  duplicate-name-reject: PASS' \
    '  nested-resolve: PASS' \
    'File semantics:' \
    '  create-file: PASS' \
    '  zero-length-create: PASS' \
    '  write-roundtrip: PASS' \
    '  overwrite: PASS' \
    '  write-past-eof-zero-gap: PASS' \
    '  partial-read: PASS' \
    '  eof-read: PASS' \
    '  file-size-bound: PASS' \
    '  failed-growth-preserves-data: PASS' \
    'Truncate:' \
    '  truncate-shrink: PASS' \
    '  truncate-grow: PASS' \
    '  truncate-grow-zero-fill: PASS' \
    '  truncate-zero: PASS' \
    '  truncate-bound-reject: PASS' \
    'Directory enumeration:' \
    '  readdir-live-state: PASS' \
    '  readdir-after-rename: PASS' \
    '  readdir-after-unlink: PASS' \
    '  readdir-end: PASS' \
    'Rename:' \
    '  rename-file: PASS' \
    '  move-file: PASS' \
    '  rename-directory: PASS' \
    '  move-directory: PASS' \
    '  preserve-file-content: PASS' \
    '  descendant-cycle-reject: PASS' \
    '  collision-reject: PASS' \
    'Removal:' \
    '  unlink-regular: PASS' \
    '  unlink-directory-reject: PASS' \
    '  unlink-busy-reject: PASS' \
    '  rmdir-nonempty-reject: PASS' \
    '  rmdir-empty: PASS' \
    '  name-reuse: PASS' \
    'CWD:' \
    '  process-a-cwd: PASS' \
    '  process-b-cwd: PASS' \
    '  process-relative-distinct: PASS' \
    '  process-relative-content: PASS' \
    '  process-cwd-release: PASS' \
    'Mount:' \
    '  second-ramfs-create: PASS' \
    '  child-mount: PASS' \
    '  child-resolve: PASS' \
    '  mount-dotdot: PASS' \
    '  cross-filesystem-rename: PASS' \
    'Capacity / safety:' \
    '  node-or-data-capacity: PASS' \
    '  invalid-object-reject: PASS' \
    'Cleanup:' \
    '  handles-closed: PASS' \
    '  paths-released: PASS' \
    '  process-cwd-release: PASS' \
    '  vfs-shutdown: PASS' \
    '  child-ramfs-destroy: PASS' \
    '  root-ramfs-destroy: PASS' \
    '  heap-used-restored: PASS' \
    '  heap-allocation-count-restored: PASS' \
    'RAMFS node max: 32' \
    'RAMFS file max: 8192' \
    'RAMFS total data max: 32768' \
    'BoringKernel RAMFS test passed.'
do
    if ! grep -Fqx "${line}" "${LOG}"; then
        echo "missing RAMFS acceptance line: ${line}" >&2
        status=1
    fi
done

if grep -Eiq 'RAMFS test FAILED|BoringKernel syscall fatal|Fatal exception: controlled halt|triple fault|reboot' "${LOG}"; then
    echo 'unexpected RAMFS acceptance failure path' >&2
    status=1
fi

cat "${LOG}"

if [ "${status}" -ne 0 ]; then
    echo 'BoringKernel RAMFS verification FAILED.' >&2
    exit "${status}"
fi

echo 'BoringKernel RAMFS verification passed.'
