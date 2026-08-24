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
```

The accepted development banner is now:

```text
BoringKernel 0.0.17-dev
```

The current syscall ABI remains exactly:

```text
0 GETPID
1 DEBUG_WRITE
2 CONSOLE_WRITE
3 CONSOLE_READ
```

There is still no numeric file-descriptor table, no stdin/stdout/stderr model, no userspace file-content syscall API, no executable loading from VFS/RAMFS, no BoringFS/block-storage implementation, no networking, no display/input stack, no BoringWM integration, no APIC migration and no SMP.

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

Native x86_64 `SYSCALL` / `SYSRETQ` is the sole userspace call boundary. The bootstrap ABI provides `GETPID`, bounded `DEBUG_WRITE`, validated user copies, deterministic negative native errors and checked user return state on a dedicated trusted syscall stack.

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

Implemented and accepted behavior includes:

- nested directories;
- real heap-backed regular-file bytes;
- stable VFS node identity;
- real `lookup`, `create`, `mkdir`, `read`, `write`, `truncate`, `readdir`, `rename`, `unlink` and `rmdir` behavior;
- VFS mount integration, including a second RAMFS instance through the existing mount model;
- process CWD use through retained VFS references;
- bounded node/file/data capacity and cleanup/bookkeeping proof.

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

Accepted behavior:

- real ELF64 x86_64 static `ET_EXEC` built from the existing BoringOS-owned C runtime;
- delivered as a Limine boot module and loaded through the existing constrained ELF loader;
- real deterministic PID 1;
- independent PMM-backed process address space;
- entry at CPL3 with existing W^X/NX and supervisor-only higher-half rules;
- userspace proves PID 1 through existing `GETPID`;
- output uses existing `CONSOLE_WRITE` only;
- no new syscall was added for Milestone 16;
- PID 1 remains alive/long-lived rather than inventing an exit/reaping model.

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

Milestone 16 did **not** add a shell, file descriptors, file syscalls, executable loading from VFS/RAMFS, BoringFS, storage, networking, display/input, APIC migration or SMP.

## Milestone 17: `boring-shell` — NEXT / NOT STARTED

Launch a native C shell from `boring-init` in userspace.

The defining contract remains:

> Filesystem commands operate through real VFS/syscall state. No simulated directory listings.

Milestone 17 has **not** been started by this roadmap reconciliation. It requires a separate implementation branch/PR.

---

# Stage 7 — BoringFS host tooling before kernel writes

## Milestone 18: pure BoringFS format codec and validator — PLANNED

Implement the documented BoringFS v0 encoding/decoding rules in small C modules suitable for host tests and later kernel reuse where practical.

## Milestone 19: `mkboringfs` — PLANNED

Create deterministic valid BoringFS images containing an empty root directory.

## Milestone 20: `boringfsck` — PLANNED

Create a read-only structural validator/inspector. Initial versions report corruption and return non-zero; they do not attempt aggressive repair.

---

# Stage 8 — persistent block I/O

## Milestone 21: generic block-device layer — PLANNED

Create a filesystem-independent bounded block-device interface.

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

# Exact next implementation milestone

## Milestone 17 — `boring-shell`

The next roadmap item is **Milestone 17**.

It is **NOT STARTED** by this reconciliation.

The required core contract is:

```text
boring-init
    ↓
native C boring-shell at CPL3
    ↓
userspace command parsing
    ↓
BoringOS syscall boundary
    ↓
process CWD + existing VFS
    ↓
real RAMFS namespace
```

Filesystem commands must manipulate and observe the **real** landed VFS/RAMFS state. No hard-coded listings, shell-local fake filesystem, kernel-print substitute or other simulated directory state is acceptable.

Milestone 17 remains separate from BoringFS, block storage, package management, networking, display/input, BoringWM and every Milestone 18+ item.
