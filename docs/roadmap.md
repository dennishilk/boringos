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
physical memory manager
    ↓
selected 4-KiB virtual mappings
    ↓
bounded kernel heap
    ↓
BoringKernel x86_64 IDT
    ↓
CPU exception diagnostics
    ↓
legacy PIC remapped to vectors 32–47
    ↓
PIT channel 0 → IRQ0 → vector 32
    ↓
C timer tick + PIC EOI
    ↓
restore + iretq
```

BoringKernel currently runs in ring 0, owns a minimal physical page-frame allocator, can create/remove selected 4-KiB mappings in the active x86_64 four-level address space, has a bounded dynamic kernel heap, installs its own x86_64 IDT, diagnoses real CPU Divide Error and MMU Page Fault exceptions, and now receives repeated real PIT-generated IRQ0 interrupts through a remapped 8259-compatible PIC.

The verified bootstrap timer path maps IRQ0 to vector 32, increments a `uint64_t` tick counter only from the IRQ handler, sends PIC End Of Interrupt and returns with `iretq`. The accepted merged-main QEMU run required at least ten timer deliveries, proving repeated acknowledgement/return rather than accepting a single first interrupt.

For this deliberately legacy bootstrap proof, the QEMU `q35` reference uses one `qemu64,apic=off` CPU. No LAPIC/IOAPIC configuration is implemented yet. The active page-table root and boot-critical mappings remain inherited from Limine.

There is still no scheduler, preemption, task switching, threads, processes, ring 3, syscalls, userspace loader, VFS, RAMFS, block devices, storage drivers, BoringFS implementation, init or shell.

Every milestone should keep all earlier QEMU acceptance checks green and add a focused proof for the new capability.

---

# Stage 1 — memory foundations

## Milestone 1: physical memory manager — COMPLETE

Implemented and accepted in QEMU:

- consume the Limine memory map;
- validate entries defensively;
- accept only memory explicitly marked usable;
- normalize usable ranges to 4096-byte boundaries;
- track usable page frames with a bounded static bitmap;
- allocate unique 4-KiB physical frames;
- free allocated frames;
- detect invalid/double frees where practical;
- verify alignment, uniqueness, usable-range membership, free/reuse and bookkeeping inside QEMU.

## Milestone 2: selected BoringKernel virtual-memory control — COMPLETE

Implemented and accepted in QEMU:

- explicitly request x86_64 four-level paging from Limine;
- request and validate Limine's HHDM offset;
- read the active PML4 physical address from `CR3` without replacing it;
- preserve the running kernel, stack, HHDM and boot-time mappings inherited from Limine;
- centralize checked physical-to-HHDM access for page-table memory;
- walk ordinary PML4 → PDPT → PD → PT paths;
- allocate missing PDPT/PD/PT frames through the existing PMM;
- map one 4-KiB PMM-owned physical frame at a controlled kernel virtual address;
- translate the mapping back to the same physical frame;
- perform a real write/read through the mapped virtual address;
- unmap the page and invalidate the TLB entry with `invlpg`;
- reclaim empty page-table frames only when those tables were allocated by VMM itself;
- verify VMM and PMM bookkeeping returns to the pre-test state.

This milestone proves selected mapping control. It does **not** claim complete address-space ownership and does not replace Limine's active root page table.

## Milestone 3: kernel heap — COMPLETE

Implemented and accepted in QEMU:

- reserve the finite heap virtual range `[0xffffff0000200000, 0xffffff0001200000)`;
- start with exactly two mapped 4-KiB heap pages;
- obtain every backing data frame through PMM and install every heap mapping through VMM;
- grow explicitly by one 4-KiB page when first-fit cannot satisfy an allocation;
- stop cleanly at the finite 16-MiB heap limit;
- provide `heap_init`, `kmalloc` and `kfree` without libc;
- align returned payload pointers to 16 bytes;
- use a deterministic first-fit block allocator with 48-byte in-heap headers;
- split reusable free blocks and immediately coalesce adjacent free blocks;
- validate block magic, guard values, sizes, alignment, bounds, links and contiguous topology;
- define `kmalloc(0)` as `NULL`;
- reject outside-heap, unaligned/interior and double frees;
- retain mapped heap pages after `kfree` rather than pretending page-level shrinking exists;
- test real allocations of 1, 16, 64, 200, 6000 and 4096 bytes;
- verify real write/read patterns, alignment, non-overlap, PMM/VMM-backed growth, valid free, deterministic reuse, invalid/double-free rejection and final bookkeeping.

The accepted QEMU run starts with two heap pages, forces one additional page mapping, then frees all test allocations. Three pages remain mapped by policy with zero used payload bytes and one coalesced free block.

This milestone proves a functioning small dynamic kernel heap. It does **not** claim a production allocator.

---

# Stage 2 — controlled faults, interrupts and time

## Milestone 4: exception infrastructure — COMPLETE

Implemented and accepted in QEMU:

- allocate a BoringKernel-owned 256-entry x86_64 IDT;
- install valid exception gates for vectors 0–31;
- use DPL0 64-bit interrupt gates with the currently executing kernel code selector;
- validate the configured descriptors before loading the table;
- execute `lidt` and verify the active IDTR with `sidt`;
- keep exception policy/diagnostics in C and isolate ABI-required entry work in one x86_64 assembly source file;
- normalize exceptions with and without CPU-pushed hardware error codes into one trap-frame contract;
- preserve general-purpose registers and report vector, name, error code, RIP, CS, RFLAGS, RSP and SS;
- report `CR2` and decode relevant Page Fault error-code state;
- provide a dedicated minimal Double Fault diagnostic while documenting that it still uses the normal kernel stack and has no IST hardening;
- never return from a fatal exception; end through the existing controlled `cli`/`hlt` halt loop;
- add separate normal, divide and pagefault build/test modes rather than crashing the ordinary successful boot;
- preserve all previous PMM, VMM and heap acceptance checks.

Verified real exception proofs include CPU-generated Divide Error on vector 0 and MMU-generated Page Fault on vector 14 with `CR2` matching the deliberately unmapped test address.

This milestone proves a working fatal CPU-exception path through BoringKernel's own IDT. It does **not** provide recoverable faults, demand paging or a hardened Double Fault IST stack.

## Milestone 5: hardware interrupt controller and periodic timer — COMPLETE

Implemented, merged and accepted on `main` in QEMU:

- retain CPU exception vectors 0–31 unchanged;
- install DPL0 IDT interrupt gates for legacy PIC vectors 32–47;
- initialize the 8259-compatible master/slave PICs in 8086 mode;
- remap master IRQ0–7 to vectors 32–39 and slave IRQ8–15 to vectors 40–47;
- mask all hardware IRQs initially;
- program PIT channel 0 in rate-generator mode from the 1,193,182 Hz input clock;
- request 100 Hz and calculate divisor 11932, producing approximately 99.998491 Hz;
- unmask only IRQ0, leaving master/slave masks at `0xfe` / `0xff`;
- execute `sti` only after IDT/PIC/PIT/mask state has been validated;
- preserve GPRs in small x86_64 IRQ stubs, dispatch policy in C and return from normal IRQs with `iretq`;
- increment the timer tick counter only from real IRQ0 dispatch;
- send master EOI for master IRQs and slave-before-master EOI for slave IRQs;
- handle the straightforward spurious IRQ7/IRQ15 acknowledgement rules;
- avoid dynamic allocation and per-tick serial output in the IRQ handler;
- require at least ten asynchronous timer IRQ deliveries before success;
- verify repeated delivery, practical EOI correctness, zero unexpected IRQs and normal return from interrupt;
- preserve the existing real Divide Error and Page Fault QEMU tests.

The accepted normal path reported:

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

The current QEMU reference uses `-M q35 -cpu qemu64,apic=off` specifically so this milestone remains an isolated PIC/PIT bootstrap rather than silently introducing LAPIC setup. PIC/PIT is temporary bootstrap infrastructure, not the intended final modern interrupt architecture.

This milestone proves that BoringKernel can receive, acknowledge and return from repeated real periodic hardware interrupts. It does **not** implement scheduling, preemption, task switching, LAPIC, IOAPIC, HPET, ACPI/MADT or SMP.

---

# Stage 3 — execution and process foundations

## Milestone 6: kernel execution contexts / scheduler — NEXT

Implement the smallest scheduler capable of switching between independent kernel execution contexts.

Required properties include explicit task state, dedicated stacks, deterministic runnable ordering and a minimal isolated context-switch boundary.

The timer now exists as a real interrupt source, but this milestone must not assume that timer-driven preemption is automatically required. Context switching and scheduling policy need their own narrow design and acceptance proof.

## Milestone 7: process and address-space model

Define a small process object with a separate virtual address space and clean resource reclamation.

## Milestone 8: ring 3 transition

Enter real x86_64 CPL 3 code and prove that user code cannot directly perform privileged kernel operations.

## Milestone 9: syscall mechanism and safe user-memory crossing

Create the first controlled userspace/kernel API with checked arguments, bounded user copies and explicit error returns.

The stable public syscall ABI should not be declared finished at this point.

---

# Stage 4 — load and run native BoringOS programs

## Milestone 10: ELF userspace loader

Load a deliberately constrained x86_64 userspace ELF with strict validation of class, machine, segments, ranges and permissions.

The first executable may arrive as a boot module rather than pretending a filesystem exists.

## Milestone 11: minimal native userspace runtime

Provide BoringOS-owned C entry glue, syscall wrappers and tiny string/memory helpers. No host libc.

## Milestone 12: serial console / TTY path for userspace

Expose real console input/output through the kernel API so early native programs can use the QEMU serial terminal.

---

# Stage 5 — VFS and RAMFS

## Milestone 13: VFS core

Introduce filesystem-independent kernel objects and path walking.

The first backend operation set should remain close to:

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

VFS owns paths, mount relationships, open handles and process working directories. Filesystem backends do not own process or terminal policy.

## Milestone 14: RAMFS backend

Implement a tiny real in-memory filesystem supporting nested directories and ordinary file operations through the VFS interface.

RAMFS remains strongly recommended before persistent BoringFS because it separates path/syscall semantics from block I/O and on-disk corruption handling.

---

# Stage 6 — native init and shell

## Milestone 15: `boring-init`

Run a real first userspace process:

```text
BoringKernel
    ↓
