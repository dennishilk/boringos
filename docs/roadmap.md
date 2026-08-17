# BoringOS roadmap — boot kernel to native shell and BoringFS

This roadmap starts from the **actual current repository state** after the first BoringKernel boot milestone. It is intentionally dependency-driven. Planned work is not implemented work.

## Current state

What genuinely works today:

```text
QEMU x86_64
    ↓
Limine
    ↓
BoringKernel ELF entry
    ↓
BoringKernel COM1 serial output
    ↓
controlled halt
```

Current BoringKernel runs in x86_64 ring 0 and can initialize its own serial output. It does **not** yet have a physical-memory allocator, BoringKernel-owned virtual-memory management, heap, IDT, interrupts, timer, scheduler, processes, ring 3, syscalls, userspace loader, VFS, RAMFS, block devices, storage drivers, BoringFS, init, or shell.

The filesystem roadmap must not skip those dependencies merely to display shell-like output earlier.

## Guiding dependency chain

The long-term path is:

```text
boot-only BoringKernel
    ↓
memory foundations
    ↓
exceptions / interrupts / time
    ↓
execution and process foundations
    ↓
ring 3 + safe syscall boundary
    ↓
userspace program loading
    ↓
VFS + RAMFS
    ↓
boring-init + boring-shell
    ↓
real volatile filesystem commands
    ↓
block-device layer + VirtIO block
    ↓
BoringFS
    ↓
persistent native root filesystem
```

Each milestone should keep the existing QEMU boot test green and add a focused acceptance test for the new capability.

---

# Stage 1 — memory foundations

## Milestone 1: physical memory manager

Consume and validate the Limine memory map and build a minimal allocator for **4096-byte physical page frames**.

Required properties:

- only memory explicitly reported as usable is handed out;
- all arithmetic on base addresses and lengths is overflow-checked;
- page alignment is enforced;
- allocated frames are unique until freed;
- invalid or double frees are detected where practical;
- allocator metadata remains understandable and bounded;
- a QEMU boot self-test allocates, verifies, frees, and re-allocates frames.

Do not add a kernel heap merely to make the allocator easier.

This is the **next single implementation milestone** after this design phase.

## Milestone 2: BoringKernel-owned virtual memory

Take explicit ownership of x86_64 page-table management instead of indefinitely relying on the bootloader-created environment.

Minimum goals:

- understand current mappings inherited at boot;
- create/map/unmap 4-KiB pages under kernel control;
- establish explicit kernel address-space conventions;
- protect non-writable/non-executable regions where practical;
- keep page-table operations isolated behind a small architecture layer.

Do not design general shared-memory or userspace mapping APIs yet.

## Milestone 3: kernel heap

Build a small kernel dynamic allocator on top of physical and virtual memory primitives.

Initial goals:

- `alloc`/`free`-style internal API;
- alignment support;
- overflow-safe sizes;
- deterministic failure on exhaustion;
- debug checks in development builds;
- host-side tests for allocator logic where possible.

Avoid a sophisticated slab ecosystem at this stage.

---

# Stage 2 — CPU exceptions, interrupts, and time

## Milestone 4: exception infrastructure

Introduce the minimum x86_64 descriptor/exception machinery needed for controlled faults.

Goals:

- IDT;
- exception entry stubs with minimal isolated assembly where unavoidable;
- readable exception reports over serial;
- safe halt/panic after unrecoverable kernel exceptions;
- page-fault reporting that captures relevant fault information.

GDT/TSS changes should be only those required for correct x86_64 privilege transitions and exception handling, not a speculative redesign.

## Milestone 5: interrupt controller and timer

Establish one tested timer source and interrupt path under QEMU.

Goals:

- mask/unmask control;
- timer ticks;
- interrupt acknowledgement;
- no lost acknowledgement loops;
- serial-test visibility without printing on every production tick.

The exact initial controller/timer choice should be selected when this milestone begins based on the QEMU reference machine and simplicity.

---

# Stage 3 — execution and process foundations

## Milestone 6: kernel execution contexts / scheduler

Implement the smallest scheduler capable of switching between independent kernel execution contexts.

Goals:

- explicit task state;
- dedicated stacks;
- deterministic runnable queue;
- context switch isolated to minimal architecture-specific assembly;
- timer-driven or explicitly yielded scheduling;
- QEMU test with at least two real execution contexts making progress.

Do not call these userspace processes until ring 3 and address-space separation exist.

## Milestone 7: process and address-space model

Define a small process object and separate address spaces.

Goals:

- per-process virtual address space;
- process identity;
- kernel-owned process resources;
- process lifecycle states;
- clean destruction and page reclamation;
- no shared mutable user address space by default.

Keep the model small. Threads can be deferred until a concrete need exists.

## Milestone 8: ring 3 transition

Enter real x86_64 CPL 3 code and return to the kernel only through controlled mechanisms.

Acceptance must prove that user code is actually running at ring 3 and cannot directly execute privileged kernel operations.

## Milestone 9: syscall mechanism and safe user-memory crossing

Create the first controlled userspace/kernel API.

Before broad syscall growth, establish:

