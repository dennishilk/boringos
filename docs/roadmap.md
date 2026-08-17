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
selected bootstrap VMM mappings
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
    ↓
process identity + independent x86_64 address spaces
    ↓
scheduler-integrated CR3 switching
```

BoringKernel **0.0.9-dev** still executes only in CPL0, but it now has a real distinction between tasks and processes. A task is an execution/scheduling entity; a process is an identity plus address-space owner.

PID 0 represents the inherited bootstrap/kernel address space. New process roots are allocated from PMM. PML4 slots 0–255 are process-private in the current bootstrap model, while slots 256–511 are shared from the bootstrap root so kernel code/data, HHDM, heap, task stacks, interrupt code and scheduler state remain available in every current process address space.

The process layer owns only page-table frames allocated specifically for that process. It never frees the inherited bootstrap/Limine root, shared higher-half tables, VMM-owned shared tables, or page tables belonging to another process.

The merged-main acceptance proof creates PID 1 and PID 2, maps the same lower-half virtual address to different physical frames, performs real CR3 switches, dereferences the same VA through each active address space, and then lets the real PIT scheduler switch between two process-owned CPU-bound tasks with no `task_yield()` calls.

The accepted process-isolation values were:

```text
Test virtual address:     0x0000004000000000
Process A PID:            1
Process B PID:            2
Process A root:           0x0000000000078000
Process B root:           0x0000000000079000
Process A physical frame: 0x000000000007A000
Process B physical frame: 0x000000000007B000
Address-space switches:   18
Preemptive CR3 switches:   7
Process A slices:           3
Process B slices:           3
```

PID 1 writes and later rereads `0xAAAAAAAAAAAAAAAA` at the shared test VA. PID 2 independently writes and later rereads `0xBBBBBBBBBBBBBBBB` at the same VA. The scheduler restores PID 0/bootstrap CR3 afterward, and process-owned mappings, page tables and data frames are reclaimed with PMM bookkeeping verified.

All earlier PMM, VMM, heap, IRQ/EOI, cooperative task, full-GPR preemption, Divide Error and Page Fault acceptance checks remain green.

For the deliberately legacy bootstrap timer proof, QEMU remains:

```text
-M q35 -cpu qemu64,apic=off -m 128M
```

There is still **no ring 3, syscall layer, userspace loader, VFS, RAMFS, block-device stack, BoringFS implementation, init, shell, networking, SMP or modern APIC timer path**.

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

This original VMM remains the selected-mapping manager for the inherited PID-0/bootstrap root. Milestone 8 adds separate process address-space roots without rewriting that established bootstrap VMM.

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

- keep cooperative call-boundary context separate from interrupt-time context;
- use a complete 192-byte x86_64 interrupt frame for arbitrary-instruction preemption;
- preserve all integer GPRs plus `RIP`, `CS`, `RFLAGS`, `RSP` and `SS`;
- retain an interrupted task's frame on that task's own 16-KiB stack;
- let C scheduling policy select the frame that IRQ assembly restores;
- acknowledge the PIC before assembly abandons the interrupted IRQ stack;
- restore the selected context through full GPR restore + `iretq`;
- deterministic fixed-table round robin;
- one PIT tick per scheduling quantum;
- synthetic full restore frames for never-run preemptive tasks;
- clean bootstrap IRQ-frame restoration;
- no allocation or serial logging in the timer scheduling path;
- CPU-bound stress tasks with no `task_yield()`;
- full integer GPR preservation proof;
- stack/local-state/cleanup/bookkeeping validation;
- retained PMM/VMM/heap/IRQ/#DE/#PF regressions.

The accepted preemption proof observed:

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

Milestone 7 proves timer-driven switching between kernel tasks. Milestone 8 extends that scheduler so a selected task may also carry an independent process address space.

## Milestone 8: process and address-space model — COMPLETE

Implemented, merged and accepted on `main` in QEMU:

- keep **task** and **process** as separate concepts;
- introduce a bounded minimal process object with PID, state and address-space ownership;
- reserve PID 0 for the bootstrap/kernel process;
- allocate deterministic PID 1 and PID 2 test processes;
- represent the inherited active CR3 root explicitly as PID 0's non-destroyable bootstrap address space;
- introduce an explicit x86_64 `address_space` object with root physical address and owned page-table-frame tracking;
- create new PMM-backed process PML4 roots;
- keep PML4 slots 0–255 private to each process in the current bootstrap model;
- share/copy PML4 entries 256–511 from PID 0 so required higher-half kernel mappings remain executable;
- explicitly protect inherited/shared kernel page tables from process destruction;
- provide explicit create/activate/map/translate/unmap/destroy address-space operations;
- add a tiny architecture-specific CR3 write operation and avoid unnecessary reload when a requested root is already active;
- use real CR3 loads as the broad TLB invalidation mechanism for this bootstrap model;
- centrally reserve process test VA `0x0000004000000000`;
- map that same VA to distinct PMM data frames in PID 1 and PID 2;
- prove same-VA/different-PA translation;
- activate each real root and dereference the test VA through the CPU rather than through an HHDM physical alias;
- write/retain `0xAAAAAAAAAAAAAAAA` in PID 1 and `0xBBBBBBBBBBBBBBBB` in PID 2;
- extend tasks with an owning-process pointer;
- make the scheduler activate the selected task's process root before returning the selected interrupt frame;
- preserve PIC EOI before the assembly task-stack/frame switch;
- bind one non-yielding CPU-bound task to PID 1 and one to PID 2;
- prove real PIT preemption repeatedly switches both task context and CR3;
- prove each process-owned task observes only its own pattern at the identical VA;
- restore task 0 + PID 0/bootstrap CR3 after the test;
- free finished task stacks before process destruction;
- unmap private process pages and reclaim only process-owned empty PT/PD/PDPT/root frames;
- free process test data frames back to PMM;
- verify process/address-space cleanup and PMM/heap/VMM bookkeeping;
- retain and pass all existing PMM, VMM, heap, exception, IRQ, cooperative-task, full-GPR preemption, Divide Error and Page Fault regressions.

The merged-main process/address-space proof reported:

```text
Process subsystem:
Bootstrap PID: 0
Processes created: 2
Address spaces created: 2
Process model: online

