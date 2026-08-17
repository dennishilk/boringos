# BoringOS architecture — current bootstrap state

These decisions remain deliberately narrow and provisional. This document describes implemented behavior only.

## Platform and boot boundary

- **Architecture:** x86_64.
- **Reference machine:** QEMU `q35`, one `qemu64,apic=off` CPU for the current legacy PIC/PIT bootstrap path.
- **Bootloader:** Limine 12.5.2.
- **Kernel:** freestanding, statically linked ELF64, primarily C with small isolated architecture assembly.
- **Privilege:** CPL0 only. There is no ring-3/userspace execution.
- **Bootstrap address space:** PID 0 adopts the active Limine-created four-level root.
- **Additional process address spaces:** PMM-backed independent roots with private lower-half mappings and shared higher-half kernel mappings.

The current normal boot path proves PMM, selected bootstrap-VMM mappings, the bounded kernel heap, exception infrastructure, repeated hardware PIT/PIC interrupts, cooperative kernel contexts, timer-driven preemptive kernel scheduling, process identity, real CR3 switching, and independent process address spaces.

## Physical memory

BoringKernel allocates 4096-byte physical frames only from Limine memory-map entries marked usable. A bounded static bitmap and usable-region table provide deterministic first-fit allocation, free/reuse, and invalid/double-free rejection. Bootloader-reclaimable and all other non-usable types remain non-allocatable.

All new process root tables, process-private lower-level page tables, and process-isolation data frames come from this existing PMM. There is no second physical allocator.

## Bootstrap VMM

The original VMM requests x86_64 four-level paging, discovers Limine's HHDM, reads the inherited root from `CR3`, and controls selected 4-KiB mappings inside the bootstrap address space.

New VMM page-table frames come from PMM and are accessed through the HHDM. Empty tables are returned to PMM only when VMM allocated them itself. The early VMM test window remains:

```text
[0xffffff0000000000, 0xffffff0000200000)
```

The QEMU self-test maps a PMM frame, translates it, performs a real write/read, unmaps it, invalidates the TLB entry, and restores PMM/VMM bookkeeping.

This inherited bootstrap root is now represented explicitly as PID 0's address space. It is never freed by the process layer.

## Process address-space layer

BoringKernel 0.0.9-dev adds an explicit address-space abstraction alongside the original bootstrap VMM.

The current object is deliberately small:

```c
struct address_space {
    uint64_t root_physical;
    uint64_t owned_table_frames[16];
    uint64_t owned_table_count;
    bool bootstrap;
    bool initialized;
};
```

The API can create, activate, map, translate, unmap, validate, and destroy a specific address space. Lower-half operations therefore do not implicitly depend on whichever CR3 happens to be active.

### Kernel/private split

The verified current split is:

```text
PML4 slots   0-255   process-private lower half
PML4 slots 256-511   shared kernel higher half
```

A new process root begins as a zeroed PMM frame. Entries 256-511 are copied from the PID-0/bootstrap PML4 and continue to reference the existing higher-half page-table structures. Kernel physical memory is not duplicated.

Important current shared entries include:

```text
slot 256 → HHDM
slot 510 → VMM test + kernel heap region
slot 511 → linked higher-half kernel image
```

Sharing the full current higher half preserves the mappings used by kernel code/data, HHDM physical access, heap, task stacks, PMM/VMM metadata, IDT, exception/IRQ code, scheduler metadata, and still-referenced Limine structures.

### Ownership rule

A non-bootstrap address space records only page-table frames allocated specifically for that address space:

- its own root PML4;
- its own process-private PDPT/PD/PT frames.

It never records or frees:

- PID 0's inherited/Limine root;
- shared higher-half page-table structures;
- bootstrap VMM-owned shared tables;
- page tables owned by another process.

Process-private lower-level tables are reclaimed only after their mappings have been removed and the tables become empty. An address space must not be active when destroyed. Destruction requires the lower half to be empty and the root to be the final owned table frame.

### CR3 activation

`address_space_activate()` validates the target root and shared kernel entries. It skips a reload when the requested root is already active. Otherwise the architecture layer performs a real `mov` to `CR3`, reads CR3 back, and updates current-address-space bookkeeping only after the switch succeeds.

Loading CR3 supplies the broad TLB invalidation needed by this bootstrap model. There is no PCID support.

## Process model

A kernel task is not a process.

```text
task
→ execution context / scheduling entity

process
→ identity + address-space ownership
```

The current process object is:

