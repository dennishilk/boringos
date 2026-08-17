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
Limine memory map
    ↓
BoringKernel physical memory manager
    ↓
4-KiB frame allocate/free self-test
    ↓
Limine HHDM + active CR3 root
    ↓
BoringKernel selected 4-KiB virtual mapping
    ↓
translate + real write/read + unmap self-test
    ↓
bounded BoringKernel heap
    ↓
byte allocation + growth + free/reuse self-test
    ↓
controlled halt
```

BoringKernel currently runs in ring 0, owns a minimal physical page-frame allocator, can create/remove selected 4-KiB mappings in the active x86_64 four-level address space, and has a bounded dynamic kernel heap backed through those PMM/VMM layers. The active root and boot-critical mappings remain inherited from Limine. There is still no IDT, exception framework, interrupts, timer, scheduler, processes, ring 3, syscalls, userspace loader, VFS, RAMFS, block devices, storage drivers, BoringFS implementation, init or shell.

Every milestone should keep the existing QEMU acceptance test green and add a focused test for the new capability.

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
- reclaim empty page-table frames only when those tables were allocated by the VMM itself;
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
- reject outside-heap, unaligned/interior and double frees without silently trusting caller-provided metadata;
- retain mapped heap pages after `kfree` rather than pretending page-level shrinking exists;
- test real allocations of 1, 16, 64, 200, 6000 and 4096 bytes;
- verify real write/read patterns, alignment, non-overlap, PMM/VMM-backed growth, valid free, deterministic reuse, invalid/double-free rejection and final bookkeeping.

The accepted QEMU run starts with two heap pages, forces one additional page mapping, then frees all test allocations. Three pages remain mapped by policy with zero used payload bytes and one coalesced free block.

This milestone proves a functioning small dynamic kernel heap. It does **not** claim a production allocator: there is no SMP locking, slab/object-cache layer, per-CPU allocation, guard-page system or page-level heap shrinking.

---

# Stage 2 — controlled faults, interrupts and time

## Milestone 4: exception infrastructure — NEXT

Introduce the minimum x86_64 descriptor/exception machinery required for controlled faults:

- IDT;
- minimal isolated entry assembly where unavoidable;
- readable serial fault reports;
- safe halt/panic after unrecoverable exceptions;
- page-fault diagnostics.

GDT/TSS work should remain limited to what privilege transitions and exception handling actually require.

No exception/IDT code is part of the completed heap milestone.

## Milestone 5: interrupt controller and timer

Establish one tested interrupt path and one timer source under QEMU.

Acceptance should prove interrupt acknowledgement and timer progress without flooding the serial console.

---

# Stage 3 — execution and process foundations

## Milestone 6: kernel execution contexts / scheduler

Implement the smallest scheduler capable of switching between independent kernel execution contexts.

Required properties include explicit task state, dedicated stacks, deterministic runnable ordering and a minimal isolated context-switch boundary.

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

## Exception infrastructure

The next implementation task is the separate Milestone 4: establish the smallest controlled x86_64 exception path so faults can be diagnosed instead of relying only on missing-output/time-out detection.

It must remain separate from the completed heap and must not jump ahead to:

- timer/scheduler work;
- threads or processes;
- ring 3;
- syscalls;
- VFS/RAMFS;
- BoringFS implementation;
- shell code;
- storage drivers.

The next narrow proof is:

> BoringKernel can receive a deliberately triggered CPU exception through its own IDT path, report useful fault information over serial, and halt in a controlled way.
