# BoringKernel bootstrap hardware interrupts

This document describes the deliberately narrow x86_64 hardware-interrupt path used by the current QEMU bootstrap target. It now provides both the periodic timer source and the architecture boundary for **single-CPU kernel-task preemption**. It is not a modern APIC architecture and not a physical-PC compatibility claim.

## Verified hardware path

The source remains real legacy hardware as emulated by QEMU:

```text
PIT channel 0
    ↓
IRQ0
    ↓
8259-compatible master PIC
    ↓
IDT vector 32
    ↓
x86_64 IRQ entry stub
    ↓
complete normalized interrupt frame on current stack
    ↓
C IRQ dispatcher
    ↓
timer tick++
```

When preemption is disabled, the dispatcher acknowledges the PIC and the stub restores the same frame with `iretq`.

When preemption is enabled, the path continues:

```text
task_scheduler_tick(current frame)
    ↓
select current or another task frame
    ↓
PIC End Of Interrupt on current IRQ stack
    ↓
return selected frame pointer to assembly
    ↓
RSP = selected frame
    ↓
restore all GPRs
    ↓
iretq
```

The scheduling decision therefore originates from a genuine PIT IRQ0. The acceptance test never calls the scheduler tick directly and never manually increments the timer tick counter.

## QEMU reference configuration

The verified bootstrap reference remains:

```text
-M q35 -cpu qemu64,apic=off -m 128M
```

The local APIC CPU feature remains explicitly disabled so this milestone does not silently expand into LAPIC/IOAPIC configuration. PIC/PIT is temporary bootstrap infrastructure.

No SMP, LAPIC, IOAPIC, APIC timer, HPET, ACPI/MADT parsing, or interrupt affinity exists yet.

## PIC mapping and masks

The PIC pair remains in 8086 mode and remapped away from CPU exceptions:

```text
master IRQ0–7  → vectors 32–39
slave  IRQ8–15 → vectors 40–47
```

Initialization masks all IRQs:

```text
master = 0xff
slave  = 0xff
```

After PIT setup only IRQ0 is unmasked:

```text
master = 0xfe
slave  = 0xff
```

No other device IRQ is deliberately enabled.

Ordinary master IRQs receive master EOI. Ordinary slave IRQs receive slave EOI followed by master cascade EOI. Spurious IRQ7/IRQ15 continue to use ISR checks and the existing lightweight acknowledgement rules.

## PIT source

Channel 0 remains in rate-generator mode with bootstrap input clock:

```text
1,193,182 Hz
```

Requested rate:

```text
100 Hz
```

Calculated integer divisor:

```text
11932
```

Approximate resulting rate:

```text
99.998491 Hz
```

Serial output reports the rounded value as `99998 mHz`. Preemptive scheduling currently uses **one PIT tick per quantum**, so the nominal timeslice is approximately 10 ms, not a precision realtime guarantee.

## Interrupt enable ordering

The initial hardware path still follows:

```text
IF=0
→ install exception and IRQ gates
→ remap PIC
→ mask PICs
→ program PIT
→ unmask IRQ0 only
→ validate masks/state
→ sti
```

Preemption itself is enabled only later, after the ordinary repeated-IRQ test and cooperative-context test have succeeded and the preemptive task stacks/initial frames have already been created.

Task creation, scheduler-mode changes, and finished-stack destruction use short IF-off critical sections so IRQ0 cannot observe half-updated task metadata. This is a single-CPU bootstrap technique, not an SMP synchronization design.

## Complete IRQ frame

The IRQ and fatal-exception entry code now share one **192-byte** normalized ring-0 frame:

```text
offset   field
0x00     RSP copy
0x08     SS copy
0x10     R15
0x18     R14
0x20     R13
0x28     R12
0x30     R11
0x38     R10
0x40     R9
0x48     R8
0x50     RSI
0x58     RDI
0x60     RBP
0x68     RDX
0x70     RCX
0x78     RBX
0x80     RAX
0x88     vector
0x90     error code
0x98     RIP
0xA0     CS
0xA8     RFLAGS
0xB0     hardware RSP
0xB8     hardware SS
```

