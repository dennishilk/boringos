# BoringOS roadmap — boot kernel to native shell and BoringFS

This roadmap tracks the **actual repository state**. Planned work is not implemented work.

## Current verified state

```text
QEMU x86_64
    ↓
Limine
    ↓
BoringKernel
    ↓
PMM + selected-mapping VMM + bounded kernel heap
    ↓
x86_64 IDT/exceptions + legacy PIC/PIT
    ↓
cooperative + timer-driven preemptive kernel scheduling
    ↓
process identity + independent x86_64 address spaces
    ↓
controlled CPL3 + TSS.RSP0
    ↓
native x86_64 SYSCALL/SYSRETQ
    ↓
validated static ELF64 ET_EXEC userspace loading
    ↓
BoringOS-owned freestanding C userspace runtime
    ↓
bounded userspace serial console
    ↓
filesystem-independent VFS
    ↓
real bounded mutable RAMFS
    ↓
real PID 1 boring-init at CPL3
    ↓
native PID 2 boring-shell at CPL3
    ↓
userspace filesystem syscalls
    ↓
process CWD
    ↓
VFS
    ↓
real mutable RAMFS

host-side BoringFS path:

docs/boringfs.md
    ↓
explicit little-endian BoringFS v0 codec
    ↓
read-only shared structural validator
    ↓
deterministic mkboringfs formatter
    ↓
read-only boringfsck inspector (Milestone 20 complete)

kernel storage path:

validated read-only or synchronous writable BoringFS mount at `/disk`
    ↓
generic bounded block-device layer (Milestone 21 complete)
    ↓
modern VirtIO PCI block backend (Milestone 22 complete)
    ↓
real QEMU raw disk I/O
```

The accepted development banner is now:

```text
BoringKernel 0.0.25-dev
```

The current syscall ABI is exactly:

```text
0 GETPID
1 DEBUG_WRITE
2 CONSOLE_WRITE
3 CONSOLE_READ
4 LAUNCH
5 FS_READDIR
6 FS_MKDIR
7 FS_RMDIR
8 FS_CHDIR
9 FS_READ
10 FS_TOUCH
11 FS_WRITE
12 FS_UNLINK
```

There is still no numeric file-descriptor table, no stdin/stdout/stderr abstraction, no executable loading from VFS/BoringFS, no partition layer, no persistent root filesystem, no networking, no display/input stack, no BoringWM integration, no APIC migration and no SMP.

Every milestone must keep all earlier acceptance checks green and add a focused proof for the new capability.

---

# Stage 1 — memory foundations

## Milestone 1: physical memory manager — COMPLETE

A bounded 4-KiB PMM consumes the defensively validated Limine memory map, allocates unique usable frames deterministically, supports free/reuse, rejects invalid/double frees and verifies bookkeeping.

## Milestone 2: selected BoringKernel virtual-memory control — COMPLETE

BoringKernel adopts the active x86_64 four-level paging root, uses the Limine HHDM, creates/removes selected 4-KiB mappings, translates mappings, invalidates TLB entries and reclaims only VMM-owned empty page-table frames.

## Milestone 3: kernel heap — COMPLETE

A finite PMM/VMM-backed 16-MiB virtual heap provides deterministic first-fit allocation, 16-byte payload alignment, splitting/coalescing, invalid/interior/double-free rejection and bounded page growth.

---

# Stage 2 — controlled faults, interrupts and time

## Milestone 4: exception infrastructure — COMPLETE

BoringKernel owns its x86_64 IDT and fatal exception path, including real Divide Error and Page Fault acceptance with controlled halt diagnostics.

## Milestone 5: hardware interrupt controller and periodic timer — COMPLETE

The bootstrap interrupt path uses the legacy 8259 PIC plus PIT IRQ0 on the current QEMU reference configuration and proves repeated acknowledged timer delivery.

---

# Stage 3 — execution and process foundations

## Milestone 6: kernel execution contexts + cooperative context switching — COMPLETE

Bounded kernel tasks have independent stacks, deterministic cooperative round-robin switching, callee-saved register preservation, clean return/cleanup and coexistence with the timer.

## Milestone 7: timer-driven preemptive kernel scheduling — COMPLETE

PIT IRQ0 drives real full-GPR interrupt-frame preemption between kernel tasks without cooperative yields, while preserving stack/local/register state and clean bootstrap return.

## Milestone 8: process and address-space model — COMPLETE

PID 0 represents the bootstrap/kernel process. New processes own independent PMM-backed x86_64 lower-half address-space roots while retaining required supervisor-only shared higher-half kernel mappings; scheduler-integrated CR3 switching and process cleanup are proven.

## Milestone 9: ring 3 transition — COMPLETE

