#!/bin/sh
set -eu

QEMU_BIN=${QEMU:-qemu-system-x86_64}
NORMAL_LOG=build/m59-physical-smoke-normal.log
HIGH_LOG=build/m59-physical-smoke-32g.log
NORMAL_QMP=build/m59-physical-smoke-normal.qmp
HIGH_QMP=build/m59-physical-smoke-32g.qmp
RAM_FILE=build/m59-physical-smoke-32g.ram

fail() {
    echo "m59-physical-smoke-qemu: FAIL: $*" >&2
    [ ! -f "$NORMAL_LOG" ] || cat "$NORMAL_LOG" >&2
    [ ! -f "$HIGH_LOG" ] || cat "$HIGH_LOG" >&2
    exit 1
}

wait_and_inject() {
    log=$1
    qmp=$2
    pid=$3
    attempt=0
    while [ "$attempt" -lt 300 ]; do
        if [ -f "$log" ] && grep -Fq 'M59 PHYSICAL SMOKE FAILED:' "$log"; then
            fail "guest failed before input injection: $log"
        fi
        if [ -f "$log" ] && grep -Fq 'M59 USB input ready; inject or provide real USB input now.' "$log"; then
            break
        fi
        if ! kill -0 "$pid" 2>/dev/null; then fail "QEMU exited before input window: $log"; fi
        attempt=$((attempt + 1))
        sleep 0.05
    done
    [ "$attempt" -lt 300 ] || fail "guest did not reach input window: $log"
    python3 tests/m53-qmp-input.py "$qmp" || fail "QMP input injection failed: $log"

    attempt=0
    while [ "$attempt" -lt 600 ]; do
        if grep -Fq 'M59 PHYSICAL SMOKE FAILED:' "$log"; then
            fail "guest failed after input injection: $log"
        fi
        if grep -Fq 'M59 PHYSICAL SMOKE HARNESS PASSED' "$log"; then
            break
        fi
        if ! kill -0 "$pid" 2>/dev/null; then fail "QEMU exited before PASS: $log"; fi
        attempt=$((attempt + 1))
        sleep 0.05
    done
    [ "$attempt" -lt 600 ] || fail "bounded smoke acceptance timed out: $log"
}

check_common() {
    log=$1
    for marker in \
        'BoringKernel 0.0.60-dev' \
        'Arch: x86_64' \
        'PCI enumeration: COMPLETE' \
        'Framebuffer: READY' \
        'xHCI: READY' \
        'USB addressing truncated: NO' \
        'USB descriptors: READY' \
        'USB keyboard: DETECTED' \
        'USB pointer: DETECTED' \
        'USB HID configured: YES' \
        'Canonical input: EVENTS OBSERVED' \
        'Storage writes: DISABLED' \
        'PHYSICAL SMOKE READY' \
        'M59 PHYSICAL SMOKE HARNESS PASSED'
    do
        grep -Fqx "$marker" "$log" || fail "missing marker in $log: $marker"
    done
    grep -Eq '^Canonical input queued events: [1-9][0-9]*$' "$log" ||
        fail "no canonical input events in $log"
    grep -Fqx 'Canonical input dropped events: 0' "$log" ||
        fail "canonical input dropped events in $log"
}

rm -rf build/kernel build/iso_root
rm -f build/kernel.elf build/boringos.iso build/.test-mode \
      "$NORMAL_LOG" "$HIGH_LOG" "$NORMAL_QMP" "$HIGH_QMP" "$RAM_FILE"
make TEST_MODE=runtime \
    TEST_HARNESS_C='kernel/core/m59_physical_smoke_test.c kernel/core/m59_physical_smoke_test_adapter.c' \
    all

"$QEMU_BIN" \
    -M q35,i8042=off -m 256M -cdrom build/boringos.iso -boot d \
    -vga std -display none -serial "file:$NORMAL_LOG" -monitor none \
    -qmp "unix:$NORMAL_QMP,server=on,wait=off" \
    -no-reboot -no-shutdown \
    -device qemu-xhci,id=xhci \
    -device usb-kbd,bus=xhci.0 \
    -device usb-tablet,bus=xhci.0 &
NORMAL_PID=$!
trap 'kill "$NORMAL_PID" 2>/dev/null || true; [ -z "${HIGH_PID:-}" ] || kill "$HIGH_PID" 2>/dev/null || true; wait "$NORMAL_PID" 2>/dev/null || true; [ -z "${HIGH_PID:-}" ] || wait "$HIGH_PID" 2>/dev/null || true; rm -f "$NORMAL_QMP" "$HIGH_QMP" "$RAM_FILE"' EXIT INT TERM
wait_and_inject "$NORMAL_LOG" "$NORMAL_QMP" "$NORMAL_PID"
kill "$NORMAL_PID" 2>/dev/null || true
wait "$NORMAL_PID" 2>/dev/null || true
check_common "$NORMAL_LOG"

truncate -s 32G "$RAM_FILE"
"$QEMU_BIN" \
    -M q35,i8042=off,memory-backend=m59ram -m 32G \
    -object memory-backend-file,id=m59ram,size=32G,mem-path="$RAM_FILE",share=on,prealloc=off \
    -cdrom build/boringos.iso -boot d \
    -vga std -display none -serial "file:$HIGH_LOG" -monitor none \
    -qmp "unix:$HIGH_QMP,server=on,wait=off" \
    -no-reboot -no-shutdown \
    -device qemu-xhci,id=xhci \
    -device usb-kbd,bus=xhci.0 \
    -device usb-tablet,bus=xhci.0 &
HIGH_PID=$!
wait_and_inject "$HIGH_LOG" "$HIGH_QMP" "$HIGH_PID"
kill "$HIGH_PID" 2>/dev/null || true
wait "$HIGH_PID" 2>/dev/null || true
check_common "$HIGH_LOG"
grep -Fqx 'Memory above 4GiB: YES' "$HIGH_LOG" || fail '32-GiB guest did not expose >4-GiB memory'
awk '/Memory usable bytes:/ { if (($4 + 0) <= 4294967296) exit 1; found=1 } END { if (!found) exit 1 }' "$HIGH_LOG" ||
    fail '32-GiB usable-memory evidence missing'

printf '%s\n' '--- M59 normal-RAM evidence ---'
grep -E '^(Kernel:|Arch:|Memory|PCI|Framebuffer:|xHCI:|USB |Canonical input|Storage writes:|PHYSICAL|M59 PHYSICAL)' "$NORMAL_LOG"
printf '%s\n' '--- M59 32-GiB evidence ---'
grep -E '^(Kernel:|Arch:|Memory|PCI|Framebuffer:|xHCI:|USB |Canonical input|Storage writes:|PHYSICAL|M59 PHYSICAL)' "$HIGH_LOG"
echo 'M59 PHYSICAL SMOKE HARNESS PASSED'
