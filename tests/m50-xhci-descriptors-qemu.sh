#!/bin/sh
set -eu

QEMU_BIN=${QEMU:-qemu-system-x86_64}
LOG=build/m50-xhci-descriptors-serial.log

make TEST_MODE=m49-xhci-address \
    TEST_CPPFLAGS=-DBORING_M50_DESCRIPTOR_ACCEPTANCE=1 all
rm -f "$LOG"
timeout 40s "$QEMU_BIN" \
    -M q35 -m 256M -cdrom build/boringos.iso -boot d \
    -display none -serial "file:$LOG" -monitor none \
    -no-reboot -no-shutdown -device qemu-xhci,id=xhci \
    -device usb-kbd,bus=xhci.0 -device usb-tablet,bus=xhci.0 || true

if grep -Fq 'M50 xHCI descriptors FAILED:' "$LOG"; then
    cat "$LOG"
    exit 1
fi
test "$(grep -Fc 'M50 descriptor device port=' "$LOG")" -eq 2 || {
    cat "$LOG"
    exit 1
}
grep -Eq 'M50 descriptor device port=[0-9]+ slot=[0-9]+ speed=[0-9]+ vid=[0-9]+ pid=[0-9]+ configuration_length=[0-9]+ interfaces=[0-9]+' "$LOG" || {
    cat "$LOG"
    exit 1
}
for marker in \
    'M50 real transfer events: ' \
    'M50 real descriptor bytes: ' \
    'M50 Evaluate Context completions: ' \
    'M50 xHCI EP0 descriptor discovery QEMU passed.'
do
    grep -Fq "$marker" "$LOG" || { cat "$LOG"; exit 1; }
done
cat "$LOG"
echo 'm50-xhci-descriptors-qemu: PASS'