BoringKernel owns CPL0/CPL3 descriptors and a 64-bit TSS with dedicated RSP0 stack, maps controlled user code/stack pages, enters CPL3 with `iretq`, and proves a real CPL3 privileged-instruction fault returns through the TSS.RSP0 exception path.

## Milestone 10: system call boundary / syscall ABI — COMPLETE

Native x86_64 `SYSCALL` / `SYSRETQ` is the sole userspace call boundary. The bootstrap ABI began with `GETPID` and bounded `DEBUG_WRITE`; later milestones extended the same checked boundary without introducing a file-descriptor layer.

---

# Stage 4 — load and run native BoringOS programs

## Milestone 11: ELF userspace loader — COMPLETE

A deliberately constrained ELF64 little-endian x86_64 `ET_EXEC` loader validates Limine boot-module images, copies `PT_LOAD` bytes into PMM-owned process pages, enforces W^X/NX and supervisor-only higher-half mappings, zeros BSS and enters the validated ELF entry at CPL3.

Executable loading from VFS/RAMFS does **not** exist yet.

## Milestone 12: minimal native userspace runtime — COMPLETE

The BoringOS-owned freestanding runtime provides `_start -> int boring_main(void)`, tiny memory/string helpers and native syscall wrappers without host libc, host CRT, a dynamic linker or PIE.

## Milestone 13: early userspace serial console — COMPLETE

Provisional syscall 2 `CONSOLE_WRITE` and syscall 3 `CONSOLE_READ` provide bounded direct userspace serial I/O through kernel-only COM1. RX remains deliberately blocking/polled; there is still no TTY, line discipline, FD layer or stdin/stdout/stderr abstraction.

---

# Stage 5 — VFS and RAMFS

## Milestone 14: VFS core — COMPLETE

The filesystem-independent VFS provides bounded iterative absolute/process-relative path walking, repeated `/`, `.` and `..`, mount traversal, retained process CWD references, kernel-internal open handles and backend dispatch for `lookup`, `create`, `mkdir`, `unlink`, `rmdir`, `rename`, `read`, `write`, `truncate` and `readdir`.

Final Milestone 14 merged main:

```text
94e7a5c7b82e9c1b83e68a304e88851400f26393
```

Merged-main verification was independently observed as BoringKernel boot test Run #139 / ID 32697392544 / push / main / SUCCESS.

## Milestone 15: RAMFS backend — COMPLETE

Milestone 15 adds a **real bounded mutable RAMFS backend** underneath the existing generic VFS rather than a synthetic test filesystem.

Implemented and accepted behavior includes nested directories, heap-backed regular-file bytes, stable VFS node identity, real VFS backend operations, mount integration, process CWD use through retained VFS references, bounded capacity and cleanup/bookkeeping proof.

Final version after Milestone 15:

```text
BoringKernel 0.0.16-dev
```

Merged main:

```text
4f84a4d5662c80a6fa85f5c97c145a82b1f54c56
```

The accepted final implementation PR was #23. Its final PR CI was Run #147 / ID 32705510281 / SUCCESS. The merged-main push run was independently observed as Run #148 / ID 32705711339 / SUCCESS on the exact merged main SHA above.

Milestone 15 did **not** add userspace filesystem syscalls, file descriptors, BoringFS, block storage, init or shell.

---

# Stage 6 — native init and shell

## Milestone 16: `boring-init` — COMPLETE

Milestone 16 adds a real freestanding native `boring-init.elf` as the first long-lived BoringOS userspace process.

Accepted behavior includes a real static ELF64 `ET_EXEC`, deterministic PID 1, an independent address space, CPL3 execution, `GETPID` proof, `CONSOLE_WRITE` output, and a long-lived `PROCESS_ALIVE` PID 1 without inventing an exit/reaping model.

Final version after Milestone 16:

```text
BoringKernel 0.0.17-dev
```

Merged main:

```text
c24cd9a0bc07ae54f1761a5002dc98586154de5f
```

Final pull-request CI:

```text
BoringKernel boot test
Run #150
Run ID: 32710178979
Result: SUCCESS
```

The merged-main push CI for Milestone 16 was not independently observable through the available connector and is **not claimed**.

## Milestone 17: `boring-shell` — COMPLETE

Milestone 17 adds a real freestanding static `boring-shell.elf` launched from real userspace `boring-init`.

Accepted architecture and behavior:

- `boring-init` is PID 1 and initiates `LAUNCH`;
- `boring-shell` becomes PID 2 in an independent address space;
- successful launch performs a trusted-syscall-stack-safe PID1 -> PID2 handoff by reusing the existing syscall return frame rather than nesting live PID1/PID2 syscall frames;
- PID 1 remains `PROCESS_ALIVE`;
- PID 2 inherits process CWD through retained VFS references;
- shell input/output use `CONSOLE_READ` / `CONSOLE_WRITE`;
- userspace filesystem syscalls are `FS_READDIR`, `FS_MKDIR`, `FS_RMDIR`, and `FS_CHDIR`;
- there is no FD table, stdin/stdout/stderr abstraction, or userspace file-content syscall API;
- shell commands are `help`, `ls [path]`, `mkdir <name>`, `rmdir <name>`, and `cd <path>`;
- shell input has a 128-byte line bound.

