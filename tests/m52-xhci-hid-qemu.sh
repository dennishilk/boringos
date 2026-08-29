#!/bin/sh
set -eu

QEMU_BIN=${QEMU:-qemu-system-x86_64}
LOG=build/m52-xhci-hid-serial.log
QMP=build/m52-xhci-hid-qmp.sock

fail() {
    echo "m52-xhci-hid-qemu: FAIL: $*" >&2
    [ ! -f "$LOG" ] || cat "$LOG" >&2
    exit 1
}

make TEST_MODE=m49-xhci-address TEST_CPPFLAGS=-DBORING_M52_HID_ACCEPTANCE=1 all
rm -f "$LOG" "$QMP"

"$QEMU_BIN" \
    -M q35 -m 256M -cdrom build/boringos.iso -boot d \
    -display none -serial "file:$LOG" -monitor none \
    -qmp "unix:$QMP,server=on,wait=off" \
    -no-reboot -no-shutdown \
    -device qemu-xhci,id=xhci \
    -device usb-kbd,bus=xhci.0 \
    -device usb-tablet,bus=xhci.0 &
PID=$!
trap 'kill "$PID" 2>/dev/null || true; wait "$PID" 2>/dev/null || true; rm -f "$QMP"' EXIT INT TERM

attempt=0
while [ "$attempt" -lt 200 ]; do
    if [ -f "$LOG" ] && grep -Fq 'M52 xHCI HID reports FAILED:' "$LOG"; then
        fail 'guest failed before injection'
    fi
    if [ -f "$LOG" ] && grep -Fq 'M52 USB HID transfers ready; inject real USB input now.' "$LOG"; then
        break
    fi
    if ! kill -0 "$PID" 2>/dev/null; then fail 'QEMU exited before injection'; fi
    attempt=$((attempt + 1))
    sleep 0.05
done
[ "$attempt" -lt 200 ] || fail 'guest did not reach M52 injection window'

python3 tests/m52-qmp-input.py "$QMP" || fail 'QMP input injection failed'

attempt=0
while [ "$attempt" -lt 400 ]; do
    if grep -Fq 'M52 xHCI HID reports FAILED:' "$LOG"; then
        fail 'guest rejected real HID transport'
    fi
    if grep -Fq 'M52 xHCI HID interrupt-IN QEMU passed.' "$LOG"; then
        break
    fi
    if ! kill -0 "$PID" 2>/dev/null; then fail 'QEMU exited before M52 PASS'; fi
    attempt=$((attempt + 1))
    sleep 0.05
done
[ "$attempt" -lt 400 ] || fail 'M52 real report acceptance timed out'

for marker in \
    'BoringKernel 0.0.58-dev' \
    'M52 USB HID transfers ready; inject real USB input now.' \
    'M52 xHCI HID interrupt-IN QEMU passed.'
do
    grep -Fq "$marker" "$LOG" || fail "missing marker: $marker"
done

test "$(grep -Fc 'M52 HID report slot=' "$LOG")" -eq 2 ||
    fail 'expected exactly two real configured HID endpoints'
grep -Eq 'M52 HID report slot=[0-9]+ endpoint_id=[2-9][0-9]* protocol=1 submitted=[1-9][0-9]* completed=[1-9][0-9]* bytes=[1-9][0-9]* short=[0-9]+' "$LOG" ||
    fail 'missing dynamic real keyboard endpoint evidence'
grep -Eq 'M52 HID report slot=[0-9]+ endpoint_id=[2-9][0-9]* protocol=0 submitted=[1-9][0-9]* completed=[1-9][0-9]* bytes=[1-9][0-9]* short=[0-9]+' "$LOG" ||
    fail 'missing dynamic real tablet endpoint evidence'
grep -Eq 'M52 keyboard transitions presses=[1-9][0-9]* releases=[1-9][0-9]* last_usage=[1-9][0-9]* last_down=0' "$LOG" ||
    fail 'real keyboard press/release transitions not proven'
grep -Eq 'M52 pointer report x=[0-9]+ y=[0-9]+ buttons=[0-9]+' "$LOG" ||
    fail 'real absolute tablet report not proven'
grep -Eq 'M52 real Interrupt-IN submissions: [3-9][0-9]*' "$LOG" ||
    fail 'real submission counter missing'
grep -Eq 'M52 real Interrupt-IN completions: [3-9][0-9]*' "$LOG" ||
    fail 'real completion counter missing'
grep -Eq 'M52 real HID report bytes: [1-9][0-9]*' "$LOG" ||
    fail 'real report byte counter missing'
grep -Eq 'M52 decoded HID reports: [3-9][0-9]*' "$LOG" ||
    fail 'decoded report counter missing'

cat "$LOG"
echo 'm52-xhci-hid-qemu: PASS'
