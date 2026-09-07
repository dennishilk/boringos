#!/bin/sh
set -eu
QEMU_BIN=${QEMU:-qemu-system-x86_64}
LOG=build/m51-xhci-configuration-serial.log
make TEST_MODE=m49-xhci-address TEST_CPPFLAGS=-DBORING_M51_CONFIGURATION_ACCEPTANCE=1 all
rm -f "$LOG"
timeout 40s "$QEMU_BIN" \
    -M q35 -m 256M -cdrom build/boringos.iso -boot d \
    -display none -serial "file:$LOG" -monitor none \
    -no-reboot -no-shutdown -device qemu-xhci,id=xhci \
    -device usb-kbd,bus=xhci.0 -device usb-tablet,bus=xhci.0 || true
if grep -Fq 'M51 xHCI configuration FAILED:' "$LOG"; then cat "$LOG"; exit 1; fi
test "$(grep -Fc 'M51 configured device port=' "$LOG")" -eq 2 || { cat "$LOG"; exit 1; }
test "$(grep -Fc 'M51 HID endpoint slot=' "$LOG")" -ge 2 || { cat "$LOG"; exit 1; }
grep -Eq 'M51 configured device port=[0-9]+ slot=[0-9]+ configuration=[0-9]+ hid_endpoints=[0-9]+' "$LOG" || { cat "$LOG"; exit 1; }
grep -Eq 'M51 HID endpoint slot=[0-9]+ address=[0-9]+ endpoint_id=[0-9]+ max_packet=[0-9]+ interval=[0-9]+ xhci_interval=[0-9]+' "$LOG" || { cat "$LOG"; exit 1; }
for marker in \
    'M51 real SET_CONFIGURATION completions: 2' \
    'M51 real Configure Endpoint completions: 2' \
    'M51 real Transfer Events consumed: ' \
    'M51 xHCI HID endpoint setup QEMU passed.'
do
    grep -Fq "$marker" "$LOG" || { cat "$LOG"; exit 1; }
done
cat "$LOG"
echo 'm51-xhci-configuration-qemu: PASS'
