#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
QEMU=${QEMU:-qemu-system-x86_64}
QEMU_CPU=${QEMU_CPU:-qemu64,apic=off}
LOG=$(mktemp)
PID=

cleanup() {
    if [ -n "${PID}" ] && kill -0 "${PID}" 2>/dev/null; then
        kill "${PID}" 2>/dev/null || true
        wait "${PID}" 2>/dev/null || true
    fi
    rm -f "${LOG}"
}
trap cleanup EXIT INT TERM

make -C "${ROOT}" TEST_MODE=normal

"${QEMU}" \
    -M q35 \
    -cpu "${QEMU_CPU}" \
    -m 128M \
    -cdrom "${ROOT}/build/boringos.iso" \
    -boot d \
    -display none \
    -serial "file:${LOG}" \
    -monitor none \
    -no-reboot \
    -no-shutdown &
PID=$!

attempt=0
while [ "${attempt}" -lt 150 ]; do
    if grep -Fqx 'BoringKernel process/address-space test passed.' "${LOG}" 2>/dev/null; then
        break
    fi

    if ! kill -0 "${PID}" 2>/dev/null; then
        break
    fi

    attempt=$((attempt + 1))
    sleep 0.1
done

status=0
for line in \
    'BoringOS booting...' \
    'BoringKernel 0.0.58-dev' \
    'Arch: x86_64' \
    'Hello from BoringKernel.' \
    'Physical memory manager:' \
    'Page size: 4096 bytes' \
    'PMM: online' \
    'PMM self-test:' \
    '  allocate: PASS' \
    '  unique: PASS' \
    '  aligned: PASS' \
    '  usable: PASS' \
    '  free: PASS' \
    '  invalid-free: PASS' \
    '  bookkeeping: PASS' \
    'BoringKernel physical memory test passed.' \
    'Virtual memory manager:' \
    'Paging: x86_64 4-level' \
    'VMM: online' \
    'VMM self-test:' \
    '  frame-allocation: PASS' \
    '  unmapped-check: PASS' \
    '  map: PASS' \
    '  translate: PASS' \
    '  write-read: PASS' \
    '  unmap: PASS' \
    '  frame-release: PASS' \
    'Test pattern: 0x424F52494E474F53' \
    'BoringKernel virtual memory test passed.' \
    'Kernel heap:' \
    'Mapped pages: 2' \
    'Mapped capacity: 8192 bytes' \
    'Alignment: 16 bytes' \
    'Heap: online' \
    'Heap self-test:' \
    '  allocate: PASS' \
    '  alignment: PASS' \
    '  non-overlap: PASS' \
    '  write-read: PASS' \
    '  growth: PASS' \
    '  free: PASS' \
    '  reuse: PASS' \
    '  double-free: PASS' \
    '  invalid-free: PASS' \
    '  bookkeeping: PASS' \
    'Allocation sizes tested: 1,16,64,200,6000,4096 bytes' \
    'Final used bytes: 0' \
    'BoringKernel heap test passed.' \
    'Exception handling:' \
    'IDT entries: 32' \
    'IDTR limit: 4095' \
    'IDT: loaded' \
    'Exceptions: online' \
    'BoringKernel exception infrastructure test passed.' \
    'Hardware interrupts:' \
    'Controller: 8259 PIC' \
    'PIC: remapped' \
    'Master vectors: 32-39' \
    'Slave vectors: 40-47' \
    'Initial master mask: 255' \
    'Initial slave mask: 255' \
    'IRQ0 vector: 32' \
    'Master mask: 254' \
    'Slave mask: 255' \
    'Timer:' \
    'Source: PIT channel 0' \
    'Input frequency: 1193182 Hz' \
    'Requested frequency: 100 Hz' \
    'Divisor: 11932' \
    'Effective frequency: 99998 mHz' \
    'IRQ: 0' \
    'Vector: 32' \
    'Timer: online' \
    'Interrupts: enabled' \
    'IRQ self-test:' \
    '  timer-delivery: PASS' \
    '  repeated-irqs: PASS' \
    '  acknowledgement: PASS' \
    'Unexpected IRQs: 0' \
    'BoringKernel hardware interrupt test passed.' \
    'Kernel tasks:' \
    'Mode: cooperative' \
    'Tasks created: 2' \
    'Task stack size: 16384 bytes' \
    'Bootstrap task ID: 0' \
    'Scheduler: online' \
    'Task A:' \
    'Task B:' \
    'Context switch self-test:' \
    '  task-a-start: PASS' \
    '  task-b-start: PASS' \
    '  alternating-switch: PASS' \
    '  stack-isolation: PASS' \
    '  register-state: PASS' \
    '  task-return: PASS' \
    '  timer-coexistence: PASS' \
    '  stack-cleanup: PASS' \
    '  heap-bookkeeping: PASS' \
    'Task stacks freed: 2' \
    'Task heap allocations after cleanup: 0' \
    'BoringKernel cooperative task test passed.' \
    'Preemptive scheduler:' \
    'Policy: round-robin' \
    'Timer source: PIT IRQ0' \
    'Timer vector: 32' \
    'Quantum: 1 tick' \
    'Preemption: enabled during test' \
    'Preemption self-test:' \
    '  task-a-progress: PASS' \
    '  task-b-progress: PASS' \
    '  no-cooperative-yield: PASS' \
    '  repeated-preemption: PASS' \
    '  stack-isolation: PASS' \
    '  local-state: PASS' \
    '  register-state: PASS' \
    '  timer-delivery: PASS' \
    '  bootstrap-return: PASS' \
    '  finished-task-skip: PASS' \
    '  stack-sentinel: PASS' \
    '  stack-cleanup: PASS' \
    '  heap-bookkeeping: PASS' \
    'Cooperative yields during test: 0' \
    'Task stacks freed: 2' \
    'Task heap allocations after preemption cleanup: 0' \
    'BoringKernel preemptive scheduling test passed.' \
    'Process A kernel mappings active.' \
    'Process B kernel mappings active.' \
    'Process subsystem:' \
    'Bootstrap PID: 0' \
    'Processes created: 2' \
    'Address spaces created: 2' \
    'Process model: online' \
    'Address-space test:' \
    'Process A PID: 1' \
    'Process B PID: 2' \
    'Process/address-space self-test:' \
    '  process-create: PASS' \
    '  unique-pid: PASS' \
    '  address-space-create: PASS' \
    '  distinct-root: PASS' \
    '  same-va-different-pa: PASS' \
    '  cr3-switch: PASS' \
    '  kernel-mappings: PASS' \
    '  process-a-isolation: PASS' \
    '  process-b-isolation: PASS' \
    '  preemptive-address-space-switch: PASS' \
    '  bootstrap-return: PASS' \
    '  address-space-cleanup: PASS' \
    '  pmm-bookkeeping: PASS' \
    'BoringKernel process/address-space test passed.'
