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

make -C "${ROOT}"

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
    if grep -Fqx 'BoringOS booting...' "${LOG}" 2>/dev/null \
        && grep -Fqx 'BoringKernel 0.0.1-dev' "${LOG}" 2>/dev/null \
        && grep -Fqx 'Arch: x86_64' "${LOG}" 2>/dev/null \
        && grep -Fqx 'Hello from BoringKernel.' "${LOG}" 2>/dev/null; then
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
    'BoringKernel 0.0.1-dev' \
    'Arch: x86_64' \
    'Hello from BoringKernel.'
do
    if ! grep -Fqx "${line}" "${LOG}"; then
        echo "missing serial line: ${line}" >&2
        status=1
    fi
done

cat "${LOG}"

if [ "${status}" -ne 0 ]; then
    echo 'BoringKernel QEMU boot verification FAILED.' >&2
    exit "${status}"
fi

echo 'BoringKernel QEMU boot verification passed.'
