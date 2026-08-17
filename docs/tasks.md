# BoringKernel cooperative kernel tasks

This document describes only the currently implemented **kernel-only cooperative execution-context milestone**. It is deliberately not a process model and not a preemptive scheduler.

## Verified execution path

The normal QEMU acceptance path proves:

```text
bootstrap kernel context
        ↓ task_yield()
kernel task A on stack A
        ↓ task_yield()
kernel task B on stack B
        ↓ task_yield()
kernel task A resumes after its yield
        ↓
...
        ↓ task entries return
FINISHED tasks are skipped
        ↓
bootstrap context resumes
```

The test does not call task A and task B in sequence from a loop. The execution contexts are suspended and resumed by replacing the active stack pointer and restoring the SysV AMD64 callee-saved register state.

## Context ABI

The architecture-specific context is intentionally small:

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

BoringKernel currently uses the SysV AMD64 C calling convention for this boundary.

Saved/restored by `x86_64_context_switch`:

- `RSP`
- `RBX`
- `RBP`
- `R12`
- `R13`
- `R14`
- `R15`

The ordinary SysV caller-saved registers are deliberately **not** part of this cooperative context object: `RAX`, `RCX`, `RDX`, `RSI`, `RDI`, and `R8`–`R11`. A C caller cannot require those values to survive an ordinary function call, so saving them here would falsely turn a small cooperative call-boundary context into a full interrupt/preemption frame.

There is no explicit `RIP` field. A suspended task already has the normal return address for its call to `x86_64_context_switch` on its saved stack. Restoring `RSP` and executing `ret` therefore resumes the C call path naturally.

The kernel does not yet manage FPU/SIMD/vector context. Kernel C is still compiled with implicit x87/MMX/SSE/SSE2 generation disabled.

## Task model

The internal task object contains only the state needed by this milestone:

```text
id
READY / RUNNING / FINISHED
x86_64 kernel context
stack base + stack size
entry function + argument
slot ownership
intended interrupt-enabled state
```

The public API remains small:

```c
bool task_init(void);
bool task_create(void (*entry)(void *), void *arg, uint64_t *task_id);
void task_yield(void);
uint64_t task_current_id(void);
```

Additional inspection/cleanup helpers exist for the kernel acceptance path, but there is no process API, priority API, blocking API, wait queue or timeslice interface.

Task metadata is stored in a fixed table with space for four ordinary kernel tasks. The acceptance test creates exactly two. This is a bounded bootstrap implementation, not a claim of a scalable scheduler.

## Bootstrap context

The already-running kernel is represented as special task ID **0**.

Its existing Limine-provided kernel stack is not copied. The bootstrap context is populated naturally the first time it calls `x86_64_context_switch` to leave the boot stack. When all ordinary test tasks have finished, the last task restores that saved bootstrap context and execution continues immediately after the original `task_yield()` call.

The bootstrap context is not treated as an ordinary round-robin task while runnable kernel tasks remain.

## Per-task stacks

Each created task receives a separate **16 KiB** stack through the existing kernel heap:

```text
kmalloc(16384)
```

The heap therefore remains the ownership source for task stack memory; task metadata itself is static.

Current stack checks include:

- 16-byte-aligned heap base;
- 16-byte-aligned stack top;
- initial saved `RSP` within the allocated range;
- no overlap with another live task stack;
- a 64-bit sentinel `0x424f52494e475354` at the low end of each stack;
- the low 16 bytes kept outside the accepted task-local stack area;
- sentinel revalidation before finished stacks are released.

Guard pages are not implemented.

## First-entry stack construction

A new task has never called the context-switch routine, so it has no natural return address yet. BoringKernel deliberately builds a tiny synthetic first-return frame at the high end of the new stack:

```text
stack_top - 16  → address of kernel_task_trampoline
stack_top - 8   → padding/reserved zero
```

The saved context starts with `RSP = stack_top - 16`.

The first `x86_64_context_switch` loads that `RSP` and executes `ret`, which transfers control to `kernel_task_trampoline`. After the `ret`, the trampoline begins with the SysV AMD64 function-entry alignment expected by C.

The trampoline restores the task's intended interrupt state, calls:

```c
task->entry(task->arg);
```

and handles ordinary function return automatically.

## Cooperative task completion

