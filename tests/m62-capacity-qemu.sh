#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
QEMU=$(printenv QEMU 2>/dev/null || printf '%s' qemu-system-x86_64)
LOG="$ROOT/build/m62-capacity-serial.log"
QEMU_LOG="$ROOT/build/m62-capacity-qemu.log"
rm -rf "$ROOT/build/kernel" "$ROOT/build/iso_root"
rm -f "$ROOT/build/kernel.elf" "$ROOT/build/boringos.iso" "$ROOT/build/.test-mode"
make -C "$ROOT" TEST_MODE=runtime TEST_HARNESS_C='kernel/core/m62_capacity_test.c kernel/core/m62_capacity_test_adapter.c' all
rm -f "$LOG" "$QEMU_LOG"
timeout 45s "$QEMU" -M q35 -cpu qemu64,apic=off -m 256M -cdrom "$ROOT/build/boringos.iso" -boot d -display none -serial "file:$LOG" -monitor none -no-reboot -no-shutdown >/dev/null 2>"$QEMU_LOG" || true
if grep -Fq 'M62 CAPACITY TEST FAILED:' "$LOG"; then cat "$LOG"; exit 1; fi
grep -Fq 'M62 CAPACITY SUCCESS max_processes=24 max_tasks=24 cycles=3 baseline_restored=yes' "$LOG" || { cat "$LOG"; cat "$QEMU_LOG"; exit 1; }
cycle=1
while [ "$cycle" -le 3 ]; do
  grep -Fq "M62 CAPACITY cycle=$cycle live_processes=24 live_tasks=24" "$LOG"
  grep -Fq "M62 CAPACITY cycle=$cycle schedulable=24" "$LOG"
  grep -Eq "^M62 CAPACITY cycle=$cycle baseline_processes=0 baseline_tasks=0 heap_allocations=[0-9]+$" "$LOG"
  cycle=$((cycle + 1))
done
printf '%s\n' 'MAX_CONCURRENT_PROCESS_TEST=24' 'MAX_CONCURRENT_TASK_TEST=24' 'CHURN_CYCLES=3' 'POST_CHURN_BASELINE_RESTORED=YES' > "$ROOT/build/m62-capacity-proof.txt"
echo 'm62-capacity-qemu: PASS'