Real interactive acceptance proved the real namespace path rather than a shadow model:

```text
mkdir Test
ls
Test

mkdir Test
-> already exists

cd Test
mkdir Inner
ls
Inner

cd ..
rmdir Test
-> directory not empty

cd Test
rmdir Inner
cd ..
rmdir Test
ls
-> Test is no longer present
```

The mutation and later independent observation path is:

```text
boring-shell
    ↓
userspace syscall
    ↓
process CWD
    ↓
VFS
    ↓
real mutable RAMFS
```

There is no shell shadow filesystem and no fake directory listing.

Acceptance record:

```text
final implementation PR: #26
frozen green implementation: b95466a14d7372d2681471e3b0c39c55d8df65f6
final PR head: c38686138279d6276936b6f7ff80dd4779f29509
final PR CI: Run #168 / ID 32718959564 / SUCCESS
merged main: f063c2ef4a36cf759ff39814a8110a3cb13459f6
```

The push-triggered merged-main workflow was not independently observable:

```text
merged-main push CI: NOT OBSERVABLE
success: NOT CLAIMED
```

---

# Stage 7 — BoringFS host tooling before kernel writes

## Milestone 18: pure BoringFS format codec and validator — COMPLETE

Milestone 18 implements the documented BoringFS v0 binary encoding/decoding rules and a read-only structural validator as small BoringOS-owned C modules suitable for reuse by host tools and later kernel integration.

Accepted boundary:

```text
raw BoringFS v0 bytes
    ↓
explicit little-endian codec
    ↓
decoded format values
    ↓
read-only structural validator
```

Acceptance record:

```text
final PR: #28
final PR head: 8b99063b2402fc1b4a8605c70a7c0456b5be20f0
final PR CI: Run #173 / ID 32734788941 / SUCCESS
final version: BoringKernel 0.0.19-dev
merged main: 266ff57071011330480c0bae6f64306d8b1b89b3
```

The Milestone 18 merged-main push workflow was not independently observable:

```text
merged-main push CI: NOT OBSERVABLE
success: NOT CLAIMED
```

Milestone 18 did not add a kernel BoringFS mount, block-device support, persistence, formatter/checker CLI, writable BoringFS or executable loading from BoringFS.

## Milestone 19: `mkboringfs` — COMPLETE

Milestone 19 adds a deterministic host-side formatter that creates valid empty-root BoringFS v0 images through the shared codec and requires the shared structural validator to accept the completed bytes before publication.

Acceptance record:

```text
final PR: #29
frozen green implementation: d1fa22ba045d9882abeb747edf43096233b71e74
final PR head: 7afb51a8509c7098338bcb48341d0f48d53cbe6a
final PR CI: Run #176 / ID 32742096005 / SUCCESS
final version: BoringKernel 0.0.20-dev
merged main: c1c6614eb12c3942aa5f9c99741e1c7d839aaccb
```

The Milestone 19 merged-main push workflow was not independently observable:

```text
merged-main push CI: NOT OBSERVABLE
success: NOT CLAIMED
```

Milestone 19 did not add `boringfsck`, repair, kernel BoringFS, block devices, storage or a persistent root.

## Milestone 20: `boringfsck` — COMPLETE

Milestone 20 adds the first real read-only host checker around the same shared BoringFS v0 codec and structural validator. The accepted path is:

```text
mkboringfs
    ↓
real BoringFS image bytes
    ↓
read-only boringfsck
    ↓
shared structural validator
    ↓
VALID or precise CORRUPT result
```

The checker distinguishes corruption from host-I/O/resource failures, exposes shared validator locations when present, proves non-mutation of valid and corrupt images, and remains strictly inspection-only. No repair mode is part of Milestone 20.

Acceptance record:

```text
final PR: #30
final closeout head: 64fba49f8a300b12cde53e52d770d7275476ed4a
final PR CI: Run #179 / ID 32750652939 / SUCCESS
merged main: 7e64ba1cc7f1a99210cf0ad89330ece1a946ae73
merged-main push CI: NOT OBSERVABLE — SUCCESS NOT CLAIMED
```

Milestone 20 did not add a kernel BoringFS mount, block devices, storage, repair, a new syscall or an FD layer.

---

# Stage 8 — persistent block I/O

## Milestone 21: generic block-device layer — COMPLETE