```c
struct process {
    uint64_t pid;
    struct address_space address_space;
    enum process_state state;
    bool slot_used;
};
```

States are only:

```text
ALIVE
FINISHED
```

PID policy is deterministic and monotonic in this bootstrap stage:

```text
PID 0 → bootstrap/kernel process
PID 1 → first process test object
PID 2 → second process test object
```

There are no process trees, signals, credentials, sessions, process groups, file-descriptor tables, environment variables, zombies, PID namespaces, `fork`, `exec`, or `waitpid`.

See [`processes.md`](processes.md) for the complete current process/address-space model.

## Kernel heap

The bounded heap owns:

```text
[0xffffff0000200000, 0xffffff0001200000)
```

It starts with two mapped pages, grows one 4096-byte page at a time through PMM + bootstrap VMM, uses deterministic first-fit allocation with 16-byte payload alignment, splits free blocks, and coalesces adjacent free blocks. Mapped pages are intentionally retained after `kfree` in this bootstrap stage.

Because the heap lives in shared higher-half mappings, heap-backed task stacks remain reachable while any current process root is active.

The heap is single-core bootstrap infrastructure, not a production allocator.

## IDT and fatal exceptions

BoringKernel owns a 256-entry x86_64 IDT. Vectors 0-31 are CPU exception gates; PIC vectors 32-47 are installed later by the hardware-IRQ layer. Configured entries are DPL0 64-bit interrupt gates using the current kernel code selector and IST 0.

The real acceptance suite continues to prove:

```text
Divide Error → vector 0  → BoringKernel diagnostic → controlled halt
Page Fault   → vector 14 → CR2/error decode       → controlled halt
```

Fatal exceptions do not return. Double Fault still lacks a dedicated IST/emergency stack.

## Complete normalized x86_64 trap frame

The exception and IRQ entry paths share one **192-byte** normalized ring-0 frame:

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

The first `RSP`/`SS` pair is a C-facing copy. The tail contains the return state consumed by `iretq`. IRQ vector stubs synthesize error code zero and preserve all 15 integer GPRs before entering C.

This frame is also the preemptive task context. An interrupted task's saved frame remains on that task's own stack until the task is resumed.

## Bootstrap PIC/PIT timer

The legacy 8259 PIC is remapped to vectors 32-47. Both PICs begin fully masked; after PIT initialization only IRQ0 is unmasked (`0xfe` master / `0xff` slave).

PIT channel 0 uses input clock 1,193,182 Hz. A requested 100 Hz produces divisor 11932 and approximately 99.998491 Hz, reported as 99998 mHz. The timer tick is advanced only by real IRQ0 delivery.

The initial hardware acceptance still requires at least ten IRQ0 deliveries, proving repeated acknowledgement and `iretq` return rather than accepting a single first interrupt.

## Cooperative kernel contexts

Cooperative switching remains available and deliberately uses a smaller SysV AMD64 call-boundary context:

```text
RSP RBX RBP R12 R13 R14 R15
```

A switch occurs only when kernel code explicitly calls `task_yield()` or when a cooperative task returns and must leave its stack. Existing cooperative regression tasks belong to PID 0 and therefore normally keep the bootstrap root active.

The cooperative QEMU test still proves two independent 16-KiB stacks, alternating execution, task-local state, callee-saved register preservation, clean return, stack cleanup, and timer coexistence.

## Timer-driven preemptive scheduling with process roots

Scheduling policy remains deliberately minimal:

```text
READY / RUNNING / FINISHED
deterministic fixed-table round robin
1 PIT tick = 1 quantum
no priorities
```

Every task now carries an owning process pointer. The preemptive path is:

```text
Task A executing
    ↓
real PIT IRQ0 / vector 32
    ↓
complete frame saved on A's stack
    ↓
timer tick++
    ↓
task_scheduler_tick(A frame)
    ↓
select Task B
    ↓
process_activate(B.process)
    ↓
real CR3 load if root differs
    ↓
PIC EOI while still on A's shared kernel IRQ stack
    ↓
IRQ assembly loads B's saved frame into RSP
    ↓
restore all integer GPRs + iretq
    ↓
Task B executes in B's process address space
```

The CR3 switch happens while executing shared higher-half kernel code and stack mappings. `irq.c` still sends EOI before assembly changes to the target task stack, preserving the previously verified acknowledgement ordering.

Existing PID-0 preemption regression tasks still use the same bootstrap root and therefore do not cause unnecessary CR3 reloads.

## Fresh tasks and bootstrap restoration

