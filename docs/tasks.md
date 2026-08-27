# BoringKernel kernel tasks and scheduling

This document describes the implemented **kernel-only execution-context layer**. BoringKernel supports explicit cooperative switching, real PIT-timer-driven preemption, and now task ownership by explicit process/address-space objects. All task execution is still CPL0.

A task is not a process:

```text
task
→ execution context / scheduling entity

process
→ identity + address-space ownership
```

A process may eventually own multiple tasks. The 0.0.9-dev acceptance needs only one preemptive task per test process and does not introduce a thread abstraction.

## Two context boundaries

BoringKernel keeps the two switching mechanisms separate:

```text
cooperative context
→ normal SysV AMD64 C call boundary

preemptive context
→ arbitrary x86_64 hardware-interrupt boundary
```

The cooperative object preserves only `RSP`, `RBX`, `RBP`, and `R12`-`R15`. Timer preemption must preserve the complete integer state needed to resume an arbitrary interrupted instruction stream.

## Preemptive interrupt context

Timer preemption uses the normalized x86_64 trap frame stored on the interrupted task's own stack. The complete current structure remains **192 bytes**:

```text
offset   field
0x00     RSP copy for C inspection
0x08     SS copy for C inspection
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

The IRQ stub preserves all 15 general-purpose registers plus the interrupt-return state. A task's `preempt_frame` points at the saved frame on that task's stack until the task is selected again.

## Task model

M35 raises the bounded task table to eight ordinary kernel tasks, separate
from the bootstrap task, so display, WM and three apps can coexist. States remain:

```text
READY
RUNNING
BLOCKED
FINISHED
```

Each task records, as applicable:

```text
id
owning process pointer
state
cooperative context
preemptive trap-frame pointer
stack base + stack size
entry function + argument
preemptive slice count
context kind
slot ownership
intended interrupt state
```

The bootstrap task is task ID 0 and belongs to the bootstrap process PID 0.

The legacy helpers `task_create()` and `task_create_preemptive()` create PID-0 tasks, preserving the existing cooperative and preemption regression behavior. The process milestone adds a narrow `task_create_preemptive_for_process()` path for binding a preemptive task to an explicit live process.

There are no priorities, blocked/sleeping states, wait queues, realtime classes, fairness framework, thread groups, or per-CPU run queues.

## Per-task stacks

Every ordinary task receives a separate **16 KiB** stack from the shared higher-half kernel heap:

```text
kmalloc(16384)
```

Because current process roots share the kernel higher half, these stacks remain mapped while PID 0, PID 1, or PID 2 is active.

Existing checks remain:

- 16-byte-aligned stack allocation and top;
- no overlap among live task stacks;
- low-end sentinel `0x424f52494e475354`;
- low 16 bytes excluded from accepted task-local storage;
- saved cooperative `RSP` inside its stack;
- saved preemptive frame and resumed `RSP` inside its stack;
- sentinel revalidation before finished stacks are freed.

There are no guard pages or automatic stack growth.

## Fresh task entry

A new cooperative task still receives a synthetic normal return frame targeting `kernel_task_trampoline()`.

A never-run preemptive task receives a synthetic complete 192-byte restore frame containing zeroed GPRs, vector 32, error code 0, the task trampoline RIP, current kernel CS/SS, IF-enabled RFLAGS, and a correctly aligned resumed RSP. Fresh and already-preempted tasks therefore share the same IRQ restore/`iretq` path.

The trampoline verifies that the current task owns a live process and that the globally active process matches `task->process` before calling the task entry function.

## Bootstrap task and process

The already-running kernel remains task ID 0 in process PID 0. Its boot stack and inherited root page table are never copied.

For preemption, `task_preemption_start()` leaves bootstrap executing until the next real PIT IRQ0 creates an authentic interrupt frame on the bootstrap stack. The scheduler saves that frame and may select another task.

When all preemptive tasks finish, the scheduler selects the saved bootstrap frame and activates PID 0's bootstrap address space before restoration if a different process root is currently active. `iretq` then resumes the exact interrupted bootstrap instruction stream.

## Cooperative scheduling

Cooperative selection remains deterministic fixed-table round robin. A RUNNING task becomes READY only when it explicitly calls `task_yield()`.

Before switching to a selected cooperative task, the scheduler activates the selected task's owning process. The existing cooperative acceptance tasks all belong to PID 0, so this normally leaves CR3 unchanged.

The accepted sequence remains:

```text
bootstrap → A → B → A → B → A → B → bootstrap
```

with seven cooperative context switches.

## Timer-driven preemptive scheduling

Preemptive scheduling remains deterministic fixed-table round robin with:

```text
1 PIT tick = 1 scheduling quantum
```

The PIT request remains 100 Hz with divisor 11932, approximately 99.998491 Hz. This is not a realtime timing guarantee.

The 0.0.9-dev IRQ0 scheduling path is now:

```text
real PIT IRQ0
    ↓