boring-init
```

Keep it intentionally small.

## Milestone 16: `boring-shell`

Launch a native C shell from `boring-init` in userspace.

A sensible order for real commands is:

1. `help`
2. `version`
3. `pwd`
4. `ls`
5. `cd`
6. `mkdir`
7. `touch`
8. `cat`
9. `echo` and later redirection
10. `rm`
11. `rmdir`
12. `mv`
13. `cp`
14. `pause`
15. `clear`
16. `reboot` / `shutdown`

Filesystem commands must operate through real VFS/syscall state. No simulated directory listings.

---

# Stage 7 — BoringFS host tooling before kernel writes

## Milestone 17: pure BoringFS format codec and validator

Implement the documented BoringFS v0 encoding/decoding rules in small C modules suitable for host tests and later kernel reuse where practical.

## Milestone 18: `mkboringfs`

Create deterministic valid BoringFS images containing an empty root directory.

## Milestone 19: `boringfsck`

Create a read-only structural validator/inspector. Initial versions report corruption and return non-zero; they do not attempt aggressive repair.

---

# Stage 8 — persistent block I/O

## Milestone 20: generic block-device layer

Create a filesystem-independent interface conceptually providing:

```text
logical_block_size
logical_block_count
writable
read(lba, count, destination)
write(lba, count, source)
flush()
```

BoringFS must depend only on this boundary, never directly on VirtIO, NVMe or AHCI.

## Milestone 21: QEMU VirtIO-block driver

Use VirtIO block as the first QEMU storage target unless implementation analysis finds a concrete blocker.

Prove bounded device capacity, read, write to a disposable test image and read-back persistence.

---

# Stage 9 — BoringFS kernel integration

## Milestone 22: read-only BoringFS mount

Mount a host-formatted BoringFS volume through:

```text
VirtIO block
    ↓
