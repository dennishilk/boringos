#!/bin/sh
set -eu

QEMU_BIN=${QEMU:-qemu-system-x86_64}
LOG=build/m53-usb-input-serial.log
QMP=build/m53-usb-input-qmp.sock

fail() {
    echo "m53-usb-input-qemu: FAIL: $*" >&2
    [ ! -f "$LOG" ] || cat "$LOG" >&2
    exit 1
}

make TEST_MODE=m49-xhci-address TEST_CPPFLAGS=-DBORING_M53_INPUT_ACCEPTANCE=1 all
rm -f "$LOG" "$QMP"

"$QEMU_BIN" \
    -M q35,i8042=off -m 256M -cdrom build/boringos.iso -boot d \
    -display none -serial "file:$LOG" -monitor none \
    -qmp "unix:$QMP,server=on,wait=off" \
    -no-reboot -no-shutdown \
    -device qemu-xhci,id=xhci \
    -device usb-kbd,bus=xhci.0 \
    -device usb-tablet,bus=xhci.0 &
PID=$!
trap 'kill "$PID" 2>/dev/null || true; wait "$PID" 2>/dev/null || true; rm -f "$QMP"' EXIT INT TERM

attempt=0
while [ "$attempt" -lt 240 ]; do
    if [ -f "$LOG" ] && grep -Fq 'M53 USB input queue FAILED:' "$LOG"; then
        fail 'guest failed before injection'
    fi
    if [ -f "$LOG" ] && grep -Fq 'M53 USB input queue ready; inject real USB input now.' "$LOG"; then
        break
    fi
    if ! kill -0 "$PID" 2>/dev/null; then fail 'QEMU exited before injection'; fi
    attempt=$((attempt + 1))
    sleep 0.05
done
[ "$attempt" -lt 240 ] || fail 'guest did not reach M53 injection window'

python3 tests/m53-qmp-input.py "$QMP" || fail 'QMP USB input injection failed'

attempt=0
while [ "$attempt" -lt 500 ]; do
    if grep -Fq 'M53 USB input queue FAILED:' "$LOG"; then
        fail 'guest rejected USB input integration'
    fi
    if grep -Fq 'M53 real USB input queue QEMU passed.' "$LOG"; then
        break
    fi
    if ! kill -0 "$PID" 2>/dev/null; then fail 'QEMU exited before M53 PASS'; fi
    attempt=$((attempt + 1))
    sleep 0.05
done
[ "$attempt" -lt 500 ] || fail 'M53 USB input acceptance timed out'

for marker in \
    'BoringKernel 0.0.53-dev' \
    'M53 USB input queue ready; inject real USB input now.' \
    'M53 canonical input events: 7' \
    'M53 queue event 0 type=1 code=81 value1=1 value2=0 modifiers=8' \
    'M53 queue event 1 type=1 code=1 value1=1 value2=0 modifiers=8' \
    'M53 queue event 2 type=2 code=0 value1=2345 value2=3456 modifiers=8' \
    'M53 queue event 3 type=3 code=1 value1=1 value2=0 modifiers=8' \
    'M53 queue event 4 type=1 code=1 value1=0 value2=0 modifiers=8' \
    'M53 queue event 5 type=3 code=1 value1=0 value2=0 modifiers=8' \
    'M53 queue event 6 type=1 code=81 value1=0 value2=0 modifiers=0' \
    'M53 real USB input queue QEMU passed.'
do
    grep -Fqx "$marker" "$LOG" || fail "missing marker: $marker"
done

grep -Eq '^M53 real USB Transfer completions: ([89]|1[0-6])$' "$LOG" ||
    fail 'real USB completion count was outside bounded acceptance'
grep -Eq '^M53 decoded HID reports: ([89]|1[0-6])$' "$LOG" ||
    fail 'decoded HID report count was outside bounded acceptance'
grep -Fqx 'M53 queue precheck queued=7 dropped=0 modifiers=0 owner=53 owned=1 initialized=1' "$LOG" ||
    fail 'canonical queue did not reach the required final state'

cat "$LOG"
echo 'm53-usb-input-qemu: PASS (q35 i8042=off, real qemu-xhci USB input)'