- syscall entry/return path;
- argument validation;
- checked user pointers;
- `copy_from_user` / `copy_to_user`-style bounded helpers;
- clear error returns;
- no kernel dereference of unchecked userspace pointers.

The exact stable public syscall ABI should not be declared finished in the first implementation. Start with only enough calls to prove controlled userspace execution and console I/O.

---

# Stage 4 — load and run native BoringOS programs

## Milestone 10: ELF userspace loader

Load a deliberately constrained x86_64 ELF userspace executable.

Initial support should be narrow and validated:

- exact supported ELF class/endianness/machine;
- bounded program-header count;
- range/overflow checks;
- permitted segment types only;
- user/kernel address separation;
- writable/executable permissions derived explicitly;
- reject malformed or unsupported images.

The first test executable may be supplied as a boot module rather than pretending a filesystem already exists.

## Milestone 11: minimal native userspace runtime

Provide tiny BoringOS-owned C userspace support sufficient for first native programs:

- process entry glue;
- syscall wrappers;
- minimal string/memory helpers;
- no host libc dependency.

This is not yet a complete libc.

## Milestone 12: serial console / TTY path for userspace

Expose real console input/output to userspace through the syscall/device model.

An early `boring-shell` may legitimately run on the QEMU serial terminal. This is an early native console, not the final graphical terminal.

---

# Stage 5 — VFS and volatile filesystem semantics

## Milestone 13: VFS core

Introduce filesystem-independent kernel objects and path walking.

The first VFS should own:

- root filesystem reference;
- vnode/node abstraction;
- path component parsing;
- `.` and `..` semantics;
- lookup;
- create/remove/rename dispatch;
- open-file objects and offsets;
- process current working directory;
- directory enumeration abstraction.

The backend operation set should remain close to:

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

Exact public syscall numbers and locking policy remain separate decisions.

## Milestone 14: RAMFS backend

Implement a tiny real in-memory VFS filesystem.

RAMFS should support:

- root directory;
- nested directories;
- regular files;
- create/read/write;
- truncate;
- enumeration;
- unlink/rmdir;
- rename/move.

RAMFS is strongly recommended before BoringFS because it proves VFS, process CWD, syscalls, and shell semantics without also debugging disk I/O and persistent corruption.

Acceptance should operate through VFS APIs, not private RAMFS shortcuts.

---

# Stage 6 — native init and shell

## Milestone 15: `boring-init`

Run a real first userspace process:

```text
BoringKernel
    ↓
boring-init
```

Initial `boring-init` can remain extremely small. Its job is process-0/first-userspace orchestration, not service-manager complexity.

## Milestone 16: `boring-shell`

Launch a native C shell from `boring-init`:

```text
BoringKernel
    ↓
boring-init
    ↓
boring-shell
```

The shell must execute in userspace and reach the kernel through syscalls. Do not implement the final shell in kernel mode.

### First real shell commands

A sensible order is:

1. `help` — shell-owned command table;
2. `version` — real BoringOS/kernel version query or userspace build identity;
3. `pwd` — current VFS working directory;
4. `ls` — real VFS directory enumeration;
5. `cd` — change the shell process CWD;
6. `mkdir` — VFS directory creation;
7. `touch` — create a real empty file;
8. `cat` — real open/read/output loop;
9. `echo` — ordinary output first, then redirection after open/write/truncate exists;
10. `rm` — unlink a real file;
11. `rmdir` — remove a real empty directory;
12. `mv` — VFS rename/move;
13. `cp` — userspace read/write loop;
14. `pause` — wait for real input;
15. `clear` — terminal-control operation;
16. `reboot` / `shutdown` — controlled privileged system calls.

For the first filesystem acceptance target, the useful subset is:

```text
pwd
ls
cd
mkdir
touch
cat
echo with redirection
rm
rmdir
mv
cp
```

All must change or inspect actual RAMFS/VFS state. No simulated directory listing is acceptable.

A later persistent BoringFS mount should make the same shell commands work without changing their user-facing semantics.

---

# Stage 7 — BoringFS host tooling before kernel writes

## Milestone 17: pure BoringFS format codec and validator

Implement the documented BoringFS v0 encoding/decoding rules in small C modules designed to compile in host-side tests and later in the kernel where practical.

Goals:

- explicit little-endian field readers/writers;
- no direct struct dumping;
- overflow-safe block/offset arithmetic;
- superblock validation;
- bitmap validation;
- object-record validation;
- directory-record validation.

Host-specific file I/O remains outside the format codec.

## Milestone 18: `mkboringfs`

Create a deterministic empty BoringFS image containing a valid root directory.

Do not add a package installer or complex image-population language yet.

## Milestone 19: `boringfsck`

Create a read-only inspector/validator.

Initial behavior:

- validate complete metadata graph;
- report precise errors;
- non-zero exit on corruption;
- no automatic repair.

Host tests should mutate known-good images to exercise invalid magic, unsupported versions, invalid extents, duplicate names, malformed bitmaps, parent cycles, truncated images, and other corruptions documented in `docs/boringfs.md`.

---

