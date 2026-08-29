#!/bin/sh
set -eu

QEMU_BIN=${QEMU:-qemu-system-x86_64}
LOG=build/m56-ahci-readonly-serial.log
DISK=build/m56-ahci-readonly.raw
HARNESS='kernel/core/ahci.c kernel/arch/x86_64/ahci_hw.c kernel/core/ahci_read_test.c kernel/core/ahci_read_test_adapter.c'

make TEST_MODE=runtime TEST_HARNESS_C="$HARNESS" all
mkdir -p build
rm -f "$LOG" "$DISK"

python3 - "$DISK" <<'PY'
import sys

path = sys.argv[1]
sector_size = 512
sectors = 16384
with open(path, "wb") as image:
    for lba in range(sectors):
        image.write(bytes(
            ((lba * 17 + offset * 13 + (lba >> 8) + 0x5a) & 0xff)
            for offset in range(sector_size)
        ))
PY

timeout 35s "$QEMU_BIN" \
    -M q35 -m 256M -cdrom build/boringos.iso -boot d \
    -display none -serial "file:$LOG" -monitor none \
    -no-reboot -no-shutdown \
    -drive "file=$DISK,format=raw,if=ide,index=0,media=disk" || true

if grep -Fq 'M56 AHCI READ QEMU FAILED:' "$LOG"; then
    cat "$LOG"
    exit 1
fi

for marker in \
    'BoringKernel 0.0.56-dev' \
    'M56 AHCI IDENTIFY: port=' \
    'M56 AHCI geometry: blocks=16384 logical_block_size=512' \
    'M56 AHCI DMA frames: 4' \
    'M56 AHCI generic block registration: PASS' \
    'M56 AHCI first LBA: PASS' \
    'M56 AHCI middle LBA: PASS' \
    'M56 AHCI last LBA: PASS' \
    'M56 AHCI multi-sector read: PASS' \
    'M56 AHCI neighboring sectors: PASS' \
    'M56 AHCI out-of-range pre-I/O: PASS' \
    'M56 AHCI read-only write rejection: PASS' \
    'M56 AHCI READ DMA EXT commands:' \
    'M56 AHCI synchronous read-only block path QEMU passed.'
do
    grep -Fq "$marker" "$LOG" || { cat "$LOG"; exit 1; }
done

if grep -Fq 'VirtIO block device QEMU passed.' "$LOG"; then
    cat "$LOG"
    echo 'm56-ahci-readonly-qemu: unexpected VirtIO read evidence' >&2
    exit 1
fi

cat "$LOG"
echo 'm56-ahci-readonly-qemu: PASS'
