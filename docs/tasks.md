# BoringKernel kernel tasks and scheduling

This document describes the currently implemented **kernel-only execution-context layer**. BoringKernel now supports both explicit cooperative switching and real PIT-timer-driven preemption between independent ring-0 kernel tasks. This is still not a process model and not userspace multitasking.

## Two deliberately different context boundaries

BoringKernel keeps the two mechanisms conceptually separate:

```text
cooperative context
→ normal SysV AMD64 C call boundary

preemptive context
→ arbitrary x86_64 hardware-interrupt boundary
```

A cooperative switch only has to preserve the state a normal C caller may require to survive a call. A timer interrupt may arrive at any instruction, so preemption must preserve the complete integer register and interrupt-return state required to resume that exact instruction stream.

## Cooperative context

The cooperative architecture context remains intentionally small:

```c
struct x86_64_kernel_context {
    uint64_t rsp;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
};
```

`x86_64_context_switch()` saves/restores `RSP`, `RBX`, `RBP`, and `R12`–`R15`, then returns through the selected task stack. There is no explicit cooperative `RIP`: the ordinary return address already lives on that saved stack.

The SysV caller-saved registers (`RAX`, `RCX`, `RDX`, `RSI`, `RDI`, `R8`–`R11`) deliberately remain outside this cooperative object.

## Preemptive interrupt context

Timer preemption uses the normalized x86_64 trap frame stored on the interrupted task's own stack. The complete current structure is **192 bytes**:

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

The IRQ stub preserves all 15 general-purpose registers:

```text
RAX RBX RCX RDX RBP RDI RSI
R8 R9 R10 R11 R12 R13 R14 R15
```

and the interrupt-return state contains `RIP`, `CS`, `RFLAGS`, `RSP`, and `SS`. Hardware IRQs have no CPU error code, so the vector stub supplies normalized `error_code = 0` plus the vector number before the common GPR save.

The first two fields are convenient C-facing copies of the interrupted `RSP` and `SS`. The original hardware return words remain at the tail of the frame and are consumed by `iretq`.

This full frame is the state that belongs to a preempted task. It is not copied into a global scratch frame. `kernel_task.preempt_frame` points at the frame residing on that task's own stack until that task is selected again.

## Task model

The fixed bootstrap task table still supports at most four ordinary kernel tasks. The states remain:

```text
READY
RUNNING
FINISHED
```

Each task records, as applicable:

```text
id
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

There are no priorities, blocked/sleeping states, wait queues, realtime classes, fairness framework, or per-CPU run queues.

## Per-task stacks

Every created ordinary task receives a separate **16 KiB** stack from the existing kernel heap:

```text
kmalloc(16384)
```

Task metadata is static; stack memory is heap-owned. Current checks include:

- 16-byte-aligned stack allocation and top;
- no overlap among live task stacks;
- a low-end sentinel `0x424f52494e475354`;
- the low 16 bytes excluded from accepted task-local storage;
- cooperative saved `RSP` inside its stack;
- preemptive saved frame and resumed `RSP` inside its stack;
- sentinel revalidation before finished stacks are freed.

There are no guard pages or automatic stack growth yet.

## Fresh cooperative task entry

A new cooperative task receives a small synthetic return frame at the top of its stack. Restoring that task through `x86_64_context_switch()` executes `ret` into `kernel_task_trampoline()`, which then calls the task entry function with normal SysV AMD64 stack alignment.

## Fresh preemptive task entry

A never-run preemptive task has no previous timer frame, so BoringKernel constructs a valid **synthetic 192-byte restore frame** on that task's own stack.

The fresh frame contains:

```text
all GPRs                 = 0
vector                   = 32
error code               = 0
RIP                      = kernel_task_trampoline
CS                       = current kernel code selector
RFLAGS                   = reserved bit + IF
hardware RSP             = stack_top - 8
hardware SS              = current kernel SS
C-facing RSP / SS copies = same RSP / SS values
```

A defensive return address to `x86_64_halt_forever` is placed at the resumed stack pointer. The synthetic `hardware RSP = stack_top - 8` gives `kernel_task_trampoline()` the SysV AMD64 function-entry alignment expected after `iretq`.

Fresh and already-preempted tasks therefore use the same IRQ restore/`iretq` mechanics once selected.

## Bootstrap context

The already-running kernel remains special task ID **0**. Its existing boot stack is never copied or recreated.

For cooperative switching, the bootstrap call-boundary context is saved naturally on the first explicit `task_yield()`.

For preemption, `task_preemption_start()` leaves bootstrap running until the next real PIT IRQ0. That interrupt creates a genuine IRQ frame on the bootstrap stack. `task_scheduler_tick()` keeps a pointer to that frame, selects the first READY preemptive task, and assembly restores that task's frame instead.

When all preemptive test tasks have become `FINISHED`, the scheduler selects the saved bootstrap IRQ frame. `iretq` therefore resumes the exact interrupted bootstrap instruction stream. Only after that return does bootstrap disable preemptive scheduling, validate finished stacks, and free them.

## Cooperative scheduling

Cooperative selection remains deterministic fixed-table round robin. A RUNNING cooperative task becomes READY only when it explicitly calls `task_yield()`.

The existing acceptance sequence remains:

```text
bootstrap → A → B → A → B → A → B → bootstrap
```

with seven cooperative context switches and normal task-function return marking tasks `FINISHED`.

## Timer-driven preemptive scheduling

Preemptive scheduling is also deterministic fixed-table round robin, but selection begins only from real PIT IRQ0 delivery while preemption is enabled.

The current quantum is deliberately simple:

```text
1 PIT tick = 1 scheduling quantum
```

The PIT request remains 100 Hz with divisor 11932, approximately 99.998491 Hz, so the nominal quantum is roughly 10 ms. This is not a precise realtime timing guarantee.

Conceptually the IRQ0 scheduling path is:

```text
real PIT IRQ0
    ↓