# Stage 8 — persistent block I/O

## Milestone 20: generic block-device layer

Create a filesystem-independent kernel block-device interface.

Conceptual properties:

```text
logical_block_size
logical_block_count
writable
read(lba, count, destination)
write(lba, count, source)
flush()
```

BoringFS must depend only on this interface, never on a VirtIO/NVMe/AHCI implementation.

A bounded slice/partition wrapper should eventually be able to expose a subsection of a disk as another block device.

## Milestone 21: QEMU VirtIO-block driver

Prefer VirtIO block for the first persistent QEMU disk unless implementation analysis reveals a concrete blocker.

Acceptance:

- identify a configured QEMU VirtIO block device;
- read known sectors/blocks correctly;
- write only in a dedicated destructive test image;
- verify persistence by reading data back;
- validate device-reported capacity and request sizes;
- keep all filesystem knowledge out of the driver.

Do not add NVMe/AHCI in the same milestone.

---

# Stage 9 — BoringFS kernel integration

## Milestone 22: read-only BoringFS mount

Mount a host-formatted BoringFS image through:

```text
VirtIO block
    ↓
generic block device
    ↓
BoringFS driver
    ↓
VFS
```

Start read-only.

Acceptance should include:

- reject invalid magic/version;
- validate structural metadata;
- enumerate the root directory;
- read a known file crossing a 4-KiB filesystem-block boundary;
- expose files through normal VFS operations;
- `boring-shell` `ls`/`cat` works unchanged on the mounted volume.

## Milestone 23: BoringFS mutation support

Only after read-only mounting is solid, add:

- create;
- mkdir;
- write;
- truncate;
- unlink;
- rmdir;
- rename/move;
- allocation/freeing.

Every mutation must preserve the documented invariants. If structural validation fails, the filesystem must not continue writing.

Crash consistency beyond simple carefully ordered writes is not promised in v0; journaling remains out of scope. The limitations must be documented rather than hidden.

## Milestone 24: persistent native root filesystem

Boot with a BoringFS volume as the persistent root filesystem and launch native userspace from it.

Target chain:

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

At this point a session such as the following must reflect persistent state:

```text
boring> pwd
/
boring> mkdir projects
boring> cd projects
boring> touch hello.txt
boring> echo "moin" > hello.txt
boring> cat hello.txt
moin
```

No host shell and no Linux/BSD userspace is involved.

---

# VFS / filesystem separation

The intended ownership boundary is:

```text
userspace
   ↓
filesystem-related syscalls
   ↓
VFS
   ↓
filesystem backend (RAMFS or BoringFS)
   ↓
block-device interface (BoringFS only)
   ↓
storage driver
   ↓
disk
```

Responsibilities should remain separated:

- **syscall layer:** validates userspace arguments and buffers;
- **VFS:** paths, mounts, open handles, CWD, generic node semantics;
- **RAMFS:** volatile file/directory storage only;
- **BoringFS:** BoringFS object/allocation/directory semantics and on-disk validation;
- **block layer:** generic block I/O and bounds;
- **VirtIO driver:** device-specific transport only.

A filesystem must not reach directly into process tables, terminal code, or a particular disk controller.

---

# Security and corruption gates

Before persistent BoringFS writes are enabled, all of these must already be real:

- kernel/user privilege separation;
- separate process address spaces;
- checked syscall boundary;
- validated user-memory copies;
- overflow-safe kernel allocation;
- block-device bounds checking;
- BoringFS metadata range checking;
- complete writable-mount structural validation;
- host corruption tests;
- QEMU integration tests using disposable images.

Networking is still unrelated and remains deferred. A filesystem milestone is not a reason to open the networking gate.

---

# What is explicitly not scheduled early

Do not combine these with the shell/filesystem path:

- networking;
- USB;
- audio;
- GPU acceleration;
- BoringWM;
- browser work;
- package repositories;
- filesystem journaling;
- snapshots;
- encryption;
- ACLs;
- broad physical-hardware storage support.

Each deserves a separate milestone after the kernel foundations it needs genuinely exist.

---

# Exact next implementation milestone

## BoringKernel physical-memory milestone

The next implementation task should do **only** this:

1. add the Limine memory-map request required by BoringKernel;
2. validate the returned memory-map response;
3. normalize usable ranges to 4096-byte page boundaries;
4. implement a small physical page-frame allocator;
5. support allocate/free of individual 4-KiB frames or a deliberately tiny equivalent interface;
6. add development checks for alignment, range, duplicate allocation, and invalid frees where practical;
7. add a QEMU boot self-test proving allocation/free/reuse;
8. retain the existing serial boot identity and boot acceptance test;
9. report usable/managed page counts truthfully;
10. stop.

Do **not** add in that milestone:

- kernel heap;
- general virtual-memory manager;
- IDT/interrupts;
- timer;
- scheduler;
- processes;
- ring 3;
- syscalls;
- VFS;
- RAMFS;
- BoringFS code;
- shell code;
- storage drivers.

The purpose is one narrow proof:

> BoringKernel can safely discover and allocate physical 4-KiB page frames from the machine memory it actually owns.