Each new preemptive task receives an independent 16-KiB heap-backed stack plus a synthetic complete IRQ/`iretq` frame on that stack. The frame targets `kernel_task_trampoline`, uses the current kernel CS/SS, has IF set, and supplies a correctly aligned resumed RSP.

Bootstrap remains task ID 0 in process PID 0. When preemption starts, the next real PIT interrupt creates an authentic bootstrap IRQ frame on that stack. The scheduler retains it. When all ordinary preemptive tasks finish, it activates PID 0/bootstrap root if needed and restores that saved bootstrap frame through `iretq`.

## Full integer-register preservation

Preemption preserves:

```text
RAX RBX RCX RDX RSI RDI RBP
R8 R9 R10 R11 R12 R13 R14 R15
RSP RIP CS RFLAGS SS
```

The existing acceptance suite keeps an assembly-assisted full-GPR probe across real timer preemption. There is still no FPU/SIMD context switching; kernel C remains compiled with implicit x87/MMX/SSE/SSE2 generation disabled.

## Verified process/address-space acceptance

The process test uses one lower-half address in both process roots:

```text
TEST_VA = 0x0000004000000000
```

It maps:

```text
PID 1: TEST_VA → frame A
PID 2: TEST_VA → frame B
frame A != frame B
```

It activates each real root and dereferences `TEST_VA` through the CPU:

```text
PID 1 writes 0xAAAAAAAAAAAAAAAA
PID 2 writes 0xBBBBBBBBBBBBBBBB
PID 1 later reads 0xAAAAAAAAAAAAAAAA
PID 2 later reads 0xBBBBBBBBBBBBBBBB
```

The stronger proof binds Task A to PID 1 and Task B to PID 2. Neither calls `task_yield()`. Real PIT IRQ0 scheduling repeatedly switches both task context and CR3 while each task accesses the same VA and must observe only its own pattern.

A verified clean-source QEMU run reported:

```text
Process A root:           0x0000000000078000
Process B root:           0x0000000000079000
Process A physical frame: 0x000000000007A000
Process B physical frame: 0x000000000007B000
Address-space switches:   18
Preemptive CR3 switches:   7
Process A slices:           3
Process B slices:           3
```

All required process, isolation, CR3, bootstrap-return, cleanup, and PMM-bookkeeping checks passed while the earlier PMM/VMM/heap/IRQ/cooperative/preemption/#DE/#PF regressions remained green.

## Cleanup and ownership proof

Process cleanup runs only after the CPU has returned to the PID-0/bootstrap root. Finished task stacks are freed first. Process-private test mappings are removed, empty process-owned PT/PD/PDPT frames are reclaimed, the two test data frames are returned to PMM, and finally each inactive process root PML4 is freed.

Shared higher-half kernel page tables remain alive because they were never recorded as process-owned frames.

PMM bookkeeping accounts for legitimate retained shared kernel-heap growth while requiring all process-specific page tables and data frames to be reclaimed.

## C / assembly boundary

High-level policy remains in C: memory ownership, address-space policy, process identity, exception diagnostics, PIC/timer dispatch, cooperative selection, preemptive round robin, task/process association, cleanup, and acceptance invariants.

Architecture assembly remains isolated to exception/IRQ entry and restore mechanics, `iretq`, minimal cooperative context save/restore, deliberate fault triggers, register probes, and CPU instructions that C cannot express directly. CR3 read/write and `invlpg` remain tiny isolated architecture-specific operations.

## Current limitations

BoringKernel still does **not** provide:

- ring 3 or untrusted user-mode execution;
- user CS/SS or TSS privilege-stack switching;
- syscalls or safe user-memory crossing;
- ELF userspace loading or a userspace runtime;
- fork, exec, wait, signals, copy-on-write, or PID namespaces;
- file descriptors, credentials, process trees, sessions, or process groups;
- PCID, demand paging, or swap;
- FPU/SIMD task/process state;
- sleeping, blocked I/O, wait queues, or wakeups;
- mutexes, semaphores, or condition variables;
- priorities or realtime scheduling;
- SMP or per-CPU scheduler/process state;
- LAPIC, IOAPIC, APIC timer, HPET, or ACPI/MADT;
- VFS, RAMFS, BoringFS implementation, or storage drivers;
- input, networking, USB, audio, graphics, or native BoringWM.

BoringKernel 0.0.9-dev proves **CPL0 process identity and independent x86_64 address spaces with real scheduler-owned CR3 switching**. It does not prove userspace safety. A real Ring 3 transition remains a separate later milestone.