full interrupt state on current stack
    ↓
timer tick++
    ↓
scheduler tick++
    ↓
save current task's frame pointer
    ↓
choose next READY preemptive task
    ↓
PIC EOI on the current IRQ stack
    ↓
return selected frame pointer to assembly
    ↓
RSP = selected frame
    ↓
restore all GPRs
    ↓
iretq
```

The scheduler never calls `task_yield()` from IRQ0 and does not use the cooperative call-boundary context for arbitrary interrupt-time state.

## EOI ordering

The C IRQ dispatcher asks `task_scheduler_tick()` which complete frame should be restored, but **the stack has not changed yet**. The PIC is acknowledged before the C dispatcher returns the selected frame pointer to assembly.

Only after EOI does `irq_stubs.S` replace `RSP` with the selected frame and execute the restore/`iretq` path. Therefore interrupt acknowledgement never depends on later returning to the abandoned task's C stack.

## Critical sections and preemption control

This milestone uses the smallest mechanism required for the single-CPU bootstrap:

- task creation disables maskable interrupts while task-table and stack ownership are changing;
- preemption start/stop disables interrupts while scheduler mode/current-task state is changed;
- finished-stack validation and destruction run with interrupts disabled and only from bootstrap;
- the IRQ scheduler itself runs with IF already cleared by the interrupt-gate entry semantics.

There is no general synchronization framework and no SMP locking. Timer ticks continue normally outside these short IF-off regions.

No `kmalloc`, `kfree`, PMM allocation, VMM mapping, or serial output occurs in the timer scheduling path. Tasks and stacks already exist before they become schedulable.

## Task completion

Returning from either task entry function still marks the task `FINISHED`.

A cooperative finished task switches away through the existing cooperative path. A preemptive finished task waits with `sti; hlt` for the next real timer IRQ; that IRQ sees it as `FINISHED`, never makes it READY again, and selects another READY task or the saved bootstrap frame.

The acceptance test records any impossible resume of a finished preemptive task and fails if that count is non-zero.

Stacks are never freed while executing on them. Cleanup occurs later from bootstrap.

## Full-GPR preemption probe

The preemption acceptance path includes an isolated assembly-assisted probe specifically because callee-saved cooperative testing is not enough for arbitrary interrupt-time preemption.

Task A loads known values into:

```text
RAX RBX RCX RDX RSI RDI RBP
R8 R9 R10 R11 R12 R13 R14 R15
```

and records `RSP`. It then spins using only RIP-relative memory operations while those register values stay live. The probe marks itself armed only after all patterns are loaded.

Task B cannot release the probe until the real PIT scheduler has preempted A and run B. When A is later selected again by another real PIT interrupt, the probe verifies every listed GPR plus `RSP` before returning PASS.

Kernel C is still built with implicit x87/MMX/SSE/SSE2 generation disabled. There is no FXSAVE, XSAVE, lazy FPU switching, or SIMD task context in this milestone.

## Preemption acceptance proof

The verified QEMU branch run uses two CPU-bound tasks whose task bodies contain **no call to `task_yield()`**. Each keeps a `volatile uint64_t` counter and checksum on its own stack and repeatedly validates the addresses and prior state after resumption.

The verified run reported:

```text
Preemptive scheduler:
Policy: round-robin
Timer source: PIT IRQ0
Timer vector: 32
Quantum: 1 tick
Preemption: enabled during test

Preemption self-test:
  task-a-progress: PASS
  task-b-progress: PASS
  no-cooperative-yield: PASS
  repeated-preemption: PASS
  stack-isolation: PASS
  local-state: PASS
  register-state: PASS
  timer-delivery: PASS
  bootstrap-return: PASS
  finished-task-skip: PASS
  stack-sentinel: PASS
  stack-cleanup: PASS
  heap-bookkeeping: PASS

Timer ticks during test: 7
Scheduler ticks: 7
Preemptions: 7
Task A slices: 3
Task B slices: 3
Task A resumes: 2
Task B resumes: 2
Cooperative yields during test: 0
Task stacks freed: 2
Task heap allocations after preemption cleanup: 0

BoringKernel preemptive scheduling test passed.
```

The counters are deliberately separate concepts:

- **timer ticks**: PIT time-source progress;
- **scheduler ticks**: IRQ0 entries observed while preemption mode is enabled;
- **preemptions**: actual restore-frame changes to a different execution context.

They happened to all equal seven in this acceptance run; the implementation does not define them as permanently identical.

The existing cooperative test also remains green, including its independent task stacks, seven cooperative switches, callee-saved register probe, cleanup, and timer coexistence.

## Current limitations

This milestone proves **single-address-space, single-CPU kernel-task preemption only**.

It does not implement:

- user processes or ring 3;
- per-process address spaces or CR3 switching;
- TSS user-stack switching;
- syscalls or user-memory crossing;
- ELF userspace loading;
- FPU/SIMD state switching;
- sleeping, blocking, wait queues, or wakeups;
- mutexes, semaphores, condition variables, or other synchronization primitives;
- priorities or realtime scheduling;
- SMP or per-CPU scheduler state;
- LAPIC, IOAPIC, APIC timer, HPET, or ACPI/MADT;
- guard pages or automatic task-stack growth.

All current tasks execute in CPL0 and share the same inherited kernel address space. The next execution-model work must be separately scoped; this milestone does not start a process or userspace model.