do
    if ! grep -Fqx "${line}" "${LOG}"; then
        echo "missing serial line: ${line}" >&2
        status=1
    fi
done

for pattern in \
    '^Usable memory: [1-9][0-9]* bytes$' \
    '^Usable frames: [1-9][0-9]*$' \
    '^Active root table: [1-9][0-9]*$' \
    '^HHDM offset: [1-9][0-9]*$' \
    '^Test virtual address: [1-9][0-9]*$' \
    '^Page-table frames allocated: [0-9]+$' \
    '^Test physical frame: [1-9][0-9]*$' \
    '^Translation result: [1-9][0-9]*$' \
    '^Virtual base: [1-9][0-9]*$' \
    '^Virtual limit: [1-9][0-9]*$' \
    '^Free payload: [1-9][0-9]* bytes$' \
    '^Initial PMM frames consumed: [1-9][0-9]*$' \
    '^Initial page-table frames: [0-9]+$' \
    '^Growth mappings created: [1-9][0-9]*$' \
    '^Growth PMM frames consumed: [1-9][0-9]*$' \
    '^Final mapped pages: [3-9][0-9]*$' \
    '^Final free bytes: [1-9][0-9]*$' \
    '^IDTR base: 0x[0-9A-F]{16}$' \
    '^Code selector: 0x[0-9A-F]{16}$' \
    '^Context switches: [1-9][0-9]*$' \
    '^Ticks before task test: [1-9][0-9]*$' \
    '^Ticks after task test: [1-9][0-9]*$' \
    '^Timer ticks during test: [1-9][0-9]*$' \
    '^Scheduler ticks: [1-9][0-9]*$' \
    '^Preemptions: [1-9][0-9]*$' \
    '^Task A slices: [1-9][0-9]*$' \
    '^Task B slices: [1-9][0-9]*$' \
    '^Task A resumes: [1-9][0-9]*$' \
    '^Task B resumes: [1-9][0-9]*$' \
    '^Test virtual address: 0x[0-9A-F]{16}$' \
    '^Process A root: 0x[0-9A-F]{16}$' \
    '^Process B root: 0x[0-9A-F]{16}$' \
    '^Process A physical frame: 0x[0-9A-F]{16}$' \
    '^Process B physical frame: 0x[0-9A-F]{16}$' \
    '^Address-space switches: [1-9][0-9]*$' \
    '^Preemptive CR3 switches: [1-9][0-9]*$' \
    '^Process A slices: [1-9][0-9]*$' \
    '^Process B slices: [1-9][0-9]*$'
