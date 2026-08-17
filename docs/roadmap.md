# BoringOS roadmap — boot kernel to native shell and BoringFS

This roadmap tracks the **actual repository state**. Planned work is not implemented work.

## Current verified state

```text
QEMU x86_64
    ↓
Limine
    ↓
BoringKernel ELF entry
    ↓
COM1 serial output
    ↓
PMM
    ↓
selected 4-KiB VMM mappings
    ↓
bounded kernel heap
    ↓
x86_64 IDT + fatal CPU exception diagnostics
    ↓
legacy PIC + PIT IRQ0
    ↓
cooperative kernel contexts
    ↓
real timer-driven preemptive kernel scheduling
```

BoringKernel currently runs only in ring 0. It owns a bounded PMM, selected VMM mappings in the inherited Limine address space, a bounded kernel heap, its own x86_64 IDT, real Divide Error and Page Fault diagnostics, a real PIT/PIC timer path, cooperative kernel contexts, and a verified timer-driven preemptive scheduler for independent kernel tasks.

The current preemptive path uses a complete 192-byte x86_64 interrupt frame retained on each task's own 16-KiB stack, deterministic one-tick round robin, EOI-before-frame-restore ordering, and `iretq` to resume either a fresh or previously preempted task. The merged-main acceptance test proves repeated hardware-triggered preemption without `task_yield()` in either CPU-bound stress task.

The accepted merged-main proof observed:

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

Task-local stack state, full integer GPR state, stack sentinels, finished-task skipping, bootstrap return, stack cleanup, heap bookkeeping, the cooperative scheduler regression, real Divide Error and real Page Fault tests all remained green.

For the deliberately legacy bootstrap timer proof, QEMU remains `q35` with one `qemu64,apic=off` CPU. No LAPIC/IOAPIC configuration is implemented. The active page-table root and boot-critical mappings remain inherited from Limine.

There is still **no process model, separate per-process address space, ring 3, syscall layer, userspace loader, VFS, RAMFS, block-device stack, BoringFS implementation, init, or shell**.

Every milestone must keep all earlier acceptance checks green and add a focused proof for the new capability.

---

# Stage 1 — memory foundations

## Milestone 1: physical memory manager — COMPLETE

Implemented and accepted in QEMU:

- consume and defensively validate Limine's memory map;
- accept only explicitly usable memory;
- normalize usable ranges to 4096-byte frame boundaries;
- track frames with bounded static metadata;
- allocate unique frames deterministically;
- free/reuse frames;
- reject invalid/double frees;
- verify alignment, uniqueness, usable membership and bookkeeping.

## Milestone 2: selected BoringKernel virtual-memory control — COMPLETE

Implemented and accepted in QEMU:

- require x86_64 four-level paging;
- discover Limine's HHDM;
- adopt the active PML4 from `CR3` without replacing it;
- walk PML4 → PDPT → PD → PT;
- allocate missing page-table frames from PMM;
- create and remove selected 4-KiB mappings;
- translate mappings;
- perform real write/read through a new mapping;
- invalidate with `invlpg`;
- reclaim only VMM-owned empty tables;
- restore PMM/VMM bookkeeping after the self-test.

This is selected mapping control, not complete address-space ownership.

## Milestone 3: kernel heap — COMPLETE

Implemented and accepted in QEMU:

- finite 16-MiB heap virtual range;
- PMM/VMM-backed page growth;
- deterministic first-fit allocation;
- 16-byte payload alignment;
- splitting and immediate coalescing;
- invalid/interior/double-free rejection;
- real write/read and growth tests;
- deterministic reuse;
- final allocation bookkeeping restoration.

Mapped heap pages remain mapped after `kfree` by current bootstrap policy.

---

# Stage 2 — controlled faults, interrupts and time

## Milestone 4: exception infrastructure — COMPLETE

Implemented and accepted in QEMU:

- BoringKernel-owned 256-entry x86_64 IDT;
- exception vectors 0–31;
- DPL0 interrupt gates;
- validated `lidt` / `sidt` state;
- normalized trap-frame handling;
- fatal diagnostics in C;
- real Divide Error on vector 0;
- real Page Fault on vector 14 with `CR2` reporting;
- controlled `cli`/`hlt` fatal stop.

Double Fault still has no dedicated IST/emergency stack.

