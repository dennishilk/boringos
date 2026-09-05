#!/bin/sh
set -eu

QEMU_BIN=${QEMU:-qemu-system-x86_64}
OUT=build/m64-multi-usb
IMAGE=$OUT/m64-usb.raw
BEFORE=$OUT/m64-usb.before.raw
LOG=$OUT/serial.log
QMP=$OUT/qmp.sock

fail() {
    echo "m64-multi-usb-qemu: FAIL: $*" >&2
    [ ! -f "$LOG" ] || tail -n 260 "$LOG" >&2
    exit 1
}

rm -rf "$OUT"
mkdir -p "$OUT"

make TEST_MODE=m49-xhci-address \
    TEST_HARNESS_C='kernel/core/xhci_mixed.c kernel/arch/x86_64/xhci_mixed.c kernel/core/usb_mass_storage.c kernel/core/m64_multi_usb_test.c kernel/core/m64_multi_usb_test_adapter.c' \
    all

python3 - "$IMAGE" <<'PY'
from pathlib import Path
import sys
path = Path(sys.argv[1])
size = 4 * 1024 * 1024
chunk = bytearray(4096)
with path.open("wb") as f:
    for base in range(0, size, len(chunk)):
        for i in range(len(chunk)):
            chunk[i] = ((base + i) * 19 + 7) & 0xff
        f.write(chunk)
PY
cp "$IMAGE" "$BEFORE"
: > "$LOG"
rm -f "$QMP"

"$QEMU_BIN" \
    -M q35,i8042=off -cpu "${QEMU_CPU:-qemu64,apic=off}" -m 256M \
    -cdrom build/boringos.iso -boot d \
    -display none -serial "file:$LOG" -monitor none \
    -qmp "unix:$QMP,server=on,wait=off" \
    -no-reboot -no-shutdown \
    -device qemu-xhci,id=xhciA \
    -device usb-kbd,bus=xhciA.0,port=1 \
    -drive "file=$IMAGE,if=none,format=raw,id=m64usb,cache=writeback" \
    -device usb-storage,bus=xhciA.0,port=2,drive=m64usb,removable=on \
    -device qemu-xhci,id=xhciB \
    -device usb-tablet,bus=xhciB.0,port=1 \
    -device qemu-xhci,id=xhciC &
PID=$!
trap 'kill "$PID" 2>/dev/null || true; wait "$PID" 2>/dev/null || true; rm -f "$QMP"' EXIT INT TERM

attempt=0
while [ "$attempt" -lt 1200 ]; do
    grep -Fq 'M64 multi-USB FAILED:' "$LOG" && fail 'guest failed before HID injection'
    if grep -Fq 'M64 multi-controller HID ready; inject real USB input now.' "$LOG"; then
        break
    fi
    kill -0 "$PID" 2>/dev/null || fail 'QEMU exited before HID injection'
    attempt=$((attempt + 1))
    sleep 0.05
done
[ "$attempt" -lt 1200 ] || fail 'guest did not reach HID injection window'

python3 tests/m53-qmp-input.py "$QMP" || fail 'QMP USB input injection failed'

attempt=0
while [ "$attempt" -lt 1200 ]; do
    grep -Fq 'M64 multi-USB FAILED:' "$LOG" && fail 'guest failed after HID injection'
    if grep -Fq 'M64 multi-controller USB storage/HID passed.' "$LOG"; then
        break
    fi
    kill -0 "$PID" 2>/dev/null || fail 'QEMU exited before PASS'
    attempt=$((attempt + 1))
    sleep 0.05
done
[ "$attempt" -lt 1200 ] || fail 'guest acceptance timed out'

kill "$PID" 2>/dev/null || true
wait "$PID" 2>/dev/null || true
trap - EXIT INT TERM
rm -f "$QMP"

for marker in \
    'KEYBOARD_ON_CONTROLLER_A=PASS' \
    'STORAGE_ON_CONTROLLER_A=PASS' \
    'HID_ON_CONTROLLER_B=PASS' \
    'USB_STORAGE_REGRESSION=PASS' \
    'M63_FLUSH_POLICY_REGRESSION=PASS' \
    'CROSS_CONTROLLER_EVENT_OWNERSHIP=PASS' \
    'CROSS_CONTROLLER_HID_INTERRUPT_IN=PASS' \
    'CANONICAL_MOUSE_EVENT_ON_CONTROLLER_B=PASS' \
    'M64 multi-controller USB storage/HID passed.'
do
    grep -Fq "$marker" "$LOG" || fail "missing marker: $marker"
done

python3 - "$BEFORE" "$IMAGE" <<'PY'
from pathlib import Path
import sys
before = Path(sys.argv[1]).read_bytes()
after = Path(sys.argv[2]).read_bytes()
if len(before) != len(after):
    raise SystemExit("image length changed")
block = 512
start = 8 * block
end = start + block
if before[start:end] == after[start:end]:
    raise SystemExit("target sector did not change")
if before[:start] != after[:start] or before[end:] != after[end:]:
    raise SystemExit("bytes outside target sector changed")
print("M64 host usb-storage persistence: PASS")
PY

cat "$LOG"
echo 'm64-multi-usb-qemu: PASS'