When a task entry function returns, the trampoline marks the current task:

```text
RUNNING → FINISHED
```

The selector never chooses a `FINISHED` task again. It switches to another `READY` task if one exists; otherwise it restores the saved bootstrap context. A task function therefore does not need a special exit call and does not fall through into garbage stack data.

Finished task stacks are not freed while they are active. Once execution is safely back on the bootstrap stack, the acceptance path validates their sentinels and frees both stacks with `kfree`.

Because the existing bootstrap heap intentionally retains mapped pages after `kfree`, cleanup requires heap allocation count and used bytes to return to their prior values; it does not falsely require mapped heap pages to shrink.

## Cooperative selection

The selector is a deterministic fixed-table round robin over ordinary tasks in `READY` state.

A running task changes to `READY` only when it explicitly calls `task_yield()`. No timer tick, IRQ dispatcher or unrelated kernel path invokes the selector.

With the two-task acceptance sequence, the observed execution order is equivalent to:

```text
A1
B1
A2
B2
A3
B3
```

The third iteration of each task returns normally instead of yielding. The observed context-switch count is therefore seven:

```text
bootstrap → A
A → B
B → A
A → B
B → A
A finishes → B
B finishes → bootstrap
```

## Interrupt-state policy

The existing PIT continues to interrupt ordinary task execution, but it does **not** schedule tasks.

Immediately before the small selector/context-switch critical section, `task_yield()` records whether the current context intended interrupts to be enabled and then executes `cli`. Task-state mutation, current-task replacement and the actual stack/register switch therefore cannot be interrupted halfway through.

After a suspended context resumes, or when a new task first enters its trampoline, BoringKernel restores that context's intended interrupt-enabled state. In the current acceptance path the tasks inherit the already-enabled hardware-timer state, so PIT IRQ0 continues to run while task C code executes normally.

The timer path itself remains exactly conceptually:

```text
IRQ0
→ timer tick++
→ PIC EOI
→ register restore
→ iretq
```

There is no call from IRQ0 to `task_yield`, the task selector or `x86_64_context_switch`.

## Acceptance proof

The verified QEMU run reported:

```text
Kernel tasks:
Mode: cooperative
Tasks created: 2
Task stack size: 16384 bytes
Bootstrap task ID: 0
Scheduler: online

Task A:
  iterations: 3
  local-state: PASS

Task B:
  iterations: 3
  local-state: PASS

Context switch self-test:
  task-a-start: PASS
  task-b-start: PASS
  alternating-switch: PASS
  stack-isolation: PASS
  register-state: PASS
  task-return: PASS
  timer-coexistence: PASS
  stack-cleanup: PASS
  heap-bookkeeping: PASS
Context switches: 7
Ticks before task test: 10
Ticks after task test: 16
Task stacks freed: 2
Task heap allocations after cleanup: 0

BoringKernel cooperative task test passed.
```

Each task keeps a `volatile uint64_t` local counter on its own stack, checks the same local address after resuming, and reaches three iterations. The two local addresses must differ and must each lie within the currently active task's allocated stack.

The register-preservation probe loads distinct known values into `RBX`, `RBP`, and `R12`–`R15`, yields through a real context switch, then verifies those values and `RSP` after the task resumes. The acceptance output prints `register-state: PASS` only after those checks succeed.

Timer coexistence is not inferred from a static configuration. Tasks wait for actual PIT progress while executing, and the overall test additionally requires `Ticks after task test > Ticks before task test`. The verified capture progressed from **10 to 16** ticks.

## Current limitations

This milestone proves **cooperative kernel execution-context switching only**.

It does not implement:

- timer-driven task switching or preemption;
- timeslices;
- interrupt-frame-to-task-frame conversion;
- user processes or ring 3;
- per-process address spaces;
- TSS expansion or per-task privilege stacks;
- FPU/SIMD state switching;
- sleeping, blocking, wait queues or task wakeups;
- mutexes, semaphores or other synchronization primitives;
- priorities or fairness accounting;
- SMP, per-CPU current-task state or run queues;
- guard pages or automatic stack growth.

The exact next execution-model blocker, if separately requested, is **timer-driven preemption**: deliberately connecting a timer interrupt to safe scheduling/context-switch semantics without weakening the cooperative/context and exception invariants proven here.
