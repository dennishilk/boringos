# BoringKernel bootstrap hardware interrupts

This document describes the deliberately narrow x86_64 hardware-interrupt path used by the current QEMU bootstrap target. It provides the periodic timer source and architecture boundary for **single-CPU CPL0 task preemption**, including preemptive switches between tasks owned by different process address spaces. It is not a modern APIC architecture and not a physical-PC compatibility claim.

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

When preemption is enabled, the path can now continue across process roots:

```text
task_scheduler_tick(current frame)
    ↓
select current or another task frame
    ↓
activate selected task's process root if needed
    ↓
real CR3 load while shared kernel mappings remain active
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

The scheduling decision originates from a genuine PIT IRQ0. The acceptance test never calls the scheduler tick directly and never manually increments the timer tick counter.

## QEMU reference configuration

The verified bootstrap reference remains:

```text
-M q35 -cpu qemu64,apic=off -m 128M
```

The local APIC CPU feature remains explicitly disabled so this work does not silently expand into LAPIC/IOAPIC configuration. PIC/PIT is temporary bootstrap infrastructure.

No SMP, LAPIC, IOAPIC, APIC timer, HPET, ACPI/MADT parsing, or interrupt affinity exists yet.

## PIC mapping and masks

The PIC pair remains in 8086 mode and remapped away from CPU exceptions:

```text
master IRQ0-7  → vectors 32-39
slave  IRQ8-15 → vectors 40-47
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

Preemption is enabled only after the ordinary repeated-IRQ test and after required tasks/stacks/process mappings already exist.

Task creation, process/address-space bookkeeping, scheduler-mode changes, and finished-stack/process cleanup use short IF-off critical sections so IRQ0 cannot observe half-updated metadata. This is a single-CPU bootstrap technique, not an SMP synchronization design.

## Complete IRQ frame

The IRQ and fatal-exception entry code share one **192-byte** normalized ring-0 frame:

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

For a hardware IRQ, the CPU provides the long-mode return state and the per-vector stub supplies normalized `error_code = 0` plus the vector number. The common stub saves all 15 integer GPRs. The first RSP/SS pair is a convenient C-facing copy; the original hardware return words remain at the tail for `iretq`.

The complete integer state preserved across timer preemption is:

```text
RAX RBX RCX RDX RSI RDI RBP
R8 R9 R10 R11 R12 R13 R14 R15
RIP CS RFLAGS RSP SS
```

There is no FPU/SIMD context. Kernel C remains compiled with accidental x87/MMX/SSE/SSE2 generation disabled.

## Selected-frame return and CR3 ownership

`x86_64_irq_dispatch()` returns a pointer to the complete frame that assembly should restore.

If scheduling is inactive, this is normally the same pointer. If timer preemption selects another task, `task_scheduler_tick()` may first activate the selected task's owning process address space. The selected frame still belongs to that task's stack.

Current process roots share the complete required kernel higher half, including the running IRQ code/stack and all ordinary task stacks. A CR3 change can therefore occur before leaving the old IRQ stack without making the currently executing kernel state disappear.

Assembly contains no scheduling or process policy. It only loads the selected frame, restores saved GPRs, skips normalized vector/error data, and executes `iretq`.

## EOI ordering and abandoning an IRQ stack

EOI ordering remains critical.

The scheduler may choose another task and may already switch to that task's CR3, but **the CPU is still executing on the original IRQ stack**, which remains mapped identically through the shared higher half.

`irq.c` then sends the required PIC EOI. Only after EOI does it return the selected frame pointer to assembly, which changes `RSP` and restores the target task.

Therefore the ordering is:

```text
scheduler decision
→ optional process CR3 switch
→ PIC EOI on current shared kernel IRQ stack
→ return selected frame
→ assembly changes RSP
→ restore
→ iretq
```

The kernel never depends on eventually returning through the old task's C call stack to acknowledge the PIC.

## Timer, scheduler, preemption and address-space counters

The implementation tracks distinct concepts:

- `timer_ticks`: PIT time-source progress;
- `timer_irq_count`: accepted IRQ0 deliveries;
- `scheduler_ticks`: IRQ0 deliveries while preemption is active;
- `preemptions`: actual restore-frame changes to another execution context;
- `address_space_switches`: actual root changes that caused a CR3 load.

Existing PID-0 preemption can switch task frames without changing CR3. The process-isolation acceptance instead uses different roots and verifies repeated actual CR3 loads.

## No allocation or logging in the scheduling IRQ path

The timer/preemption IRQ path performs no:

```text
kmalloc
kfree
PMM allocation
page-table allocation
VMM mapping
per-tick serial logging
```

Task/process structures, stacks, roots, and private test mappings already exist before preemption begins. The only address-space operation required in the hot path is activating an already prepared root when the selected process differs.

## Acceptance proof

The normal QEMU test retains the earlier hardware proof:

```text
IRQ self-test:
  timer-delivery: PASS
  repeated-irqs: PASS
  acknowledgement: PASS
Ticks observed: 10
IRQ0 deliveries: 10
Unexpected IRQs: 0
```

It retains the existing cooperative and full-GPR PIT-preemption regressions. The independent preemption regression still reports seven scheduler entries/preemptions, three slices for both tasks, two resumes each, and zero cooperative yields.

The 0.0.9-dev process acceptance additionally binds one CPU-bound preemptive task to PID 1 and one to PID 2. Both repeatedly access the same lower-half VA while real PIT IRQ0 switches task context and CR3 together.

A verified clean-source run reported:

```text
Preemptive CR3 switches: 7
Process A slices:         3
Process B slices:         3
```

Both process-isolation patterns survived every resume, the scheduler restored PID 0/bootstrap root, process-owned page tables/data frames were reclaimed, and the separate real Divide Error and Page Fault acceptance modes remained green.

## Limitations

The current IRQ/preemption path is verified only for:

- one x86_64 bootstrap CPU;
- CPL0 kernel tasks;
- independent process roots whose required kernel higher-half mappings are shared;
- QEMU `q35` with `qemu64,apic=off`;
- the legacy PIT/PIC timer route.

It does not provide ring 3, user privilege transitions, syscalls, TSS user stacks, PCID, FPU/SIMD switching, sleeping/blocking, synchronization primitives, priorities, realtime scheduling, SMP, or modern APIC timer routing.
