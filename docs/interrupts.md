# BoringKernel bootstrap hardware interrupts

This document describes the deliberately narrow hardware-interrupt path currently used by the x86_64 QEMU bootstrap target. It is not a general interrupt-controller architecture and it is not a scheduler.

## Verified bootstrap path

The current QEMU acceptance path is:

```text
PIT channel 0
    ↓
legacy IRQ0
    ↓
8259-compatible master PIC
    ↓
IDT vector 32
    ↓
x86_64 IRQ assembly stub
    ↓
BoringKernel C IRQ dispatcher
    ↓
timer tick increment
    ↓
PIC End Of Interrupt
    ↓
register restore + iretq
```

A successful QEMU acceptance run observed ten distinct IRQ0 deliveries and ten timer ticks before declaring the interrupt path operational. The tick counter is incremented only by the IRQ0 handler; the acceptance loop never increments it directly and never calls the IRQ dispatcher directly.

## QEMU reference configuration

The bootstrap interrupt reference remains QEMU `q35`, but the current PIC-only test CPU is explicitly started with its local APIC feature disabled:

```text
-M q35 -cpu qemu64,apic=off
```

This is intentional. With QEMU's normal local APIC present, legacy PIC output is delivered through the local APIC LINT0 path. The local APIC's LVT entries start masked after reset, and configuring that LAPIC path would expand this milestone into APIC work. BoringKernel therefore uses a single-CPU, APIC-disabled QEMU reference configuration for this legacy PIC/PIT bootstrap proof.

This does **not** mean `q35` lacks APIC hardware, and it does not claim that the current bootstrap path is appropriate for modern physical PCs. It means only that this milestone proves the simplest isolated legacy interrupt path without introducing LAPIC, IOAPIC, HPET, ACPI or MADT support simultaneously.

A later interrupt architecture is expected to replace this bootstrap arrangement with a modern APIC-based design.

## 8259 PIC configuration

The master and slave 8259-compatible PICs are initialized in 8086 mode and remapped away from CPU exception vectors:

```text
master IRQ0–7   → vectors 32–39
slave  IRQ8–15  → vectors 40–47
```

The kernel installs DPL0 64-bit interrupt gates for all sixteen vectors before enabling maskable interrupts.

Initialization starts with both interrupt masks at `0xff`:

```text
master mask = 0xff
slave mask  = 0xff
```

After the PIT is configured and the interrupt path is ready, only IRQ0 is unmasked:

```text
master mask = 0xfe
slave mask  = 0xff
```

No keyboard, mouse, storage or other legacy device IRQ is deliberately enabled.

### EOI behavior

For a normal master-PIC IRQ, BoringKernel sends an End Of Interrupt command to the master PIC after dispatch.

For a normal slave-PIC IRQ, it acknowledges the slave first and then the master cascade path.

The bootstrap handler also implements the standard lightweight spurious checks for IRQ7 and IRQ15:

- a spurious IRQ7 receives no EOI because the master ISR bit is not set;
- a spurious IRQ15 receives an EOI only on the master cascade path because the slave ISR bit is not set.

Unexpected non-spurious IRQs are counted separately from timer ticks and are still acknowledged so the PIC is not left in-service indefinitely.

## PIT channel 0

The periodic source is the legacy PIT channel 0 in rate-generator mode.

The documented bootstrap input clock is:

```text
1,193,182 Hz
```

The requested frequency is:

```text
100 Hz
```

BoringKernel calculates the integer reload divisor from the input clock instead of embedding an unexplained timer constant. The nearest valid divisor for this request is:

```text
11932
```

The resulting rate is approximately:

```text
99.998491 Hz
```

The serial diagnostic reports this rounded to integer millihertz as:

```text
99998 mHz
```

The PIT therefore should not be described as an exact 100.000000 Hz time source.

## Interrupt enable ordering

Maskable interrupts remain disabled while the kernel establishes the path. The relevant ordering is:

```text
cli / IF=0
    ↓
existing IDT valid
    ↓
install IRQ gates 32–47
    ↓
remap PICs
    ↓
mask both PICs completely
    ↓
program PIT channel 0
    ↓
unmask only IRQ0
    ↓
read back and validate masks/state
    ↓
sti / IF=1
```

`sti` is executed only by `irq_enable()` after the IRQ subsystem has been initialized, IRQ0 has been deliberately unmasked, and the expected PIC masks have been verified.

## IRQ entry frame and return

`kernel/arch/x86_64/irq_stubs.S` contains the architecture-specific entry/return mechanics. Each PIC vector pushes a synthetic zero error code and the vector number, saves the general-purpose registers, constructs the same current CPL0 176-byte trap-frame shape used by the exception path, calls the C IRQ dispatcher, restores the interrupted state and returns with `iretq`.

Hardware IRQs and fatal CPU exceptions intentionally diverge after entry:

```text
fatal CPU exception → diagnose → cli/hlt forever
hardware timer IRQ  → tick → EOI → restore → iretq
```

The IRQ assembly does not schedule, switch stacks, create tasks or choose runnable work.

## Timer tick semantics

The timer exposes a `uint64_t` tick count. In the current single-core bootstrap, the counter is `volatile` because it is modified asynchronously by IRQ0 while normal kernel code polls it.

This is not a claim of general SMP or thread safety. There is no scheduler, no second CPU, no preemption model and no locking infrastructure in this milestone.

The IRQ handler performs no dynamic allocation and emits no serial line per tick.

## Acceptance proof

The normal QEMU acceptance test requires all earlier PMM, VMM, heap and exception-infrastructure checks to pass first. It then verifies:

1. both PICs begin fully masked;
2. PIT setup succeeds and the timer count is still zero before `sti`;
3. only IRQ0 becomes unmasked;
4. IF is deliberately enabled;
5. real asynchronous IRQ0 delivery advances the timer count;
6. at least ten separate IRQ0 deliveries occur;
7. continued delivery proves that End Of Interrupt handling is working in practice;
8. no unexpected IRQ is counted;
9. the final hardware-interrupt success marker is printed.

One verified run reported:

```text
Hardware interrupts:
Controller: 8259 PIC
PIC: remapped
Master vectors: 32-39
Slave vectors: 40-47
Initial master mask: 255
Initial slave mask: 255
IRQ0 vector: 32
Master mask: 254
Slave mask: 255

Timer:
Source: PIT channel 0
Input frequency: 1193182 Hz
Requested frequency: 100 Hz
Divisor: 11932
Effective frequency: 99998 mHz
IRQ: 0
Vector: 32
Timer: online

Interrupts: enabled

IRQ self-test:
  timer-delivery: PASS
  repeated-irqs: PASS
  acknowledgement: PASS
Ticks observed: 10
IRQ0 deliveries: 10
Unexpected IRQs: 0
Spurious IRQ7: 0
Spurious IRQ15: 0

BoringKernel hardware interrupt test passed.
```

The separate real Divide Error and Page Fault acceptance modes remain independent and continue to use vectors 0 and 14 respectively.

## Explicit limitations

This bootstrap implementation has no LAPIC, IOAPIC, x2APIC, APIC timer, HPET, ACPI/MADT parsing, SMP, interrupt affinity, scheduler, preemption, threads, task switching, processes, ring 3, keyboard IRQ driver or other device-driver framework.

Only IRQ0 is intentionally unmasked. PIC/PIT exists here only to prove that BoringKernel can receive, acknowledge and return from a real periodic hardware interrupt before more modern interrupt hardware is introduced in a separate milestone.
