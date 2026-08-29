#!/bin/sh
set -eu

QEMU_BIN=${QEMU:-qemu-system-x86_64}
LOG=build/m48-xhci-serial.log

make TEST_MODE=m48-xhci all
rm -f "$LOG"
timeout 35s "$QEMU_BIN" \
    -M q35 -m 256M -cdrom build/boringos.iso -boot d \
    -display none -serial "file:$LOG" -monitor none \
    -no-reboot -no-shutdown -device qemu-xhci,id=xhci \
    -device usb-kbd,bus=xhci.0 -device usb-tablet,bus=xhci.0 || true

if grep -Fq 'M48 xHCI QEMU FAILED:' "$LOG"; then
    cat "$LOG"
    exit 1
fi
for marker in \
    'BoringKernel 0.0.55-dev' \
    'M48 xHCI DMA command/event transport online' \
    'M48 xHCI/USB-HID foundation QEMU passed.'
do
    grep -Fq "$marker" "$LOG" || { cat "$LOG"; exit 1; }
done
echo 'm48-xhci-qemu: PASS'
