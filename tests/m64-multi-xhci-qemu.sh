#!/bin/sh
set -eu

QEMU_BIN=${QEMU:-qemu-system-x86_64}
LOG=build/m64-multi-xhci-serial.log

make TEST_MODE=m64-multi-xhci all
rm -f "$LOG"
timeout 40s "$QEMU_BIN" \
    -M q35,i8042=off -m 256M -cdrom build/boringos.iso -boot d \
    -display none -serial "file:$LOG" -monitor none \
    -no-reboot -no-shutdown \
    -device qemu-xhci,id=xhciA \
    -device usb-kbd,bus=xhciA.0 \
    -device qemu-xhci,id=xhciB \
    -device qemu-xhci,id=xhciC || true

if grep -Fq 'M64 multi-xHCI FAILED:' "$LOG"; then
    cat "$LOG"
    exit 1
fi
for marker in \
    'MULTI_XHCI_CONTROLLER_COUNT=3' \
    'MULTI_XHCI_DISTINCT_BDFS=YES' \
    'MULTI_XHCI_INDEPENDENT_MMIO=YES' \
    'MULTI_XHCI_INDEPENDENT_COMMAND_RINGS=YES' \
    'MULTI_XHCI_INDEPENDENT_EVENT_RINGS=YES' \
    'KEYBOARD_ON_CONTROLLER_A=PASS' \
    'EMPTY_SECONDARY_CONTROLLER_ALLOWED=PASS' \
    'CROSS_CONTROLLER_EVENT_OWNERSHIP=PASS' \
    'M64 real multi-xHCI QEMU passed.'
do
    grep -Fq "$marker" "$LOG" || { cat "$LOG"; exit 1; }
done

echo 'm64-multi-xhci-qemu: PASS'
