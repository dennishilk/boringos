#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
QEMU=${QEMU:-qemu-system-x86_64}
QEMU_CPU=${QEMU_CPU:-qemu64,apic=off}
TMPDIR_PATH=$(mktemp -d)
SERIAL_BASE="${TMPDIR_PATH}/serial"
SERIAL_IN="${SERIAL_BASE}.in"
SERIAL_OUT="${SERIAL_BASE}.out"
LOG="${TMPDIR_PATH}/serial.log"
QEMU_LOG="${TMPDIR_PATH}/qemu.log"
PID=
CAT_PID=
SERIAL_FD_OPEN=0

cleanup() {
    if [ "${SERIAL_FD_OPEN}" -eq 1 ]; then
        exec 3>&-
    fi
    if [ -n "${PID}" ] && kill -0 "${PID}" 2>/dev/null; then
        kill "${PID}" 2>/dev/null || true
        wait "${PID}" 2>/dev/null || true
    fi
    if [ -n "${CAT_PID}" ] && kill -0 "${CAT_PID}" 2>/dev/null; then
        kill "${CAT_PID}" 2>/dev/null || true
        wait "${CAT_PID}" 2>/dev/null || true
    fi
    rm -rf "${TMPDIR_PATH}"
}
trap cleanup EXIT INT TERM

sh "${ROOT}/tests/console-build-audit.sh"
make -C "${ROOT}" TEST_MODE=console

mkfifo "${SERIAL_IN}" "${SERIAL_OUT}"
exec 3<> "${SERIAL_IN}"
SERIAL_FD_OPEN=1
cat "${SERIAL_OUT}" > "${LOG}" &
CAT_PID=$!

"${QEMU}" \
    -M q35 \
    -cpu "${QEMU_CPU}" \
    -m 128M \
    -cdrom "${ROOT}/build/boringos.iso" \
    -boot d \
    -display none \
    -serial "pipe:${SERIAL_BASE}" \
    -monitor none \
    -no-reboot \
    -no-shutdown \
    > /dev/null 2> "${QEMU_LOG}" &
PID=$!

attempt=0
while [ "${attempt}" -lt 150 ]; do
    if grep -Fqx 'console write from BoringOS userspace' "${LOG}" 2>/dev/null; then
        break
    fi
    if ! kill -0 "${PID}" 2>/dev/null; then
        break
    fi
    attempt=$((attempt + 1))
    sleep 0.1
done

if ! grep -Fqx 'console write from BoringOS userspace' "${LOG}" 2>/dev/null; then
    echo 'serial console client never reached the input-read point' >&2
    cat "${LOG}" >&2 2>/dev/null || true
    cat "${QEMU_LOG}" >&2 2>/dev/null || true
    exit 1
fi

printf 'K' >&3

attempt=0
while [ "${attempt}" -lt 150 ]; do
    if grep -Fqx 'BoringKernel userspace serial console test passed.' "${LOG}" 2>/dev/null; then
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
    'BoringKernel 0.0.24-dev' \
    'BoringKernel physical memory test passed.' \
    'BoringKernel virtual memory test passed.' \
    'BoringKernel heap test passed.' \
    'IDT: loaded' \
    'Exceptions: online' \
    'BoringKernel exception infrastructure test passed.' \
    'Userspace serial console test:' \
    '  boot-module-found: PASS' \
    '  elf64-console-image: PASS' \
    '  nx-enabled: PASS' \
    '  process-created: PASS' \
    '  load-console-image: PASS' \
    '  segment-permissions: PASS' \
    '  entry-executable: PASS' \
    '  bss-loader-zeroed: PASS' \
    '  user-stack-mapped: PASS' \
    '  higher-half-supervisor-only: PASS' \
    '  console-syscall-safety: PASS' \
    'Console process PID: 1' \
    'Entering BoringOS serial console client at CPL3.' \
    'console write from BoringOS userspace' \
    'K' \
    'Userspace serial console:' \
    '  c-entry: PASS' \
    '  initialized-data: PASS' \
    '  bss: PASS' \
    '  getpid: PASS' \
    '  console-write: PASS' \
    '  console-read: PASS' \
    '  console-echo: PASS' \
    '  sysret-resume: PASS' \
    '  boring-main-return: PASS' \
    '  final-cpl3-proof: PASS' \
    '  final-tss-rsp0: PASS' \
    'Console input byte: 75' \
    'Console boring_main return: 43' \
    'Console syscall dispatches: 4' \
    '  cleanup: PASS' \
    'BoringKernel userspace serial console test passed.'
do
    if ! grep -Fqx "${line}" "${LOG}"; then
        echo "missing serial console acceptance line: ${line}" >&2
        status=1
    fi
done

for pattern in \
    '^Console final fault RIP: 0x[0-9A-F]{16}$' \
    '^Console PMM free frames restored: [1-9][0-9]*$'
do
    if ! grep -Eq "${pattern}" "${LOG}"; then
        echo "missing serial console runtime value matching: ${pattern}" >&2
        status=1
    fi
done

if grep -Eiq 'Userspace serial console self-test FAILED|BoringKernel syscall fatal|triple fault|reboot' "${LOG}"; then
    echo 'unexpected userspace serial console failure path' >&2
    status=1
fi

cat "${LOG}"
if [ -s "${QEMU_LOG}" ]; then
    cat "${QEMU_LOG}" >&2
fi

if [ "${status}" -ne 0 ]; then
    echo 'BoringKernel userspace serial console verification FAILED.' >&2
    exit "${status}"
fi

echo 'BoringKernel userspace serial console verification passed.'