full interrupt state on current task stack
    ↓
timer tick++
    ↓
scheduler tick++
    ↓
save current task's frame pointer
    ↓
choose next READY preemptive task
    ↓
process_activate(next->process)
    ↓
load next process CR3 if root differs
    ↓
PIC EOI while still on the current shared kernel IRQ stack
    ↓
return selected frame pointer to assembly
    ↓
RSP = selected frame
    ↓
restore all GPRs
    ↓
iretq
```

The scheduler never calls `task_yield()` from IRQ0 and never uses the cooperative call-boundary object for arbitrary interrupt-time state.

## Address-space switch ordering

The scheduler changes CR3 before returning the selected frame to `irq.c`. This is safe for the current bootstrap model because kernel code, IRQ stack, scheduler metadata and task stacks all live in higher-half mappings shared identically by every current process root.

`irq.c` then sends PIC EOI while still executing on the current IRQ stack. Only after EOI does `irq_stubs.S` replace `RSP` with the selected task frame.

This preserves the established EOI-before-stack-switch invariant while allowing a different process root to already be active.

If a selected task belongs to the already active process, `address_space_activate()` avoids an unnecessary CR3 reload.

## Critical sections

The current single-CPU bootstrap uses short interrupt-disabled regions around task/process metadata changes, process activation bookkeeping, preemption start/stop, and finished-stack/process cleanup.

There is no general synchronization framework and no SMP locking.

The timer scheduling path performs no `kmalloc`, `kfree`, PMM allocation, page-table allocation, VMM mapping, or serial output. Process roots, mappings, tasks and stacks exist before they become schedulable. A scheduler CR3 load is the only new address-space operation in the hot path.

## Task completion

Returning from a task entry function marks the **task** `FINISHED`; it does not automatically destroy or finish its owning process.

A preemptive finished task waits for the next real timer IRQ. The scheduler never makes it READY again and chooses another READY task or the saved bootstrap frame. Stacks are freed only later from bootstrap.

For the process acceptance, the two process objects remain `ALIVE` until bootstrap has safely returned and both finished task stacks have been cleaned. Only then are PID 1 and PID 2 marked `FINISHED`, their private mappings removed, and their inactive address spaces destroyed.

## Existing full-GPR preemption proof

The independent 0.0.8-style regression remains in 0.0.9-dev. An assembly-assisted probe keeps known values live in:

```text
RAX RBX RCX RDX RSI RDI RBP
R8 R9 R10 R11 R12 R13 R14 R15
```

plus `RSP`, lets a real PIT interrupt preempt Task A, runs Task B, then verifies every value after A resumes.

The accepted regression still reports:

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

Kernel C remains compiled with implicit x87/MMX/SSE/SSE2 generation disabled. There is no FPU/SIMD task-state switching.

## Process-owned preemption proof

The 0.0.9-dev acceptance creates:

```text
Task A → PID 1 → root A
Task B → PID 2 → root B
```

Both are CPL0 CPU-bound tasks and neither calls `task_yield()`.

Each repeatedly dereferences the same lower-half virtual address:

```text
0x0000004000000000
```

Task A must always see/write `0xAAAAAAAAAAAAAAAA`. Task B must always see/write `0xBBBBBBBBBBBBBBBB`.

A verified clean-source QEMU run reported:

```text
Preemptive CR3 switches: 7
Process A slices:         3
Process B slices:         3
```

The test additionally verifies current task PID against the active process object, stack isolation, local progress, zero cooperative yields, real scheduler preemption, return to bootstrap PID 0/root, finished-stack cleanup, process address-space cleanup, and PMM bookkeeping.

See [`processes.md`](processes.md) for root ownership and same-VA/different-PA details.

## Current limitations

The task scheduler now proves **single-CPU preemption across independent process address spaces**, but execution remains entirely CPL0.

It does not implement:

- ring 3 or untrusted user execution;
- user CS/SS or TSS privilege-stack switching;
- syscalls or user-memory crossing;
- ELF userspace loading;
- a general threads-within-process model;
- FPU/SIMD state switching;
- sleeping, blocking, wait queues, or wakeups;
- mutexes, semaphores, condition variables, or other synchronization primitives;
- priorities or realtime scheduling;
- SMP or per-CPU scheduler/process state;
- PCID;
- LAPIC, IOAPIC, APIC timer, HPET, or ACPI/MADT;
- guard pages or automatic task-stack growth.

BoringKernel 0.0.9-dev has process identities and separate CR3 roots, but these are exercised only by trusted kernel-mode tasks. A real Ring 3 transition is a separate future milestone.
