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
controlled halt
```

BoringKernel currently runs in ring 0 and has a minimal physical page-frame allocator. It still has no BoringKernel-owned page-table manager, heap, IDT, interrupts, timer, scheduler, processes, ring 3, syscalls, userspace loader, VFS, RAMFS, block devices, storage drivers, BoringFS implementation, init or shell.

Every milestone should keep the existing QEMU acceptance test green and add a focused test for the new capability.

---

# Stage 1 — memory foundations

## Milestone 1: physical memory manager — COMPLETE

Implemented and accepted when the QEMU test passes:

- consume the Limine memory map;
- validate entries defensively;
- accept only memory explicitly marked usable;
- normalize usable ranges to 4096-byte boundaries;
- track usable page frames with a bounded static bitmap;
- allocate unique 4-KiB physical frames;
- free allocated frames;
- detect invalid/double frees where practical;
- verify alignment, uniqueness, usable-range membership, free/reuse and bookkeeping inside QEMU.

This milestone deliberately does not map frames into virtual address space.

## Milestone 2: BoringKernel-owned virtual memory — NEXT

Take explicit ownership of x86_64 page-table management instead of indefinitely relying on the bootloader-created mappings.

Minimum goals:

- inspect and understand the mappings inherited at boot;
- create a BoringKernel-owned page-table root;
- map and unmap 4-KiB pages using PMM-provided frames;
- establish explicit kernel virtual-address conventions;
- preserve the kernel, stack and required boot-time mappings during handover;
- add bounds/overflow checks to page-table walking;
- keep architecture-specific page-table code isolated;
- prove mappings in QEMU without introducing a heap.

Do not add userspace mappings, copy-on-write or a broad VM subsystem in this milestone.

## Milestone 3: kernel heap

Build a small general-purpose kernel allocator on top of physical and virtual memory primitives.

Initial requirements:

- explicit allocation failure;
- alignment support;
- overflow-safe sizes;
- deterministic behavior;
- development checks for invalid frees where practical;
- no slab ecosystem until a concrete need exists.

---

# Stage 2 — controlled faults, interrupts and time

## Milestone 4: exception infrastructure

Introduce the minimum x86_64 descriptor/exception machinery required for controlled faults:

- IDT;
- minimal isolated entry assembly where unavoidable;
- readable serial fault reports;
- safe halt/panic after unrecoverable exceptions;
- page-fault diagnostics.

GDT/TSS work should remain limited to what privilege transitions and exception handling actually require.

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

## BoringKernel-owned virtual memory

The next implementation task should do **only** page-table ownership and 4-KiB map/unmap foundations using frames from the now-working PMM.

It must not add:

- kernel heap;
- IDT/interrupts;
- timer;
- scheduler;
- processes;
- ring 3;
- syscalls;
- VFS;
- RAMFS;
- BoringFS implementation;
- shell code;
- storage drivers.

The next narrow proof is:

> BoringKernel can map and unmap PMM-owned physical frames through page tables that BoringKernel itself controls.