do
    if ! grep -Eq "${pattern}" "${LOG}"; then
        echo "missing runtime value matching: ${pattern}" >&2
        status=1
    fi
done

TICKS=$(sed -n 's/^Ticks observed: \([0-9][0-9]*\)$/\1/p' "${LOG}" | tail -n 1)
DELIVERIES=$(sed -n 's/^IRQ0 deliveries: \([0-9][0-9]*\)$/\1/p' "${LOG}" | tail -n 1)
SWITCHES=$(sed -n 's/^Context switches: \([0-9][0-9]*\)$/\1/p' "${LOG}" | tail -n 1)
TASK_TICKS_BEFORE=$(sed -n 's/^Ticks before task test: \([0-9][0-9]*\)$/\1/p' "${LOG}" | tail -n 1)
TASK_TICKS_AFTER=$(sed -n 's/^Ticks after task test: \([0-9][0-9]*\)$/\1/p' "${LOG}" | tail -n 1)
PREEMPT_TIMER_TICKS=$(sed -n 's/^Timer ticks during test: \([0-9][0-9]*\)$/\1/p' "${LOG}" | tail -n 1)
SCHEDULER_TICKS=$(sed -n 's/^Scheduler ticks: \([0-9][0-9]*\)$/\1/p' "${LOG}" | tail -n 1)
PREEMPTIONS=$(sed -n 's/^Preemptions: \([0-9][0-9]*\)$/\1/p' "${LOG}" | tail -n 1)
TASK_A_SLICES=$(sed -n 's/^Task A slices: \([0-9][0-9]*\)$/\1/p' "${LOG}" | tail -n 1)
TASK_B_SLICES=$(sed -n 's/^Task B slices: \([0-9][0-9]*\)$/\1/p' "${LOG}" | tail -n 1)
TASK_A_RESUMES=$(sed -n 's/^Task A resumes: \([0-9][0-9]*\)$/\1/p' "${LOG}" | tail -n 1)
TASK_B_RESUMES=$(sed -n 's/^Task B resumes: \([0-9][0-9]*\)$/\1/p' "${LOG}" | tail -n 1)
PROCESS_CR3_SWITCHES=$(sed -n 's/^Preemptive CR3 switches: \([0-9][0-9]*\)$/\1/p' "${LOG}" | tail -n 1)
PROCESS_A_SLICES=$(sed -n 's/^Process A slices: \([0-9][0-9]*\)$/\1/p' "${LOG}" | tail -n 1)
PROCESS_B_SLICES=$(sed -n 's/^Process B slices: \([0-9][0-9]*\)$/\1/p' "${LOG}" | tail -n 1)
ITERATION_LINES=$(grep -Fxc '  iterations: 3' "${LOG}" || true)
LOCAL_STATE_LINES=$(grep -Fxc '  local-state: PASS' "${LOG}" || true)

