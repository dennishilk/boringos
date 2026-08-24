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
read-only boringfsck inspector (Milestone 20 in progress)
```

The accepted development banner is now:

```text
BoringKernel 0.0.20-dev
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
```

There is still no numeric file-descriptor table, no stdin/stdout/stderr abstraction, no userspace file-content syscall API, no executable loading from VFS/RAMFS, no kernel BoringFS backend or block-storage implementation, no networking, no display/input stack, no BoringWM integration, no APIC migration and no SMP.

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

## Milestone 20: `boringfsck` — CURRENT / IN PROGRESS

Milestone 20 adds the first real read-only host checker around the same shared BoringFS v0 codec and structural validator. Its implementation target is:

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

The checker must distinguish corruption from host-I/O/resource failures, expose shared validator locations when present, prove non-mutation of valid and corrupt images, and remain strictly inspection-only. No repair mode is part of Milestone 20.

---

# Stage 8 — persistent block I/O

## Milestone 21: generic block-device layer — PLANNED

Create a filesystem-independent bounded block-device interface. Milestone 21 has not begun.

## Milestone 22: QEMU VirtIO-block driver — PLANNED

Use VirtIO block as the first QEMU storage target unless implementation analysis finds a concrete blocker; prove bounded disposable-image read/write/read-back behavior before any persistent-root work.

---

# Stage 9 — BoringFS kernel integration

## Milestone 23: read-only BoringFS mount — PLANNED

Mount a host-formatted BoringFS volume through the generic block-device boundary and VFS, rejecting invalid metadata before exposure.

## Milestone 24: BoringFS mutation support — PLANNED

Add mutation only after read-only mounting is solid.

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

## Milestone 20 — read-only `boringfsck`

The current implementation item is **Milestone 20**. It is **CURRENT / IN PROGRESS** in this reconciliation.

Its architectural boundary is only:

```text
existing BoringFS v0 bytes
    ↓
shared codec
    ↓
shared structural validator
    ↓
read-only boringfsck presentation / exit status
```

Milestone 20 must not repair images, mount BoringFS in the kernel, add block devices/storage, alter the syscall ABI, introduce an FD layer or begin Milestone 21.
