# BoringOS architecture — current bootstrap state

These decisions remain deliberately narrow and provisional. This document describes implemented behavior only.

## Platform and boot boundary

- **Architecture:** x86_64.
- **Reference machine:** QEMU `q35`, one `qemu64,apic=off` CPU for the current legacy PIC/PIT bootstrap path.
- **Bootloader:** Limine 12.5.2.
- **Kernel:** freestanding, statically linked ELF64, primarily C with small isolated architecture assembly.
- **Privilege:** CPL0 only. There is no ring-3/userspace execution.
- **Address space:** one shared kernel address space. BoringKernel still adopts Limine's active four-level root rather than replacing it.

The current normal boot path proves PMM, selected VMM mappings, the bounded kernel heap, exception infrastructure, repeated hardware PIT/PIC interrupts, cooperative kernel contexts, and timer-driven preemptive kernel scheduling.

## Physical and virtual memory

### PMM

BoringKernel allocates 4096-byte physical frames only from Limine memory-map entries marked usable. A bounded static bitmap and usable-region table provide deterministic first-fit allocation, free/reuse, and invalid/double-free rejection. Bootloader-reclaimable and all other non-usable types remain non-allocatable.

### VMM

The VMM requests x86_64 four-level paging, discovers Limine's HHDM, reads the active root from `CR3`, and controls selected 4-KiB mappings without claiming complete address-space ownership.

New page-table frames come from PMM and are accessed through the HHDM. Empty page tables are returned to PMM only when VMM allocated them itself. The early mapping test window remains:

```text
[0xffffff0000000000, 0xffffff0000200000)
```

The QEMU self-test maps a PMM frame, translates it, performs a real write/read, unmaps it, invalidates the TLB entry, and restores PMM/VMM bookkeeping.

## Kernel heap

The bounded heap owns:

```text
[0xffffff0000200000, 0xffffff0001200000)
```

It starts with two mapped pages, grows one 4096-byte page at a time through PMM + VMM, uses deterministic first-fit allocation with 16-byte payload alignment, splits free blocks, and coalesces adjacent free blocks. Mapped pages are intentionally retained after `kfree` in this bootstrap stage.

The heap is single-core bootstrap infrastructure, not a production allocator.

## IDT and fatal exceptions

BoringKernel owns a 256-entry x86_64 IDT. Vectors 0–31 are CPU exception gates; PIC vectors 32–47 are installed later by the hardware-IRQ layer. Configured entries are DPL0 64-bit interrupt gates using the current kernel code selector and IST 0.

The real acceptance suite continues to prove:

```text
Divide Error → vector 0  → BoringKernel diagnostic → controlled halt
Page Fault   → vector 14 → CR2/error decode       → controlled halt
```

Fatal exceptions do not return. Double Fault still lacks a dedicated IST/emergency stack.

## Complete normalized x86_64 trap frame

The exception and IRQ entry paths now share one **192-byte** normalized ring-0 frame:

```text
offset   field
0x00     RSP copy for C
0x08     SS copy for C
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

The first `RSP`/`SS` pair is a C-facing copy. The tail contains the stack-return state retained for `iretq`. IRQ vector stubs synthesize error code zero and preserve all 15 integer GPRs before entering C.

This frame is also the preemptive task context. An interrupted task's saved frame remains on that task's own stack until the task is resumed.

## Bootstrap PIC/PIT timer

The legacy 8259 PIC is remapped to vectors 32–47. Both PICs begin fully masked; after PIT initialization only IRQ0 is unmasked (`0xfe` master / `0xff` slave).

PIT channel 0 uses input clock 1,193,182 Hz. A requested 100 Hz produces divisor 11932 and approximately 99.998491 Hz, reported as 99998 mHz. The timer tick is advanced only by real IRQ0 delivery.

The initial hardware acceptance still requires at least ten IRQ0 deliveries, proving repeated acknowledgement and `iretq` return rather than accepting a single first interrupt.

## Cooperative kernel contexts

Cooperative switching remains available and deliberately uses a smaller SysV AMD64 call-boundary context:

```text
RSP
RBX
RBP
R12
R13
R14
R15
```

A switch occurs only when kernel code explicitly calls `task_yield()` or when a cooperative task returns and must leave its stack. The existing cooperative QEMU test still proves two independent 16-KiB stacks, alternating execution, task-local state, callee-saved register preservation, clean return, stack cleanup, and timer coexistence.

This cooperative context is **not** used as the arbitrary-interrupt preemption frame.

## Timer-driven preemptive kernel scheduling

BoringKernel now has a separate single-CPU preemptive path for kernel tasks.

Scheduling policy is deliberately minimal:

```text
READY / RUNNING / FINISHED
deterministic fixed-table round robin
1 PIT tick = 1 quantum
no priorities
```

The core path is:

```text
Task A executing normally
    ↓