Address-space test:
Test virtual address: 0x0000004000000000
Process A PID: 1
Process B PID: 2
Process A root: 0x0000000000078000
Process B root: 0x0000000000079000
Process A physical frame: 0x000000000007A000
Process B physical frame: 0x000000000007B000

Process/address-space self-test:
  process-create: PASS
  unique-pid: PASS
  address-space-create: PASS
  distinct-root: PASS
  same-va-different-pa: PASS
  cr3-switch: PASS
  kernel-mappings: PASS
  process-a-isolation: PASS
  process-b-isolation: PASS
  preemptive-address-space-switch: PASS
  bootstrap-return: PASS
  address-space-cleanup: PASS
  pmm-bookkeeping: PASS

Address-space switches: 18
Preemptive CR3 switches: 7
Process A slices: 3
Process B slices: 3
```

This proves that BoringKernel can maintain distinct process identities with independent x86_64 address-space roots and safely switch those roots while scheduling CPL0 kernel tasks.

It does **not** prove userspace execution or isolation from privileged kernel code. Every current task still runs at CPL0 and can therefore execute privileged instructions. Ring 3 is required before untrusted userspace exists.

## Milestone 9: ring 3 transition — NEXT

Enter real x86_64 CPL 3 code and prove that user-mode code cannot directly execute privileged kernel operations.

A future implementation must explicitly address at least:

- appropriate user code/data selectors;
- a valid TSS and privilege-stack transition path where required;
- a deliberately constrained user address/mapping setup;
- controlled transition from kernel CPL0 to user CPL3;
- a real user-mode instruction stream verified by CPL/segment state;
- a deliberate privileged operation from CPL3 that faults rather than succeeding;
- preservation of the already verified process/address-space, interrupt and scheduler invariants.

Milestone 9 has **not** been started and requires a separate instruction.

It must not silently expand into a syscall ABI, ELF loader, userspace runtime, VFS, storage, networking, or other later milestones.

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

## Ring 3 transition

The next roadmap item is **Milestone 9**.

It has **not** been started and requires a separate instruction.

Do not jump ahead to:

- syscalls;
- userspace loading;
- VFS/RAMFS;
- BoringFS implementation;
- storage drivers;
- networking.
