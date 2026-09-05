#!/bin/sh
set -eu

QEMU_BIN=${QEMU:-qemu-system-x86_64}
OUT=build/m65-usb-hub-mouse
LOG=$OUT/serial.log
QMP=$OUT/qmp.sock

fail() {
    echo "m65-usb-hub-mouse-qemu: FAIL: $*" >&2
    [ ! -f "$LOG" ] || tail -n 260 "$LOG" >&2
    exit 1
}

rm -rf "$OUT"
mkdir -p "$OUT"

make TEST_MODE=m49-xhci-address \
    TEST_HARNESS_C='kernel/core/xhci_mixed.c kernel/arch/x86_64/xhci_mixed.c kernel/core/m65_hub_test.c kernel/core/m65_hub_test_adapter.c' \
    all

: > "$LOG"
rm -f "$QMP"
"$QEMU_BIN" \
    -M q35,i8042=off -cpu "${QEMU_CPU:-qemu64,apic=off}" -m 256M \
    -cdrom build/boringos.iso -boot d \
    -display none -serial "file:$LOG" -monitor none \
    -qmp "unix:$QMP,server=on,wait=off" \
    -no-reboot -no-shutdown \
    -device qemu-xhci,id=xhci,p3=0 \
    -device usb-hub,id=hub,bus=xhci.0,port=1 \
    -device usb-mouse,bus=xhci.0,port=1.1 &
PID=$!
trap 'kill "$PID" 2>/dev/null || true; wait "$PID" 2>/dev/null || true; rm -f "$QMP"' EXIT INT TERM

attempt=0
while [ "$attempt" -lt 1200 ]; do
    grep -Fq 'M65 USB hub FAILED:' "$LOG" && fail 'guest failed before mouse injection'
    if grep -Fq 'M65 downstream mouse ready; inject real USB mouse input now.' "$LOG"; then
        break
    fi
    kill -0 "$PID" 2>/dev/null || fail 'QEMU exited before mouse injection'
    attempt=$((attempt + 1))
    sleep 0.05
done
[ "$attempt" -lt 1200 ] || fail 'guest did not reach downstream mouse injection window'

python3 tests/m65-qmp-mouse.py "$QMP" || fail 'QMP USB mouse injection failed'

attempt=0
while [ "$attempt" -lt 1200 ]; do
    grep -Fq 'M65 USB hub FAILED:' "$LOG" && fail 'guest failed after mouse injection'
    if grep -Fq 'M65 hub downstream mouse input passed.' "$LOG"; then
        break
    fi
    kill -0 "$PID" 2>/dev/null || fail 'QEMU exited before downstream mouse PASS'
    attempt=$((attempt + 1))
    sleep 0.05
done
[ "$attempt" -lt 1200 ] || fail 'downstream mouse acceptance timed out'

kill "$PID" 2>/dev/null || true
wait "$PID" 2>/dev/null || true
trap - EXIT INT TERM
rm -f "$QMP"

for marker in \
    'USB_HUB_DESCRIPTOR=PASS' \
    'USB_HUB_PORT_POWER=PASS' \
    'USB_HUB_PORT_STATUS=PASS' \
    'USB_HUB_PORT_RESET=PASS' \
    'USB_HUB_ROUTE_STRING=PASS' \
    'USB_HUB_DOWNSTREAM_ADDRESS=PASS' \
    'USB_HUB_DOWNSTREAM_MOUSE=PASS' \
    'USB_HUB_MOUSE_INTERRUPT_IN=PASS' \
    'USB_HUB_MOUSE_CANONICAL_QUEUE=PASS' \
    'M65 hub downstream mouse input passed.'
do
    grep -Fq "$marker" "$LOG" || fail "missing marker: $marker"
done

cat "$LOG"
echo 'm65-usb-hub-mouse-qemu: PASS'
