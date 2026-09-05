#!/bin/sh
set -eu

QEMU_BIN=${QEMU:-qemu-system-x86_64}
OUT=build/m65-usb-hub-mouse
LOG=$OUT/serial.log

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
timeout 45s "$QEMU_BIN" \
    -M q35,i8042=off -cpu "${QEMU_CPU:-qemu64,apic=off}" -m 256M \
    -cdrom build/boringos.iso -boot d \
    -display none -serial "file:$LOG" -monitor none \
    -no-reboot -no-shutdown \
    -device qemu-xhci,id=xhci,p3=0 \
    -device usb-hub,id=hub,bus=xhci.0,port=1 \
    -device usb-mouse,bus=xhci.0,port=1.1 || true

if grep -Fq 'M65 USB hub FAILED:' "$LOG"; then
    fail 'guest reported hub failure'
fi
for marker in \
    'USB_HUB_DESCRIPTOR=PASS' \
    'USB_HUB_PORT_POWER=PASS' \
    'USB_HUB_PORT_STATUS=PASS' \
    'USB_HUB_PORT_RESET=PASS' \
    'USB_HUB_ROUTE_STRING=PASS' \
    'USB_HUB_DOWNSTREAM_ADDRESS=PASS' \
    'USB_HUB_DOWNSTREAM_MOUSE=PASS' \
    'M65 hub downstream mouse enumeration passed.'
do
    grep -Fq "$marker" "$LOG" || fail "missing marker: $marker"
done

cat "$LOG"
echo 'm65-usb-hub-mouse-qemu: PASS'
