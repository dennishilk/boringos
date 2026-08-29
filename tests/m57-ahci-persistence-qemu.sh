#!/bin/sh
set -eu

QEMU_BIN=${QEMU:-qemu-system-x86_64}
DISK=build/m57-ahci-persistence.raw
LOG1=build/m57-ahci-persistence-first.log
LOG2=build/m57-ahci-persistence-reboot.log
HARNESS='kernel/core/ahci.c kernel/arch/x86_64/ahci_hw.c kernel/core/ahci_persistence_test.c kernel/core/ahci_persistence_test_adapter.c'

make TEST_MODE=runtime TEST_HARNESS_C="$HARNESS" all
mkdir -p build
rm -f "$DISK" "$LOG1" "$LOG2"
python3 - "$DISK" <<'PY'
import sys
with open(sys.argv[1], 'wb') as f:
    for lba in range(16384):
        f.write(bytes(((lba + offset) & 0xff) for offset in range(512)))
PY
BEFORE=$(sha256sum "$DISK" | awk '{print $1}')

run_qemu() {
    log=$1
    timeout 45s "$QEMU_BIN" -M q35 -m 256M \
        -cdrom build/boringos.iso -boot d -display none \
        -serial "file:$log" -monitor none -no-reboot -no-shutdown \
        -drive "file=$DISK,format=raw,if=ide,index=0,media=disk" || true
    if grep -Fq 'M57 AHCI PERSISTENCE FAILED:' "$log"; then
        cat "$log"
        exit 1
    fi
}

run_qemu "$LOG1"
grep -Fq 'M57 AHCI write LBA/PRDT/null bounds: PASS' "$LOG1"
grep -Fq 'M57 AHCI WRITE DMA completion: PASS' "$LOG1"
grep -Fq 'M57 AHCI FLUSH CACHE completion: PASS' "$LOG1"
grep -Fq 'M57 AHCI immediate readback: PASS' "$LOG1"
AFTER_WRITE=$(sha256sum "$DISK" | awk '{print $1}')
test "$BEFORE" != "$AFTER_WRITE"

python3 - "$DISK" <<'PY'
import sys
data = open(sys.argv[1], 'rb').read()
start = 4096 * 512
expected = bytes((0xa5 ^ (i & 0xff)) for i in range(2048))
assert data[start:start + 2048] == expected
assert data[start - 512:start] == bytes(((4095 + i) & 0xff) for i in range(512))
assert data[start + 2048:start + 2560] == bytes(((4100 + i) & 0xff) for i in range(512))
PY

run_qemu "$LOG2"
grep -Fq 'M57 AHCI reboot persistence read: PASS' "$LOG2"
grep -Fq 'M57 AHCI second boot performed no write: PASS' "$LOG2"
AFTER_REBOOT=$(sha256sum "$DISK" | awk '{print $1}')
test "$AFTER_WRITE" = "$AFTER_REBOOT"

cat "$LOG1"
cat "$LOG2"
echo "M57 initial RAW SHA256: $BEFORE"
echo "M57 persisted RAW SHA256: $AFTER_WRITE"
echo 'm57-ahci-persistence-qemu: PASS'