Milestone 21 introduced the filesystem-independent bounded block-device interface with explicit logical geometry, a fixed registry, synchronous full-transfer read/write callbacks, overflow-safe range validation, read-only enforcement above the backend and explicit result codes.

Acceptance record:

```text
final PR: #31
frozen implementation: dc518ef24801a3b1e5d4b33387ad87ed97fc10f9
final closeout head: e63222865db5f0b4a837e0f40c9524214c430209
final PR CI: Run #182 / ID 32758293243 / SUCCESS
merged main: 8d50823ccf7d069ae9c861f644f74fe3e2c61ae4
merged-main CI: Run #183 / ID 32758477270 / push / main / 8d50823ccf7d069ae9c861f644f74fe3e2c61ae4 / SUCCESS
```

The isolated `block` QEMU test mode uses RAM-backed acceptance devices only. Milestone 21 itself did not add a hardware storage driver, BoringFS mount, partition layer, storage syscall, or FD layer.

## Milestone 22: QEMU VirtIO-block driver — COMPLETE

Milestone 22 added the first real hardware-backed storage path: modern VirtIO 1.x PCI only, bounded PCI capability discovery, explicit cache-disabled MMIO mappings, PMM-owned DMA, a split virtqueue, one synchronous polling request at a time, and registration as `vblk0` beneath the existing M21 block-device API.

The disposable QEMU acceptance uses a deterministic raw disk and proves real host-authored reads, kernel writes, read-back, multi-request bounce-buffer chunking, neighbor preservation, and independent host-side persistence after QEMU exits.

Acceptance record:

```text
final PR: #33
frozen implementation: c348c24d26fcb786cf850994043bb4568decc576
implementation CI: Run #196 / ID 32816037074 / SUCCESS
final closeout head: 67e3483f0fe46e032c4a66e77de2f538b098fd18
final exact-head CI: Run #198 / ID 32816642864 / SUCCESS
merged main: 805ebe1d8c13ca2bea197154dfd0a17a409edbc8
merged-main CI: Run #199 / ID 32816812650 / push / main / SUCCESS
```

Milestone 22 did not add a kernel BoringFS mount, partition layer, storage syscall, FD layer, storage interrupts, or asynchronous I/O.

---

# Stage 9 — BoringFS kernel integration

## Milestone 23: read-only BoringFS mount — COMPLETE

Mount a host-formatted BoringFS volume through the generic block-device boundary and VFS, rejecting invalid metadata before exposure. Milestone 23 keeps RAMFS as the root, exposes the validated persistent volume at `/disk`, and proves real PID 2 directory traversal plus kernel-side regular-file reads through BoringFS extents. Mutation remains explicitly denied in this milestone.

Acceptance record:

```text
final PR: #34
frozen implementation: 0ede929d0d144fdfb561b98a324b66979586168f
implementation CI: Run #207 / ID 32852821936 / SUCCESS
final closeout head: 693730b18ceb846b9377f473a61fc903a848ea4e
final exact-head CI: Run #208 / ID 32853262677 / SUCCESS
merged main: 254c371bcf67e74a3fb5f681bd5af9f06fcb8aa7
merged-main CI: Run #209 / ID 32853491775 / push / main / SUCCESS
```

## Milestone 24: BoringFS mutation support — CURRENT

Add bounded synchronous mutation through the proven QEMU raw disk → modern VirtIO → block device → BoringFS → VFS → checked syscall → PID 2 shell path. The intended user surface is `cat`, `touch`, replacement-style `write` and `rm`, while existing directory commands and the read-only mount contract remain intact.

The writer uses deterministic first-fit allocation, persistent bitmap/object/directory updates, reusable deleted slots and blocks, and one-block regular-file replacement. It deliberately has no journal or crash-consistency guarantee; ordinary reported write failures are rolled back where possible and every acceptance image is independently revalidated.

## Milestone 25: persistent native root filesystem — PLANNED

Eventually boot through a persistent BoringFS root into native userspace:

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

# Exact current implementation milestone

## Milestone 24 — synchronous writable BoringFS over VirtIO

The current implementation item is **Milestone 24**. Milestone 23 is verified merged; Milestone 25 remains **PLANNED** until M24 has completed its full exact-head closeout, guarded merge, and merged-main CI verification.

Its architectural boundary is:

```text
host-formatted BoringFS
          ↓
   QEMU raw disk
          ↓
 modern VirtIO PCI
          ↓
        vblk0
          ↓
 M21 block-device API
          ↓
 shared BoringFS validator + synchronous writer
          ↓
 writable BoringFS VFS
          ↓
        /disk
          ↓
 PID 2 boring-shell
```

Milestone 24 does not add file descriptors, a persistent root, partition parsing, executable loading from BoringFS, journaling, crash consistency, general append/growth, rename, or any desktop/display work.
