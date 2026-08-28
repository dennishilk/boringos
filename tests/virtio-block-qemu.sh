#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
QEMU=${QEMU:-qemu-system-x86_64}
QEMU_CPU=${QEMU_CPU:-qemu64,apic=off}
HOST_CC=${HOST_CC:-cc}
LOG=$(mktemp)
HOST_LOG=$(mktemp)
IMAGE="${ROOT}/build/virtio-block-test.raw"
IMAGE_HELPER="${ROOT}/build/virtio-block-image"
PID=

cleanup() {
    if [ -n "${PID}" ] && kill -0 "${PID}" 2>/dev/null; then
        kill "${PID}" 2>/dev/null || true
        wait "${PID}" 2>/dev/null || true
    fi
    rm -f "${LOG}" "${HOST_LOG}" "${IMAGE}" "${IMAGE_HELPER}"
}
trap cleanup EXIT INT TERM

mkdir -p "${ROOT}/build"
"${HOST_CC}" \
    -std=c11 -Wall -Wextra -Wpedantic -Werror \
    -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes \
    "${ROOT}/tests/virtio-block-image.c" -o "${IMAGE_HELPER}"
"${IMAGE_HELPER}" create "${IMAGE}"

size=$(wc -c < "${IMAGE}" | tr -d ' ')
if [ "${size}" != "2097152" ]; then
    echo "unexpected VirtIO raw image size: ${size}" >&2
    exit 1
fi

make -C "${ROOT}" TEST_MODE=virtio-block

"${QEMU}" \
    -M q35 \
    -cpu "${QEMU_CPU}" \
    -m 128M \
    -cdrom "${ROOT}/build/boringos.iso" \
    -boot d \
    -drive "file=${IMAGE},if=none,format=raw,id=boringdisk" \
    -device "virtio-blk-pci,drive=boringdisk,disable-legacy=on" \
    -display none \
    -serial "file:${LOG}" \
    -monitor none \
    -no-reboot \
    -no-shutdown &
PID=$!

attempt=0
while [ "${attempt}" -lt 300 ]; do
    if grep -Fqx 'BoringKernel VirtIO block test passed.' "${LOG}" 2>/dev/null; then
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
    'BoringKernel exception infrastructure test passed.' \
    'Modern VirtIO block:' \
    'PCI:' \
    '  discovery: PASS' \
    '  transport: modern PCI' \
    '  vendor: 0x1AF4' \
    '  device: 0x1042' \
    '  memory-space: PASS' \
    '  bus-master: PASS' \
    'Capabilities:' \
    '  common-config: PASS' \
    '  notify-config: PASS' \
    '  device-config: PASS' \
    'Negotiation:' \
    '  reset: PASS' \
    '  VERSION_1: PASS' \
    '  FEATURES_OK: PASS' \
    '  queue-size: 8' \
    '  DMA frames: 5' \
    '  queue-enable: PASS' \
    '  DRIVER_OK: PASS' \
    'Block device:' \
    '  name: vblk0' \
    '  logical block size: 512' \
    '  sectors: 4096' \
    '  capacity: 2097152 bytes' \
    '  registration: PASS' \
    'I/O:' \
    '  known-sector-read: PASS' \
    '  first-sector-read: PASS' \
    '  last-sector-read: PASS' \
    '  single-sector-write: PASS' \
    '  single-sector-read-back: PASS' \
    '  multi-sector-write-read-back: PASS' \
    '  multi-request-chunking: PASS' \
    '  neighbor-preservation: PASS' \
    '  rejected-range-no-submit: PASS' \
    'VirtIO:' \
    '  used-ring completion: PASS' \
    '  request status: PASS' \
    'BoringKernel VirtIO block test passed.'
do
    if ! grep -Fqx "${line}" "${LOG}"; then
        echo "missing VirtIO block acceptance line: ${line}" >&2
        status=1
    fi
done

if ! grep -Eq '^  BDF: [0-9A-F]{2}:[0-9A-F]{2}\.[0-7]$' "${LOG}"; then
    echo 'missing dynamic VirtIO PCI BDF' >&2
    status=1
fi
if ! grep -Eq '^  device features: 0x[0-9A-F]{16}$' "${LOG}"; then
    echo 'missing offered VirtIO feature bits' >&2
    status=1
fi
if ! grep -Eq '^  accepted features: 0x[0-9A-F]{16}$' "${LOG}"; then
    echo 'missing accepted VirtIO feature bits' >&2
    status=1
fi
if ! grep -Eq '^  submissions: 1[0-9]$|^  submissions: [2-9][0-9]+$' "${LOG}"; then
    echo 'VirtIO acceptance did not wrap queue indices with enough requests' >&2
    status=1
fi
if grep -Eiq 'VirtIO block test FAILED|VirtIO block init result:|Fatal exception: controlled halt|triple fault|reboot' "${LOG}" 2>/dev/null; then
    echo 'unexpected VirtIO block failure path' >&2
    status=1
fi

cat "${LOG}"

if [ "${status}" -ne 0 ]; then
    echo 'BoringKernel VirtIO block verification FAILED.' >&2
    exit "${status}"
fi

if [ -n "${PID}" ] && kill -0 "${PID}" 2>/dev/null; then
    kill "${PID}" 2>/dev/null || true
    wait "${PID}" 2>/dev/null || true
fi
PID=

if ! "${IMAGE_HELPER}" verify "${IMAGE}" > "${HOST_LOG}"; then
    cat "${HOST_LOG}"
    echo 'host raw-disk persistence verification FAILED.' >&2
    exit 1
fi

cat "${HOST_LOG}"
for line in \
    'Host raw-disk verification:' \
    '  persisted-write: PASS' \
    '  left-neighbor: PASS' \
    '  right-neighbor: PASS'
do
    if ! grep -Fqx "${line}" "${HOST_LOG}"; then
        echo "missing host persistence line: ${line}" >&2
        exit 1
    fi
done

echo 'BoringKernel VirtIO block QEMU verification passed.'
