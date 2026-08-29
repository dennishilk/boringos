#!/bin/sh
set -eu

QEMU_BIN=${QEMU:-qemu-system-x86_64}
LOG=build/m58-high-memory-serial.log
RAM_FILE=build/m58-high-memory.ram

rm -rf build/kernel build/iso_root
rm -f build/kernel.elf build/boringos.iso build/.test-mode "$RAM_FILE"
make TEST_MODE=runtime \
    TEST_HARNESS_C='kernel/core/m58_high_memory_test.c kernel/core/m58_high_memory_test_adapter.c' \
    all
rm -f "$LOG"
truncate -s 32G "$RAM_FILE"
trap 'rm -f "$RAM_FILE"' EXIT INT TERM
timeout 50s "$QEMU_BIN" \
    -M q35,memory-backend=m58ram -m 32G \
    -object memory-backend-file,id=m58ram,size=32G,mem-path="$RAM_FILE",share=on,prealloc=off \
    -cdrom build/boringos.iso -boot d \
    -display none -serial "file:$LOG" -monitor none \
    -no-reboot -no-shutdown || true

if grep -Fq 'M58 HIGH MEMORY TEST FAILED:' "$LOG"; then
    cat "$LOG"
    exit 1
fi
for marker in \
    'M58 PMM usable bytes:' \
    'M58 PMM usable frames:' \
    'M58 high frame:' \
    'M58 high frame >= 4GiB: PASS' \
    'M58 high frame write/read: PASS' \
    'M58 neighboring frame isolation: PASS' \
    'M58 high frame free: PASS' \
    'M58 accounting: PASS' \
    'M58 cleanup: PASS' \
    'M58 HIGH MEMORY TEST PASSED'
do
    grep -Fq "$marker" "$LOG" || { cat "$LOG"; exit 1; }
done

awk '/M58 PMM usable bytes:/ { if (($5 + 0) <= 4294967296) exit 1; found=1 } END { if (!found) exit 1 }' "$LOG"
grep -F 'M58 ' "$LOG"
echo 'm58-high-memory-qemu: PASS'
