#!/bin/sh
set -eu

QEMU_BIN=${QEMU:-qemu-system-x86_64}
LOG=build/m55-ahci-serial.log
DISK=build/m55-ahci.raw
HARNESS='kernel/core/ahci.c kernel/arch/x86_64/ahci_hw.c kernel/core/ahci_test.c kernel/core/ahci_test_adapter.c'

make TEST_MODE=runtime TEST_HARNESS_C="$HARNESS" all
mkdir -p build
rm -f "$LOG" "$DISK"
truncate -s 8M "$DISK"

timeout 35s "$QEMU_BIN" \
    -M q35 -m 256M -cdrom build/boringos.iso -boot d \
    -display none -serial "file:$LOG" -monitor none \
    -no-reboot -no-shutdown \
    -drive "file=$DISK,format=raw,if=ide,index=0,media=disk" || true

if grep -Fq 'M55 AHCI QEMU FAILED:' "$LOG"; then
    cat "$LOG"
    exit 1
fi
for marker in \
    'BoringKernel 0.0.57-dev' \
    'class=01:06 prog_if=01' \
    'M55 AHCI BDF:' \
    'M55 AHCI ABAR:' \
    'M55 AHCI CAP:' \
    'M55 AHCI VS:' \
    'M55 AHCI PI:' \
    'M55 AHCI SATA present: port=' \
    'M55 AHCI cleanup: PASS' \
    'M55 AHCI/SATA controller foundation QEMU passed.'
do
    grep -Fq "$marker" "$LOG" || { cat "$LOG"; exit 1; }
done

grep -Eq 'M55 AHCI port [0-9]+ SSTS=.+ SIG=.+' "$LOG" || {
    cat "$LOG"
    exit 1
}

cat "$LOG"
echo 'm55-ahci-qemu: PASS'
