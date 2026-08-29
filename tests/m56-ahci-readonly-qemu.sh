#!/bin/sh
set -eu

QEMU_BIN=${QEMU:-qemu-system-x86_64}
LOG=build/m56-ahci-readonly-serial.log
DISK=build/m56-ahci-readonly.raw
HARNESS='kernel/core/ahci.c kernel/arch/x86_64/ahci_hw.c kernel/core/ahci_block_test.c kernel/core/ahci_block_test_adapter.c'

make TEST_MODE=runtime TEST_HARNESS_C="$HARNESS" all
mkdir -p build
rm -f "$LOG" "$DISK"

python3 - "$DISK" <<'PY'
import sys
path = sys.argv[1]
sectors = 16384
sector_size = 512
with open(path, 'wb') as f:
    for lba in range(sectors):
        f.write(bytes(((lba + offset) & 0xff) for offset in range(sector_size)))
PY

BEFORE=$(sha256sum "$DISK" | awk '{print $1}')

timeout 45s "$QEMU_BIN" \
    -M q35 -m 256M -cdrom build/boringos.iso -boot d \
    -display none -serial "file:$LOG" -monitor none \
    -no-reboot -no-shutdown \
    -drive "file=$DISK,format=raw,if=ide,index=0,media=disk" || true

AFTER=$(sha256sum "$DISK" | awk '{print $1}')
if [ "$BEFORE" != "$AFTER" ]; then
    cat "$LOG"
    echo 'M56 RAW SATA image changed during read-only acceptance' >&2
    exit 1
fi

if grep -Fq 'M56 AHCI READ QEMU FAILED:' "$LOG"; then
    cat "$LOG"
    exit 1
fi
for marker in \
    'BoringKernel 0.0.57-dev' \
    'M56 AHCI IDENTIFY: PASS' \
    'M56 AHCI capacity blocks: 16384' \
    'M56 AHCI logical sector size: 512' \
    'M56 AHCI LBA48: yes' \
    'M56 AHCI max blocks per transfer: 8' \
    'M56 M21 block device: sata0 read-only' \
    'M56 first read: PASS' \
    'M56 middle read: PASS' \
    'M56 last read: PASS' \
    'M56 multi-sector read: PASS' \
    'M56 out-of-range rejection: PASS' \
    'M56 PRDT transfer bound: PASS' \
    'M56 write rejection: PASS' \
    'M56 real read completions: 4' \
    'M56 AHCI synchronous read-only block path QEMU passed.'
do
    grep -Fq "$marker" "$LOG" || { cat "$LOG"; exit 1; }
done

cat "$LOG"
echo "M56 RAW SATA SHA256 unchanged: $AFTER"
echo 'm56-ahci-readonly-qemu: PASS'