if [ -z "${TICKS}" ] || [ "${TICKS}" -lt 10 ]; then
    echo 'timer self-test did not observe at least 10 ticks' >&2
    status=1
fi
if [ -z "${DELIVERIES}" ] || [ "${DELIVERIES}" -lt 10 ]; then
    echo 'IRQ self-test did not observe at least 10 IRQ0 deliveries' >&2
    status=1
fi
if [ -z "${SWITCHES}" ] || [ "${SWITCHES}" -lt 7 ]; then
    echo 'cooperative task test did not observe at least 7 real context switches' >&2
    status=1
fi
if [ "${ITERATION_LINES}" -ne 2 ] || [ "${LOCAL_STATE_LINES}" -lt 3 ]; then
    echo 'cooperative or preemptive task local-state checks are missing' >&2
    status=1
fi
if [ -z "${TASK_TICKS_BEFORE}" ] || [ -z "${TASK_TICKS_AFTER}" ] ||
   [ "${TASK_TICKS_AFTER}" -le "${TASK_TICKS_BEFORE}" ]; then
    echo 'timer did not progress during cooperative task execution' >&2
    status=1
fi
if [ -z "${PREEMPT_TIMER_TICKS}" ] || [ "${PREEMPT_TIMER_TICKS}" -lt 6 ]; then
    echo 'preemptive test did not observe at least 6 real timer ticks' >&2
    status=1
fi
if [ -z "${SCHEDULER_TICKS}" ] || [ "${SCHEDULER_TICKS}" -lt 6 ]; then
    echo 'preemptive test did not enter the scheduler at least 6 times' >&2
    status=1
fi
if [ -z "${PREEMPTIONS}" ] || [ "${PREEMPTIONS}" -lt 6 ]; then
    echo 'preemptive test did not perform at least 6 timer-driven context switches' >&2
    status=1
fi
if [ -z "${TASK_A_SLICES}" ] || [ "${TASK_A_SLICES}" -lt 3 ] ||
   [ -z "${TASK_B_SLICES}" ] || [ "${TASK_B_SLICES}" -lt 3 ]; then
    echo 'both preemptive tasks did not receive at least 3 scheduling slices' >&2
    status=1
fi
if [ -z "${TASK_A_RESUMES}" ] || [ "${TASK_A_RESUMES}" -lt 2 ] ||
   [ -z "${TASK_B_RESUMES}" ] || [ "${TASK_B_RESUMES}" -lt 2 ]; then
    echo 'both preemptive tasks did not resume repeatedly after timer preemption' >&2
    status=1
fi
if [ -z "${PROCESS_CR3_SWITCHES}" ] || [ "${PROCESS_CR3_SWITCHES}" -lt 6 ]; then
    echo 'process test did not perform repeated real CR3 switches' >&2
    status=1
fi
if [ -z "${PROCESS_A_SLICES}" ] || [ "${PROCESS_A_SLICES}" -lt 3 ] ||
   [ -z "${PROCESS_B_SLICES}" ] || [ "${PROCESS_B_SLICES}" -lt 3 ]; then
    echo 'both process-owned tasks did not receive at least 3 preemptive slices' >&2
    status=1
fi

if grep -Eiq 'PMM self-test FAILED|Physical memory manager: FAILED|VMM: FAILED|VMM self-test FAILED|Kernel heap: FAILED|Heap self-test FAILED|Exception handling: FAILED|Hardware interrupts: FAILED|Timer: FAILED|Hardware interrupt self-test FAILED|Kernel tasks: FAILED|Cooperative task self-test FAILED|Preemptive scheduler: FAILED|Preemptive scheduling self-test FAILED|Process subsystem: FAILED|Process/address-space self-test FAILED|heap corruption|Fatal exception: controlled halt|general protection fault|triple fault|reboot' "${LOG}"; then
    echo 'kernel reported a failure during normal boot' >&2
    status=1
fi

cat "${LOG}"

if [ "${status}" -ne 0 ]; then
    echo 'BoringKernel QEMU boot verification FAILED.' >&2
    exit "${status}"
fi

echo 'BoringKernel QEMU boot verification passed.'