## Milestone 5: hardware interrupt controller and periodic timer — COMPLETE

Implemented and accepted in QEMU:

- legacy 8259 PIC remapped to vectors 32–47;
- all hardware IRQs initially masked;
- PIT channel 0 programmed from 1,193,182 Hz;
- requested 100 Hz, divisor 11932, approximately 99.998491 Hz;
- only IRQ0 unmasked;
- repeated real IRQ0 delivery;
- correct PIC acknowledgement/EOI path;
- ordinary IRQ return through `iretq`;
- no allocation or per-tick serial logging in IRQ0;
- at least ten timer deliveries required by acceptance.

The current reference uses `-M q35 -cpu qemu64,apic=off` so this remains deliberately PIC/PIT-only bootstrap infrastructure.

---

# Stage 3 — execution and process foundations

## Milestone 6: kernel execution contexts + cooperative context switching — COMPLETE

Implemented, merged and accepted on `main`:

- bootstrap task ID 0 without copying its active stack;
- `READY`, `RUNNING`, `FINISHED` task states;
- bounded fixed task table;
- independent 16-KiB heap-backed task stacks;
- stack overlap/sentinel validation;
- SysV AMD64 cooperative context containing `RSP`, `RBX`, `RBP`, `R12`–`R15`;
- isolated assembly save/restore boundary;
- fresh-task trampoline entry;
- deterministic cooperative round robin;
- explicit `task_yield()`;
- normal task return to `FINISHED`;
- clean bootstrap restoration;
- task-local state and callee-saved register preservation proof;
- PIT coexistence while cooperative tasks execute;
- finished stack cleanup and heap bookkeeping restoration.

The accepted cooperative proof observed seven real cooperative context switches between two tasks.

## Milestone 7: timer-driven preemptive kernel scheduling — COMPLETE

Implemented, merged and accepted on `main` in QEMU:

- retain the existing cooperative call-boundary context as a separate mechanism;
- use a complete **192-byte x86_64 interrupt frame** for arbitrary-instruction preemption;
- preserve all integer GPRs plus `RIP`, `CS`, `RFLAGS`, `RSP` and `SS` required by the current ring-0 restore path;
- retain an interrupted task's frame on that task's own 16-KiB stack;
- let C scheduling policy select the frame that IRQ assembly restores;
- acknowledge the PIC **before** assembly abandons the interrupted IRQ stack;
- restore the selected context through full GPR restore + `iretq`;
- use deterministic fixed-table round robin;
- use **one PIT tick per scheduling quantum**;
- prepare never-run preemptive tasks with a complete synthetic restore/`iretq` frame on their own stack;
- preserve correct SysV AMD64 task-trampoline stack alignment;
- save the bootstrap kernel's genuine PIT interrupt frame and later restore it rather than copying/recreating the boot stack;
- use short IF-off critical sections around task/scheduler metadata mutation and stack destruction;
- perform no allocation, freeing, PMM/VMM work or serial logging in the timer scheduling path;
- keep normal task-entry return as `FINISHED` and never reschedule finished tasks;
- clean up finished stacks only after safely returning to bootstrap;
- add two CPU-bound preemption stress tasks containing no `task_yield()` calls;
- prove both tasks make repeated progress only because of real PIT preemption;
- prove task-local counters/checksums survive repeated interrupt-time suspension/resumption;
- prove stack isolation and sentinel integrity;
- prove full integer GPR preservation with an assembly-assisted live-register probe;
- track timer ticks, scheduler ticks and actual preemptions separately;
- retain and pass the cooperative scheduler, PMM, VMM, heap, IRQ/EOI, Divide Error and Page Fault regression suites.