generic block device
    ↓
BoringFS driver
    ↓
VFS
```

Start read-only and reject invalid metadata before exposing files.

## Milestone 23: BoringFS mutation support

Add create, mkdir, write, truncate, unlink, rmdir, rename/move and block allocation/free only after read-only mounting is solid.

## Milestone 24: persistent native root filesystem

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

At that point shell operations such as `mkdir`, `touch`, `echo >`, `cat`, `mv` and `rm` must reflect persistent native BoringOS state.

---

# Security and corruption gates

Before persistent BoringFS writes are enabled, BoringOS must already have real privilege separation, separate address spaces, checked syscalls, validated user-memory copies, bounded kernel allocation, block-device bounds checks, BoringFS structural validation, host corruption tests and disposable QEMU integration tests.

Networking remains unrelated and deferred.

---

# Exact next implementation milestone

## Kernel execution contexts / scheduler

The next roadmap item is Milestone 6: define and prove the smallest kernel execution-context and scheduler mechanism.

It has **not** been started and requires a separate instruction.

A future implementation must remain narrow and must not jump ahead to:

- process/address-space semantics;
- ring 3;
- syscalls;
- userspace loading;
- VFS/RAMFS;
- BoringFS implementation;
- storage drivers;
- networking.

The next proof, only when separately requested, should establish that BoringKernel can switch between explicitly defined independent kernel execution contexts with deterministic scheduling behavior and clean stack/state ownership.