For a hardware IRQ, the CPU provides the long-mode interrupt-return state and the per-vector stub supplies normalized `error_code = 0` plus the vector number. The common stub then saves all 15 integer GPRs. It copies the CPU-saved stack state into the first two fields for convenient C inspection while retaining the original return words at the tail for `iretq`.

The complete integer state preserved across timer preemption is:

```text
RAX RBX RCX RDX RSI RDI RBP
R8 R9 R10 R11 R12 R13 R14 R15
RIP CS RFLAGS RSP SS
```

There is no FPU/SIMD context. Kernel C continues to be compiled with accidental x87/MMX/SSE/SSE2 generation disabled.

## Selected-frame return

`x86_64_irq_dispatch()` returns a pointer to the complete frame that assembly should restore.

Conceptually:

```c
struct x86_64_trap_frame *x86_64_irq_dispatch(
    struct x86_64_trap_frame *current);
```

If scheduling is inactive, this is normally the same pointer. If a timer preemption selects another task, the returned pointer belongs to that other task's stack.

Assembly contains no scheduling policy. It only:

1. loads the returned frame pointer into `RSP`;
2. skips the two C-facing stack copies;
3. restores all saved GPRs;
4. skips vector/error;
5. executes `iretq` using the selected frame's hardware return state.

## EOI ordering and abandoning an IRQ stack

EOI ordering is critical once the interrupted task might not resume immediately.

BoringKernel first asks the scheduler which frame should be restored **without changing stacks**. While still executing on the current IRQ stack, `irq.c` sends the required PIC EOI. Only then does it return the selected frame pointer to assembly.

Therefore the path is:

```text
scheduler decision
→ PIC EOI
→ return selected frame
→ assembly changes RSP
→ restore
→ iretq
```

The kernel never depends on eventually returning through the old task's C call stack to acknowledge the PIC.

## Timer, scheduler and preemption counters

The implementation tracks distinct concepts:

- `timer_ticks`: PIT time-source progress;
- `timer_irq_count`: accepted IRQ0 deliveries;
- `scheduler_ticks`: IRQ0 deliveries while preemption mode is active;
- `preemptions`: actual restore-frame changes to another execution context.

A one-tick round-robin test often makes these values close, but they are not defined to be permanently identical.

## No allocation or logging in the scheduling IRQ path

The timer/preemption IRQ path performs no:

```text
kmalloc
kfree
PMM allocation
VMM mapping
per-tick serial logging
```

Task structures and stacks already exist before preemption begins. The IRQ path is intentionally bounded and small.

## Acceptance proof

The normal QEMU test first retains the earlier hardware proof:

```text
IRQ self-test:
  timer-delivery: PASS
  repeated-irqs: PASS
  acknowledgement: PASS
Ticks observed: 10
IRQ0 deliveries: 10
Unexpected IRQs: 0

BoringKernel hardware interrupt test passed.
```

It then retains the cooperative task test and finally enables timer-driven scheduling for two CPU-bound tasks that contain no `task_yield()` calls.

A verified preemptive branch run reported:

```text
Timer ticks during test: 7
Scheduler ticks: 7
Preemptions: 7
Task A slices: 3
Task B slices: 3
Task A resumes: 2
Task B resumes: 2
Cooperative yields during test: 0
```

Both task-local state and the full integer register preservation probe passed, bootstrap returned through its saved IRQ frame, finished tasks were not selected again, and both task stacks were freed afterward.

The separate real Divide Error and Page Fault acceptance modes remain green and continue to prove vectors 0 and 14 respectively.

## Limitations

The current preemptive IRQ path is verified only for:

- one x86_64 bootstrap CPU;
- CPL0 kernel tasks;
- one shared kernel address space;
- QEMU `q35` with `qemu64,apic=off`;
- the legacy PIT/PIC timer route.

It does not provide processes, ring 3, separate address spaces, CR3 task switching, TSS user-stack switching, FPU/SIMD switching, sleeping/blocking, synchronization primitives, priorities, realtime scheduling, SMP, or modern APIC timer routing.
