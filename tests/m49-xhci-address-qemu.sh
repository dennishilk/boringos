#!/bin/sh
set -eu

QEMU_BIN=${QEMU:-qemu-system-x86_64}
LOG=build/m49-xhci-address-serial.log

make TEST_MODE=m49-xhci-address all
rm -f "$LOG"
timeout 35s "$QEMU_BIN" \
    -M q35 -m 256M -cdrom build/boringos.iso -boot d \
    -display none -serial "file:$LOG" -monitor none \
    -no-reboot -no-shutdown -device qemu-xhci,id=xhci \
    -device usb-kbd,bus=xhci.0 -device usb-tablet,bus=xhci.0 || true

if grep -Fq 'M49 xHCI addressing FAILED:' "$LOG"; then
    cat "$LOG"
    exit 1
fi
test "$(grep -Fc 'M49 addressed device port=' "$LOG")" -eq 2 || {
    cat "$LOG"
    exit 1
}
for marker in \
    'BoringKernel 0.0.61-dev' \
    'M49 real Enable Slot completions: 2' \
    'M49 real Address Device completions: 2' \
    'M49 command completions consumed: 4' \
    'M49 xHCI USB device addressing QEMU passed.'
do
    grep -Fq "$marker" "$LOG" || { cat "$LOG"; exit 1; }
done
echo 'm49-xhci-address-qemu: PASS'
