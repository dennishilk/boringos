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

make -C "${ROOT}" TEST_MODE=vfs

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
    if grep -Fqx 'BoringKernel VFS core test passed.' "${LOG}" 2>/dev/null; then
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
    'BoringKernel 0.0.21-dev' \
    'BoringKernel physical memory test passed.' \
    'BoringKernel virtual memory test passed.' \
    'BoringKernel heap test passed.' \
    'IDT: loaded' \
    'Exceptions: online' \
    'BoringKernel exception infrastructure test passed.' \
    'VFS core:' \
    '  root-path: PASS' \
    '  absolute-path: PASS' \
    '  repeated-slash: PASS' \
    '  dot: PASS' \
    '  dotdot-root: PASS' \
    '  relative-path: PASS' \
    '  process-cwd: PASS' \
    '  path-bounds: PASS' \
    '  component-bounds: PASS' \
    '  non-directory-lookup: PASS' \
    '  mount-enter: PASS' \
    '  mount-leave: PASS' \
    '  mount-validation: PASS' \
    '  lookup-dispatch: PASS' \
    '  create-dispatch: PASS' \
    '  mkdir-dispatch: PASS' \
    '  unlink-dispatch: PASS' \
    '  rmdir-dispatch: PASS' \
    '  rename-dispatch: PASS' \
    '  cross-fs-rename: PASS' \
    '  handle-open: PASS' \
    '  read-dispatch: PASS' \
    '  write-dispatch: PASS' \
    '  handle-offset: PASS' \
    '  truncate-dispatch: PASS' \
    '  readdir-dispatch: PASS' \
    '  backend-validation: PASS' \
    '  cleanup: PASS' \
    'VFS path max: 1024' \
    'VFS name max: 255' \
    'VFS mount slots: 8' \
    'VFS I/O max: 4096' \
    'BoringKernel VFS core test passed.'
do
    if ! grep -Fqx "${line}" "${LOG}"; then
        echo "missing VFS acceptance line: ${line}" >&2
        status=1
    fi
done

if ! grep -Eq '^VFS PMM free frames restored: [1-9][0-9]*$' "${LOG}"; then
    echo 'missing VFS PMM restoration measurement' >&2
    status=1
fi

if grep -Eiq 'VFS core test FAILED|BoringKernel syscall fatal|triple fault|reboot' "${LOG}"; then
    echo 'unexpected VFS acceptance failure path' >&2
    status=1
fi

cat "${LOG}"

if [ "${status}" -ne 0 ]; then
    echo 'BoringKernel VFS core verification FAILED.' >&2
    exit "${status}"
fi

echo 'BoringKernel VFS core verification passed.'