real PIT IRQ0 / vector 32
    ↓
complete 192-byte frame saved on A's stack
    ↓
timer tick++
    ↓
task_scheduler_tick(A frame)
    ↓
A frame pointer retained by A
    ↓
Task B selected
    ↓
PIC EOI while still on A's IRQ stack
    ↓
IRQ assembly loads B's saved frame into RSP
    ↓
restore all integer GPRs
    ↓
iretq
    ↓
Task B executes
```

A later PIT interrupt can perform the inverse switch and resume A at the exact interrupted instruction stream.

### EOI ordering

The C dispatcher selects the frame first but sends PIC EOI before returning it to assembly. Only assembly then abandons the current IRQ stack. This prevents acknowledgement from depending on eventual return through the preempted task's stack.

### Fresh preemptive tasks

Each new preemptive task receives an independent 16-KiB heap-backed stack plus a synthetic complete IRQ/`iretq` frame on that stack. The frame targets `kernel_task_trampoline`, uses the current kernel CS/SS, has IF set, and supplies a correctly aligned resumed RSP. Fresh and previously interrupted tasks therefore share the same low-level restore path.

### Bootstrap context

Bootstrap remains task ID 0 and keeps its existing stack. When preemption starts, the next real PIT interrupt creates an authentic bootstrap IRQ frame on that stack. The scheduler saves its pointer and starts the first preemptive task. When all ordinary preemptive tasks are `FINISHED`, the saved bootstrap frame is selected and `iretq` resumes normal bootstrap execution.

### Critical sections

Task creation, preemption mode changes, and finished-stack cleanup use short interrupt-disabled regions to keep scheduler metadata coherent. This is sufficient only for the current single-CPU bootstrap.

The timer scheduling path performs no allocation, page mapping, freeing, or serial logging.

## Full integer-register preservation

Preemption preserves:

```text
RAX RBX RCX RDX RSI RDI RBP
R8 R9 R10 R11 R12 R13 R14 R15
RSP RIP CS RFLAGS SS
```

The acceptance suite includes an assembly-assisted probe that keeps caller-saved and callee-saved GPR patterns live while Task A is really preempted, lets Task B run, then verifies the complete integer register set and RSP after A resumes.

There is no FPU/SIMD context switching. Kernel C remains compiled with implicit x87/MMX/SSE/SSE2 generation disabled.

## Verified preemption acceptance

The CPU-bound preemption test tasks contain no `task_yield()` calls. A verified QEMU branch run reported:

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

Both task-local counters/checksums survived repeated preemption, stack addresses remained isolated, the full GPR probe passed, finished tasks were skipped, bootstrap returned safely, both stack sentinels survived, two task stacks were freed, and heap allocation bookkeeping returned to its pre-test value.

Timer ticks, scheduler ticks, and preemptions are tracked separately even though all three happened to equal seven in this run.

## C / assembly boundary

High-level policy remains in C: memory policy, exception diagnostics, PIC/timer dispatch, cooperative selection, preemptive round-robin selection, task state, stack ownership, and acceptance invariants.

Architecture assembly remains isolated to exception/IRQ entry and restore mechanics, `iretq`, minimal cooperative context save/restore, deliberate fault triggers, register probes, and CPU instructions that C cannot express directly.

## Current limitations

BoringKernel still does **not** provide:

- processes or a user process model;
- ring 3;
- separate task/process address spaces or CR3 switching;
- TSS user-stack switching;
- syscalls or user-memory crossing;
- ELF userspace loading;
- FPU/SIMD task state;
- sleeping, blocked I/O, wait queues, or wakeups;
- mutexes, semaphores, or condition variables;
- priorities or realtime scheduling;
- SMP or per-CPU scheduler state;
- LAPIC, IOAPIC, APIC timer, HPET, or ACPI/MADT;
- VFS, RAMFS, BoringFS implementation, or storage drivers;
- input, networking, USB, audio, graphics, or native BoringWM.

The current preemptive scheduler proves kernel-task preemption in one shared ring-0 address space only. Process/address-space work remains a separate future milestone.