The merged-main acceptance proof reported:

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
```

This milestone proves that BoringKernel can preempt one running **kernel task** with a real PIT hardware interrupt, schedule another independent kernel task, and later resume the original task at its interrupted execution point.

It does **not** prove processes, userspace multitasking, separate address spaces, SMP scheduling, or modern APIC timer support.

## Milestone 8: process and address-space model — NEXT

Define the smallest real process object and independent address-space ownership model.

A future implementation must explicitly address at least:

- process identity and lifetime;
- a BoringKernel-owned address-space root rather than treating all tasks as one shared ring-0 address space;
- mappings/resources owned by one process;
- address-space creation and destruction;
- safe switching of the active address space where required;
- clean reclamation on process exit;
- preserving the already verified kernel task/interrupt invariants.

This milestone has **not** been started and requires a separate instruction.

It must not silently expand into ring 3, syscalls, ELF userspace, VFS, storage, or networking unless those later milestones are separately requested.

## Milestone 9: ring 3 transition

Enter real x86_64 CPL 3 code and prove user code cannot directly perform privileged kernel operations.

## Milestone 10: syscall mechanism and safe user-memory crossing

Create the first controlled userspace/kernel API with checked arguments, bounded user copies and explicit error returns. Do not declare a stable public ABI prematurely.

---

# Stage 4 — load and run native BoringOS programs

## Milestone 11: ELF userspace loader

Load a deliberately constrained x86_64 userspace ELF with strict validation of class, machine, segments, ranges and permissions.

The first executable may arrive as a boot module rather than pretending a filesystem exists.

## Milestone 12: minimal native userspace runtime

Provide BoringOS-owned C entry glue, syscall wrappers and tiny string/memory helpers. No host libc.

## Milestone 13: serial console / TTY path for userspace

Expose real console input/output through the kernel API so early native programs can use the QEMU serial terminal.

---

# Stage 5 — VFS and RAMFS

## Milestone 14: VFS core

Introduce filesystem-independent kernel objects, path walking, mount relationships, open handles and process working directories.

Initial backend operations should remain close to:

```text
lookup
create
mkdir
unlink
rmdir
rename
read
write
truncate
readdir
```

## Milestone 15: RAMFS backend

Implement a tiny real in-memory filesystem supporting nested directories and ordinary file operations through the VFS boundary.

RAMFS should precede persistent BoringFS so path/syscall semantics are proven independently from block I/O and on-disk corruption handling.

---

# Stage 6 — native init and shell

## Milestone 16: `boring-init`

Run a real first userspace process:

```text
BoringKernel
    ↓
boring-init
```

## Milestone 17: `boring-shell`

Launch a native C shell from `boring-init` in userspace. Filesystem commands must operate through real VFS/syscall state; no simulated directory listings.

---

# Stage 7 — BoringFS host tooling before kernel writes

## Milestone 18: pure BoringFS format codec and validator

Implement the documented BoringFS v0 encoding/decoding rules in small C modules suitable for host tests and later kernel reuse where practical.

## Milestone 19: `mkboringfs`

Create deterministic valid BoringFS images containing an empty root directory.

## Milestone 20: `boringfsck`

Create a read-only structural validator/inspector. Initial versions report corruption and return non-zero; they do not attempt aggressive repair.

---

# Stage 8 — persistent block I/O

## Milestone 21: generic block-device layer

Create a filesystem-independent interface conceptually providing:

```text
logical_block_size
logical_block_count
writable
read(lba, count, destination)
write(lba, count, source)
flush()
```

## Milestone 22: QEMU VirtIO-block driver

Use VirtIO block as the first QEMU storage target unless implementation analysis finds a concrete blocker. Prove bounded capacity, read, disposable-image write and read-back persistence.

---

# Stage 9 — BoringFS kernel integration

## Milestone 23: read-only BoringFS mount

Mount a host-formatted BoringFS volume through the generic block-device boundary and VFS. Reject invalid metadata before exposing files.

## Milestone 24: BoringFS mutation support

Add create, mkdir, write, truncate, unlink, rmdir, rename/move and block allocation/free only after read-only mounting is solid.

## Milestone 25: persistent native root filesystem

Boot with BoringFS as the persistent root and launch native userspace from it:

```text
BoringKernel
    ↓
block device
    ↓
BoringFS root
    ↓
boring-init
    ↓
boring-shell
```

---

# Security and corruption gates

Before persistent BoringFS writes are enabled, BoringOS must already have real privilege separation, separate address spaces, checked syscalls, validated user-memory copies, bounded kernel allocation, block-device bounds checks, BoringFS structural validation, host corruption tests and disposable QEMU integration tests.

Networking remains unrelated and deferred.

---

# Exact next implementation milestone

## Process and address-space model

The next roadmap item is **Milestone 8**.

It has **not** been started and requires a separate instruction.

Do not jump ahead to:

- ring 3;
- syscalls;
- userspace loading;
- VFS/RAMFS;
- BoringFS implementation;
- storage drivers;
- networking.
