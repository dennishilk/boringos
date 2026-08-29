#!/bin/sh
set -eu

QEMU_BIN=${QEMU:-qemu-system-x86_64}
OUT=build/m60-usb-mass-storage
IMAGE=$OUT/m60-usb.raw
BEFORE=$OUT/m60-usb.before.raw
LOG=$OUT/serial.log
QEMU_LOG=$OUT/qemu.log

fail() {
    echo "m60-usb-mass-storage-qemu: FAIL: $*" >&2
    [ ! -f "$LOG" ] || tail -n 250 "$LOG" >&2
    [ ! -f "$QEMU_LOG" ] || tail -n 120 "$QEMU_LOG" >&2
    exit 1
}

rm -rf "$OUT"
mkdir -p "$OUT"

make TEST_MODE=m49-xhci-address \
    TEST_HARNESS_C='kernel/core/xhci_mixed.c kernel/arch/x86_64/xhci_mixed.c kernel/core/usb_mass_storage.c kernel/core/usb_mass_storage_test.c kernel/core/usb_mass_storage_test_adapter.c' \
    all

python3 - "$IMAGE" <<'PY'
from pathlib import Path
import sys
path = Path(sys.argv[1])
size = 4 * 1024 * 1024
chunk = bytearray(4096)
with path.open('wb') as f:
    for base in range(0, size, len(chunk)):
        for i in range(len(chunk)):
            chunk[i] = ((base + i) * 17 + 3) & 0xff
        f.write(chunk)
PY
cp "$IMAGE" "$BEFORE"
sha256sum "$BEFORE" | tee "$OUT/sha256-before.txt"
: > "$LOG"
: > "$QEMU_LOG"

"$QEMU_BIN" \
    -M q35,i8042=off -cpu "${QEMU_CPU:-qemu64,apic=off}" -m 256M \
    -cdrom build/boringos.iso -boot d \
    -display none -serial "file:$LOG" -monitor none \
    -no-reboot -no-shutdown \
    -device qemu-xhci,id=xhci \
    -device usb-kbd,bus=xhci.0,port=1 \
    -device usb-tablet,bus=xhci.0,port=2 \
    -drive "file=$IMAGE,if=none,format=raw,id=m60usb,cache=writeback" \
    -device usb-storage,bus=xhci.0,port=3,drive=m60usb,removable=on \
    -trace enable=usb_xhci_xfer_* \
    -trace enable=usb_xhci_queue_event \
    >>"$QEMU_LOG" 2>&1 &
PID=$!
trap 'kill "$PID" 2>/dev/null || true; wait "$PID" 2>/dev/null || true' EXIT INT TERM

attempt=0
while [ "$attempt" -lt 1200 ]; do
    if grep -Fq 'M60 USB MASS STORAGE TEST FAILED:' "$LOG"; then
        fail 'guest reported failure'
    fi
    if grep -Fq 'M60 USB MASS STORAGE TEST PASSED' "$LOG"; then
        break
    fi
    if ! kill -0 "$PID" 2>/dev/null; then
        fail 'QEMU exited before M60 PASS'
    fi
    attempt=$((attempt + 1))
    sleep 0.05
done
[ "$attempt" -lt 1200 ] || fail 'guest acceptance timed out'

kill "$PID" 2>/dev/null || true
wait "$PID" 2>/dev/null || true
trap - EXIT INT TERM

for marker in \
    'BoringKernel 0.0.60-dev' \
    'M60 keyboard/tablet descriptor coexistence: PASS' \
    'M60 USB mass-storage interface detected: PASS' \
    'M60 descriptor-derived Bulk OUT: PASS' \
    'M60 descriptor-derived Bulk IN: PASS' \
    'M60 Configure Endpoint: PASS' \
    'M60 CBW transport: PASS' \
    'M60 valid CSW: PASS' \
    'M60 SCSI INQUIRY: PASS' \
    'M60 TEST UNIT READY: PASS' \
    'M60 READ CAPACITY(10): PASS' \
    'M60 WRITE(10): PASS lba=8' \
    'M60 READ(10): PASS' \
    'M60 SYNCHRONIZE CACHE(10): PASS' \
    'M60 neighboring sector before: unchanged' \
    'M60 neighboring sector after: unchanged' \
    'M60 out-of-range read: REJECTED' \
    'M60 out-of-range write: REJECTED' \
    'M60 bad CSW signature: REJECTED' \
    'M60 bad CSW tag: REJECTED' \
    'M60 bad CSW residue/status: REJECTED' \
    'M60 HID coexistence: PASS (usb-kbd + usb-tablet + usb0)' \
    'M60 cleanup: PASS' \
    'M60 USB MASS STORAGE TEST PASSED'
do
    grep -Fq "$marker" "$LOG" || fail "missing marker: $marker"
done

grep -Eq '^M60 logical-sector size: (512|1024|2048|4096)$' "$LOG" ||
    fail 'missing sensible real logical-sector size'
grep -Eq '^M60 capacity bytes: [1-9][0-9]+$' "$LOG" ||
    fail 'missing real capacity'
grep -Eq '^M60 BOT commands: [1-9][0-9]* bulk_in=[1-9][0-9]* bulk_out=[1-9][0-9]*$' "$LOG" ||
    fail 'missing BOT transfer counters'

sha256sum "$IMAGE" | tee "$OUT/sha256-after.txt"
if cmp -s "$BEFORE" "$IMAGE"; then
    fail 'backing image did not change'
fi

python3 - "$BEFORE" "$IMAGE" "$LOG" <<'PY'
from pathlib import Path
import re
import sys
before_path, after_path, log_path = map(Path, sys.argv[1:])
log = log_path.read_text(errors='replace')
m = re.search(r'^M60 logical-sector size: (\d+)$', log, re.M)
if not m:
    raise SystemExit('missing block size')
block = int(m.group(1))
lba = 8
before = before_path.read_bytes()
after = after_path.read_bytes()
if len(before) != len(after):
    raise SystemExit('image length changed')
start = lba * block
end = start + block
expected = bytearray((0x60 + (i % 31)) & 0xff for i in range(block))
if before[start:end] == expected:
    expected[0] ^= 0x5a
if after[start:end] != expected:
    raise SystemExit('target sector does not contain exact guest pattern')
if before[:start] != after[:start] or before[end:] != after[end:]:
    raise SystemExit('bytes outside target sector changed')
if before[(lba-1)*block:lba*block] != after[(lba-1)*block:lba*block]:
    raise SystemExit('preceding sector changed')
if before[(lba+1)*block:(lba+2)*block] != after[(lba+1)*block:(lba+2)*block]:
    raise SystemExit('following sector changed')
print(f'M60 host persistence: PASS lba={lba} block={block} exact_target_only=1')
print('M60 host neighboring sectors: PASS unchanged')
PY

cat "$LOG"
echo 'm60-usb-mass-storage-qemu: PASS'
