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
bounded ANSI line editor + history + readdir-backed completion
    ↓
real CWD/identity/process inspection + exit/wait/reap/respawn
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
BoringKernel 0.0.48-dev
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
13 INFO (versioned bounded system-information structure)
14 GETCWD
15 PROCESS_SNAPSHOT
16 EXIT
17 WAITPID
18 FD_OPEN
19 FD_READ
20 FD_WRITE
21 FD_CLOSE
22 INPUT_CLAIM
23 INPUT_READ
24 INPUT_RELEASE
25 MEMORY_ALLOC
26 MEMORY_FREE
27 BUFFER_CREATE
28 BUFFER_MAP
29 BUFFER_UNMAP
30 BUFFER_CLOSE
31 SERVICE_REGISTER
32 SERVICE_CONNECT
33 SERVICE_ACCEPT
34 IPC_SEND
35 IPC_RECEIVE
36 IPC_CLOSE
37 BUFFER_INFO
38 FRAMEBUFFER_CLAIM
39 FRAMEBUFFER_PRESENT
40 FRAMEBUFFER_RELEASE
41 EVENT_WAIT
42 PTY_CREATE
43 SPAWN
```

VFS-backed executable loading and standalone `/bin/boringfetch` are real
since Milestone 28. Milestone 29 adds the bounded per-process descriptor and
standard-I/O foundation plus standalone `/bin/cat`; Milestone 30 adds optional
kernel framebuffer output; Milestone 31 adds bounded native PS/2 keyboard/mouse
events and exclusive blocking userspace input ownership. Milestone 32 adds
bounded dynamic anonymous Ring-3 memory, the minimal native userspace heap and
generic kernel-owned shared byte buffers with process-local capability handles,
multiple alias mappings and process-exit reclamation. Milestone 33 adds the bounded
native service registry, blocking connection-oriented IPC and transactional grants
of existing M32 shared-buffer capabilities between processes. Milestone 34 adds the
native Ring-3 `boring-display` service, shared-buffer-backed surfaces, deterministic
software composition, controlled kernel presentation and a real M31-driven cursor.
M35 adds native Ring3 BoringWM, bounded master/stack tiling, focus, reorder and
graceful close, with syscall 41 providing generic bounded readiness only.
M36 adds generation-safe bounded PTY descriptors, scheduler-owned VFS `SPAWN`
with explicit stdio and guarded generic stacks, plus the native Ring3 graphical
terminal launched by real BoringWM Super+Return. There is still no partition
layer, networking, POSIX TTY/job-control model or general desktop supervision.
M37 completes native desktop session startup without extending the ABI: PID 1 is the sole userspace boot module, starts `/bin/boring-display` and `/bin/boringwm` from persistent BoringFS through existing syscall 43 `SPAWN`, waits for and reaps both children, and remains alive after the real graphical terminal session drains. There is still no partition layer, networking, POSIX TTY/job-control model, restart policy or general desktop supervision.
M38 adds bounded PID 1 desktop-session supervision without extending syscall numbering: `WAITPID(0, status)` waits for any waitable direct child while exact-PID `WAITPID` remains unchanged. PID 1 tracks the display/WM session through STARTING, RUNNING, FAILED/DRAINING and DRAINED, reaps both central children regardless of exit order, and survives normal, WM-first and display-first drains. There is still no restart policy, general service manager, login manager, signals, networking, audio or M39 work.

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

At the completion of Milestone 11, executable loading from VFS/RAMFS did
**not** exist yet.

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

## Milestone 24: BoringFS mutation support — COMPLETE

Add bounded synchronous mutation through the proven QEMU raw disk → modern VirtIO → block device → BoringFS → VFS → checked syscall → PID 2 shell path. The intended user surface is `cat`, `touch`, replacement-style `write` and `rm`, while existing directory commands and the read-only mount contract remain intact.

The writer uses deterministic first-fit allocation, persistent bitmap/object/directory updates, reusable deleted slots and blocks, and one-block regular-file replacement. It deliberately has no journal or crash-consistency guarantee; ordinary reported write failures are rolled back where possible and every acceptance image is independently revalidated.

## Milestone 25: persistent native root filesystem — COMPLETE

Boot through a persistent BoringFS root into native userspace:

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

Milestone 25 keeps the proven ISO boot path and attaches one explicit raw
BoringFS root image through modern VirtIO. It does not invent a partition
layer or mislabel the raw filesystem as a self-contained boot image. The
human-runnable bundle and its CI acceptance use the same two-file topology.

Acceptance record:

```text
final PR: #36
merged main: 89b86f39758ddd37296e096ee6b300ec6b034169
```

---

# Security and corruption gates

Before persistent BoringFS writes are enabled, BoringOS must already have real privilege separation, separate address spaces, checked syscalls, validated user-memory copies, bounded kernel allocation, block-device bounds checks, BoringFS structural validation, host corruption tests and disposable QEMU integration tests.

Networking remains unrelated and deferred.

---

# Stage 10 — native system identity

## Milestone 26: native `boringfetch` — COMPLETE

The native-C `boringfetch` command runs inside the real ring-3
`boring-shell`. Static OS, architecture, root-filesystem and shell identity is
owned by BoringOS; usable and currently free physical memory come from one
versioned, bounded `INFO` syscall structure populated by the PMM. No CPU,
SMBIOS or network value is fabricated.

This first implementation is deliberately linked into the native shell ELF.
It is not claimed to be a standalone `/bin/boringfetch` executable.
VFS-backed executable loading remains a future milestone rather than being
faked through a host shadow or boot-module path.

Acceptance record:

```text
final PR: #37
merged main: b4e9e68c7d1a3d48cf0039158564ae1d11457e64
```

---

# Stage 11 — interactive native shell

## Milestone 27: interactive shell and real session lifecycle — COMPLETE

Milestone 27 turns the existing ring-3 `boring-shell` into a bounded
interactive environment while preserving the established syscall, VFS,
RAMFS, BoringFS, VirtIO and persistent-root architecture.

Implemented shell behavior includes:

- a 512-byte bounded editable line with Left, Right, Home, End, Backspace and
  Delete;
- complete consumption of CSI/SS3 terminal controls, including unsupported
  sequences, so suffix bytes cannot become command text;
- a volatile 16-command history with Up/Down navigation and `history` output;
- command-name completion plus type-aware path completion from real VFS
  `readdir` results, with common-prefix and buffer-bound handling;
- the real `boring@boringos:<cwd>$ ` prompt, where identity is kernel-owned
  single-user metadata and `<cwd>` comes from the current process;
- `clear`, `pwd`, `echo`, `hostname`, `uname`, `whoami`, `ps`, `history`,
  `exit` and `logout`, in addition to the existing filesystem commands and
  `boringfetch`;
- line-oriented `write <path> <text>` with one trailing newline by default,
  and exact no-newline bytes through `write -n`;
- Boringfetch v2 fields for real hostname, user, root metadata, PMM memory and
  live process count/current PID; PIT-derived uptime is shown only when the
  active boot mode supplies initialized timer statistics.

`EXIT` now terminates the launched shell, unloads its ELF mappings and leaves
a non-runnable zombie with a preserved status. PID 1 resumes, uses `WAITPID`
to validate and reap that exact child, releases its address space and CWD,
then launches a fresh shell. Process-table slots are reusable only after reap;
PID values remain monotonically allocated and are not reused in M27.
`logout` is intentionally the same shell-session termination operation as
`exit`: BoringOS has identity strings, but no authentication, authorization,
login manager or Unix permission model.

The historical corruption root cause was deterministic: the old editor
consumed only the ESC byte and allowed remaining printable escape-sequence
bytes into the parsed command. The 1,400-command pacing/burst test did **not**
prove UART FIFO overflow. The bounded UART RX queue remains defensive
reliability hardening, not the claimed root-cause fix.

No canonical Nebby reference was available in the repository or workspace,
so M27 retains the existing BOR artwork rather than inventing a mascot.

M27 does not add persistent history, credentials, permissions, a TTY/FD
layer, VFS-backed executable loading, networking, graphics or BoringWM.

---

# Stage 12 — filesystem-backed native programs

## Milestone 28: VFS-backed executable loading and standalone `boringfetch` — COMPLETE

Milestone 28 generalizes the validated static ELF64 loader around one bounded
source interface. The established Limine boot-module path is now a memory
source adapter and regular VFS files use a bounded VFS source adapter; both
feed the same validator, mapping rules and rollback path.

Accepted M28 behavior includes:

- static freestanding little-endian x86_64 `ET_EXEC` ELF64 with bounded,
  validated `PT_LOAD` segments and no writable+executable mapping;
- regular-file execution through VFS without a whole-file kernel shadow copy;
- native foreground `LAUNCH` / `EXIT` / `WAITPID` semantics with a separate
  Ring-3 child address space, zombie status preservation and explicit reap;
- a bounded BoringOS argc/argv ABI: at most 16 arguments, at most 1024 copied
  argument bytes in total, with `RDI=argc` and `RSI=argv` at userspace entry;
- fixed shell resolution of bare program names through `/bin/<name>`, while a
  command containing `/` is treated as an explicit path; there is no `PATH`;
- the former shell-integrated `boringfetch` removed in favor of a standalone
  `/bin/boringfetch` ELF stored in real persistent BoringFS;
- command completion for regular files discovered from real `/bin` VFS
  directory entries;
- repeated child launch, exit-status propagation, wait/reap and prompt return;
- malformed/non-ELF VFS execution rejection without a process/zombie leak; and
- successful `/bin/boringfetch` execution again after reboot from the same
  persistent BoringFS image.

The human-runnable QEMU bundle is seeded with the same standalone
`boringfetch.elf`, and CI audits that artifact in addition to the historical
userspace ELF gates.

The program model remains deliberately narrow. M28 does not add a dynamic
linker, shared libraries, POSIX `fork`/`exec`, signals, job control, pipes,
redirection, configurable `PATH`, permissions, a package manager, framebuffer,
networking or BoringWM.

Pre-closeout semantic freeze:

```text
feature head: 9664d5ae8118308f68a75faa95998d7b244aa216
exact-head verification: Run 32887074825 / SUCCESS
```


---

# Stage 13 — native descriptor I/O

## Milestone 29: native file descriptors and stdio foundation — COMPLETE

Milestone 29 adds a deliberately bounded BoringOS-native per-process descriptor
layer without turning the userspace into POSIX. Every normal userspace process
owns exactly 16 descriptor slots. `fd 0` is readable console input, `fd 1` is
writable console output and `fd 2` is writable console error; regular files use
slots 3 through 15 and retain existing VFS handles with independent offsets.
New `LAUNCH` children receive a fresh standard-descriptor set and do not inherit
arbitrary regular-file descriptors from their parent.

The syscall ABI extends the existing 0-17 surface without renumbering it:

```text
18 FD_OPEN
19 FD_READ
20 FD_WRITE
21 FD_CLOSE
```

One `FD_READ` or `FD_WRITE` transfers at most 4096 bytes. `FD_SEEK`, descriptor
duplication, pipes, redirection, TTY semantics, asynchronous I/O and POSIX
`fork`/`execve` remain deliberately outside M29.

The former shell-integrated `cat` command is now a real freestanding static
`/bin/cat` ELF64 `ET_EXEC` stored beside `/bin/boringfetch` in persistent
BoringFS. Its data path uses only `FD_OPEN`, `FD_READ`, `FD_WRITE(1, ...)` and
`FD_CLOSE`, followed by explicit `EXIT`; the ELF audit rejects legacy
`FS_READ`/`CONSOLE_WRITE` call edges. The QEMU acceptance proves regular-file
fd allocation, sequential read/write, EOF, close, child exit/zombie/wait/reap,
controlled missing-file failure, explicit `/bin/cat`, preserved
`/bin/boringfetch`, and no leaked child zombie.

Dedicated CPL3 syscall acceptance also proves invalid-fd rejection, READ on
stdout rejection, WRITE on stdin rejection, invalid userspace pointer
rejection and the 4096-byte transfer bound. Descriptor host tests cover the
standard slots, table-full behavior, slot reuse, independent offsets, access
modes, child isolation and teardown of retained handles.

Semantic freeze acceptance record:

```text
feature head: 950da2d9e0b373f29c0bf8817720e9f29b1b3d20
exact-head verification: Run 32916196094 / SUCCESS
final version after closeout: BoringKernel 0.0.30-dev
```

M29 does not add `FD_SEEK`, `dup`/`dup2`, pipes, shell redirection, job control,
signals, sockets, networking, permissions, authentication, a dynamic linker,
shared libraries, `mmap`, nonblocking/asynchronous I/O, poll/select/epoll, TTY
line discipline, PTYs, framebuffer/input work, BoringWM or Milestone 30 work.

---

# Stage 14 — native graphics foundation

## Milestone 30: native framebuffer graphics foundation — COMPLETE

Milestone 30 introduces optional validated Limine RGB framebuffer discovery, a
BoringOS-owned bounded software-rendering surface, project-owned 5x7 printable
ASCII pixel text and a one-shot kernel-rendered boot/status dashboard. The
surface accepts validated 24-bpp and 32-bpp RGB layouts, uses checked geometry,
pitch, byte-size and offset arithmetic, and clips every drawing primitive before
writing. Explicit text-coordinate overflow guards and UINT64_MAX/canary tests
close the final bounds-hardening edge found before semantic freeze.

The real persistent-root acceptance renders the dashboard after VirtIO-backed
BoringFS is mounted, captures the actual QEMU display through QMP `screendump`,
parses and semantically validates the PPM, and then proves that serial userspace,
`/bin/boringfetch`, descriptor-backed `/bin/cat`, BoringFS tooling and the full
historical suite remain operational. Missing, unsupported or malformed
framebuffer metadata remains non-fatal and falls back to the authoritative
serial-only boot path.

Semantic freeze acceptance record:

```text
feature head: 45baa4f2f62daee0e15c35909b7c6c1408a4378d
exact-head verification: Run 32919539003 / SUCCESS
freeze dashboard: 1024x768x32, visually inspected from exact-run artifact
```

M30 graphics remain kernel-only. Input is intentionally deferred; Milestone 30
does not implement keyboard, mouse, an input subsystem, graphics syscalls,
userspace framebuffer mapping, `boring-display`, a terminal graphics stack, GUI
clients, BoringWM or Milestone 31 work.


---

# Stage 15 — native input foundation

## Milestone 31: native keyboard and mouse input foundation — COMPLETE

Milestone 31 adds a bounded BoringOS-owned native input path for the current
QEMU x86_64 reference machine. The i8042 controller initializes translated PS/2
Set-1 keyboard input and three-byte PS/2 mouse packets with bounded waits,
explicit ACK/RESEND handling and non-fatal unavailable-device fallback. IRQ1 and
IRQ12 are routed through the existing legacy PIC/IDT path without changing the
historical timer-only acceptance contract.

Hardware bytes are normalized into the fixed 24-byte BoringOS input-event ABI.
Keyboard events use project-owned keycodes, make/break state, Shift/Ctrl/Alt/Super
modifier bits and an explicit repeat flag. Mouse events expose relative X/Y and
left/middle/right button transitions. The kernel owns one fixed 128-event FIFO;
overflow drops the newest event and increments a saturating dropped-event
counter.

Userspace access is deliberately exclusive and narrow:

```text
22 INPUT_CLAIM
23 INPUT_READ
24 INPUT_RELEASE
```

One PID owns the input stream at a time. Same-owner claim is idempotent, another
PID receives busy/access errors, explicit release clears queue/key state, and
process exit releases stale ownership before the parent resumes. `INPUT_READ`
accepts 1..16 events, validates the complete writable destination before any
dequeue, and blocks in the kernel when the queue is empty. On the current
single-CPU trusted-SYSCALL-stack design the wait arm and empty check run with
interrupts disabled and `sti; hlt` closes the empty-to-sleep race; wakeup always
rechecks the queue. Userspace does not busy-poll.

The standalone static `/bin/input-test` exercises the real Ring-3 wrappers and
prints normalized keyboard/mouse events. Host tests cover keyboard decoding,
E0/malformed recovery, modifiers/repeat, mouse sign/button/overflow/resync,
queue FIFO/wrap/overflow/ownership/cleanup, and real CPL3 negative syscall
cases. The permanent QEMU acceptance injects actual QMP keyboard and mouse
input, proves Super+Q, Super+Enter, relative movement, left-button down/up,
blocking wakeup and owner teardown/reclaim while preserving the complete
historical BoringOS suite and M30 framebuffer acceptance.

Semantic freeze acceptance record:

```text
feature head: 065fa085ee7534012037e9eab9b3cfbdcacec39e
feature tree: b9687ac96c097fba2e262206ffee25d64946b88c
exact-head permanent CI: Run 32946932189 / SUCCESS
final version after closeout: BoringKernel 0.0.32-dev
```

M31 does not add a cursor, framebuffer mapping for userspace, `boring-display`,
a terminal graphics stack, GUI clients, BoringWM, USB input, general HID, SMP
input routing or Milestone 32 implementation. The next roadmap target is the
native display/compositor/window-system foundation; it is not part of M31.


---

# Stage 16 — dynamic userspace memory foundation

## Milestone 32: dynamic userspace memory and shared buffers — COMPLETE

Milestone 32 adds real bounded anonymous data memory to ordinary Ring-3 processes. New PMM frames are explicitly zero-filled before they become user-accessible and are mapped writable + NX through the existing checked Ring-3 mapper. Per-process allocation state is fixed at 32 metadata slots, individual anonymous allocations are bounded to 16 MiB, and partial failures roll back rather than publishing incomplete state.

The freestanding runtime now has a minimal native heap (`boring_malloc`, `boring_calloc`, `boring_free`) backed by the new anonymous-memory ABI. It uses 16-byte alignment, first-fit reuse, splitting and same-arena coalescing without introducing libc or a POSIX memory contract.

M32 also adds generic kernel-owned shared byte-buffer objects. A process receives a generation-bearing process-local capability handle; the same handle can be mapped multiple times to create real aliases of the same backing frames. Handle references and mapping references have independent lifetimes: closing the handle does not invalidate existing mappings, and storage is reclaimed only after the final handle/mapping reference disappears. Process exit explicitly reclaims forgotten anonymous allocations, mappings and handles.

The new provisional ABI slots are exactly:

```text
25 MEMORY_ALLOC
26 MEMORY_FREE
27 BUFFER_CREATE
28 BUFFER_MAP
29 BUFFER_UNMAP
30 BUFFER_CLOSE
```

The standalone static `/bin/memory-test` proves the real Ring-3 path, including zero-fill, allocation/read-write, shared-buffer aliasing, handle-vs-mapping lifetime, negative cases and exit reclamation. Permanent CI contains the memory-test build audit, the M32 kernel/runtime host tests and real QEMU memory acceptance while preserving the full historical fixture and regression suite. The historical 64-block BoringFS fixture geometry remains unchanged unless `/bin/memory-test` is deliberately seeded into the enlarged fixture.

Semantic Freeze acceptance record:

```text
implementation SHA: a494a800818f14bb375bee5674dcd27d8bfb82ee
implementation tree: 4dd1b01c40d49a8447fd7fc52a047b7a40383d9e
exact-head Semantic Freeze CI: Run #338 / 32959154779 / SUCCESS
final version after closeout: BoringKernel 0.0.33-dev
```

M32 buffers are generic bytes only. Milestone 32 does **not** add cross-process buffer/capability transfer, IPC, a service registry, display or surface semantics, cursor work, `boring-display`, a compositor, BoringWM or terminal work. Those boundaries remain deferred; Milestone 33 was not started as part of M32.


---

# Stage 17 — native IPC and service foundation

## Milestone 33: native IPC and service foundation — COMPLETE

Milestone 33 adds a deliberately bounded, connection-oriented native IPC layer
above the existing process and scheduler foundations. A fixed global registry
owns bounded kernel copies of service names; process-local generation-protected
listener/endpoint handles carry authority, and PIDs do not.

`SERVICE_ACCEPT` and `IPC_RECEIVE` block without busy polling. The existing
cooperative process-bound 16-KiB task stack becomes that process's trusted
SYSCALL stack while it runs, allowing a blocked syscall to sleep and a different
process to enter the kernel safely. Wakeups cover pending connections, queued
messages and peer-close transitions without weakening the established scheduler
or introducing timer-preemptive userspace scheduling.

M33 extends the syscall ABI only in the previously free slots 31..36:

```text
31 SERVICE_REGISTER
32 SERVICE_CONNECT
33 SERVICE_ACCEPT
34 IPC_SEND
35 IPC_RECEIVE
36 IPC_CLOSE
```

Inline control payloads are bounded to 256 bytes. The only transferable
capability is an existing M32 shared-buffer capability. Transfer is grant/copy,
not move: the sender retains its local handle, a queued message retains the
kernel-owned backing object, and a successful receive transaction installs a new
receiver-local generation-protected handle referring to the same backing pages.
Failed sends/receives do not partially enqueue, dequeue or duplicate capability
state.

Process exit closes remaining IPC handles, unregisters owned listeners, wakes
peers/waiters, releases pending/queued IPC state and retained M32 buffer
references, then proceeds through the established M32 userspace-memory cleanup.
The three-process CPL3 acceptance proves three distinct process address spaces,
service register/connect/accept, blocking wakeups, FIFO/queue-full transaction
behavior, negative syscall paths, shared-buffer grants and aliases, peer close,
service removal/re-registration, process-local handle isolation, resource
reclamation and PMM recovery.

Acceptance record:

```text
PR: #44
base: 97abc3849145a66d84e938bc51d0f5c5d4670f41
Semantic Freeze SHA: 72ba384fa46c23a7115940da47e6197adb11a2cb
Semantic Freeze tree: 3191d784a65ef067a3469738245a1d16492767e7
exact-head Semantic Freeze CI: Run #399 / 32976425779 / SUCCESS
final version after closeout: BoringKernel 0.0.34-dev
```

The permanent QEMU bundle keeps the historical fixture geometries intact:
legacy fixtures remain 64 blocks, M32's `/bin/memory-test` fixture remains 80
blocks, and only the M33 bundle including `/bin/ipc-test` uses 96 blocks.

M33 does **not** add a display/surface protocol, framebuffer ownership transfer,
cursor, compositor, `boring-display`, terminal graphics stack, BoringWM or GUI
application. Those remain outside the M33 Semantic Freeze.


---

# Stage 18 — native display service foundation

## Milestone 34: native boring-display service — COMPLETE

Milestone 34 adds the first BoringOS-native Ring-3 display service as the
freestanding `/bin/boring-display` program registered under `boring.display`.
The physical framebuffer remains kernel-owned. Syscalls 37..40 add authoritative
buffer-size queries plus exclusive framebuffer claim, controlled XRGB8888 present
and release operations without exposing physical addresses, HHDM aliases or kernel
pointers to userspace. Normal process exit also releases a forgotten framebuffer
claim.

The service uses M33 IPC for its bounded versioned control protocol and M32 shared
buffers for pixel backing. Generation-protected surface tokens are scoped to the
owning IPC endpoint. The fixed-capacity compositor clears a dark background,
composes surfaces in deterministic creation order, draws a clipped software cursor
last and presents one full frame through the kernel boundary.

The real QEMU acceptance runs `boring-display`, display client A and display client
B as three separate CPL3 processes with three distinct address spaces. It proves
M32 capability grants and same-backing live update, cross-client token rejection,
creation-order overlap, real QMP-to-PS/2-to-M31 mouse delivery, both edge clips, a
deterministic 1280x800 pixel-validated framebuffer, process/device/IPC/input/buffer
cleanup and PMM recovery while retaining every historical M0..M33 gate.

The complete eight-program BoringFS bundle is measured from the real ELF files.
The inherited IPC program proves that 80 blocks are insufficient; all M34 programs
fit, validate and round-trip byte-for-byte in the existing 96-block M33 geometry,
so M34 does not force an unnecessary 112-block expansion.

Acceptance record:

```text
PR: #45
base: 8b80c3f1e19532ade884290268021707b0a552fe
Semantic Freeze SHA: 2c9e7994ec5c41c4f481f8e55fd4ea45ae621905
Semantic Freeze tree: a874ac876797a2e4ace1328b8d5da3ec0e3ed4ab
exact-head Semantic Freeze CI: Run #417 / 32996081201 / SUCCESS
final version after closeout: BoringKernel 0.0.35-dev
```

M34 does **not** add BoringWM, window placement, focus or tiling policy,
workspaces, terminal graphics, X11, Wayland, GPU acceleration or a general
window-system ABI. Those remain outside the M34 Semantic Freeze.


---

# Stage 19 — native window-management policy

## Milestone 35: native BoringWM — implementation COMPLETE, Semantic Frozen

Native C Ring3 `boring.wm` manages six generation-safe, endpoint-owned clients.
The display retains all pixel composition and the real cursor; apps retain M32
buffers. The master receives 3/5 of usable width, with deterministic stack
remainders, gaps 4/8 and borders 3. New-client, keyboard and real mouse focus,
adjacent reorder, graceful close and unsolicited exit all retile deterministically.
Super+Return only records terminal unavailable. No terminal, workspace system,
floating, bar, alternate layout or M37 startup integration is included.

Five real CPL3 processes with distinct address spaces prove actual M33/M32/M34
operation and QMP -> PS/2 -> i8042 -> M31 -> Ring3 input. Five independently
validated 800x600 frames cover focus, reorder, close and exit. A separate WM-death
acceptance proves display/app survival and kernel resource cleanup. Host tests
perform 1,115,824 checks. Strict freestanding ELF audits and all inherited gates
remain. The exact 13-program BoringFS superset uses 96 blocks, with 20 free;
all older fixtures and the original M34 service/acceptance are retained.

See [native-boringwm.md](native-boringwm.md) and [RUNNING-M35.md](RUNNING-M35.md).


## M35 Semantic Freeze record

- Base: `e7ac44c437624386cb4a5666cfe1e446a696c643`
- Freeze SHA: `154344d2bc4e136b7e53c473b35b42dcb7a41348`
- Freeze tree: `36504b61c61c46fb2c438e868ca4050b0527fa6e`
- Full exact-head CI: **Run #425 / 33039917519 / SUCCESS**
- Event: `pull_request`; branch: `agent/native-boringwm`; version: `0.0.35-dev`
- PR: [#46](https://github.com/dennishilk/boringos/pull/46)

All intended M35 semantics and all M0–M34 gates passed before this freeze.
The temporary workflow is absent. Closeout changes only active version
witnesses to **BoringKernel 0.0.36-dev** and documentation; no WM semantics.
Final-head CI, guarded squash, exact merged-main push CI and artifact hashes
are recorded in the PR's post-freeze verification trail. No M36 work is included.


---

# Stage 20 — native graphical terminal

## Milestone 36: native boring-terminal — implementation COMPLETE, Semantic Frozen

M36 adds syscall 42 `PTY_CREATE` and syscall 43 `SPAWN` without changing slots
0–41. Up to eight generation-checked PTY pairs each own two 4096-byte rings;
blocking reads, HUP wakeup, queued-byte drain, EPIPE and teardown are bounded.
`SPAWN` loads a VFS ELF into an independent address space, installs explicit
stdin/stdout/stderr and publishes a scheduler task only after complete success.
Every scheduled executable uses the same two-page RW/NX stack, one unmapped
lower guard, 16-byte-aligned argc/argv, at most 16 arguments and at most 1024
argument bytes. The original one-page terminal failure is reproduced at stack
address `0x4000ffa8`; exhaustive host layout coverage and real Ring3 blocking
wakeup prove the generic fix.

The native C `/bin/boring-terminal` owns its parser/grid/renderer, M32 pixels,
display surface and WM token. Real QMP -> PS/2 -> M31 -> WM Super+Return launches
the BoringFS ELF, whose PTY slave starts a separate real `boring-shell`; that
shell uses `SPAWN` for the real `/bin/boringfetch`. Full 800x600 pixel oracles
prove the prompt, fetch output, two terminals, focus B/A/B/A and strict input
isolation. Super+Q drains the focused terminal/shell pair and retiles the
survivor. Separate test-only modes kill a terminal or issue shell `exit`; each
requires an independently usable survivor and zero final PTY/process/task/IPC/
input/framebuffer/M32 resources.

The eight-process/eight-task limits are unchanged. Display + WM + two terminals
+ two shells use six ordinary slots; one foreground command peaks at seven.
WM clients remain bounded at six, display surfaces at sixteen, descriptors at
sixteen per process and PTYs at eight. The production BoringFS image contains
only `boring-terminal` (27,232 bytes / 7 blocks), `boring-shell` (26,936 / 7)
and `boringfetch` (15,288 / 4). Thirty-four filesystem blocks are sufficient
and thirty-three are rejected; all historical fixture geometries remain intact.

Permanent CI retains every M0–M35 gate and adds PTY/FD/shell/SPAWN host tests,
generic stack exhaustion, real Scheduler/Ring3 PTY/SPAWN, terminal/shell/fetch
ELF audits, measured M36 BoringFS, normal/dual/death graphical modes, corruption
oracle, exact-bundle reboot and visual/bundle artifacts. See
[RUNNING-M36.md](RUNNING-M36.md) and
[process-startup-stack.md](process-startup-stack.md).


## M36 Semantic Freeze record

- Base: `d8975a293a2518269a1cd38bee0b24fcdf2a3830`
- Freeze SHA: `6019d05bf266f049d36cf624753bfa82f4714984`
- Freeze tree: `15f8355cd19d5e951b7bb48508ab80b3bcd4b3d0`
- Full exact-head CI: **Run #464 / 33078951287 / SUCCESS**
- Event: `pull_request`; branch: `agent/native-boring-terminal`; version: `0.0.36-dev`
- PR: [#47](https://github.com/dennishilk/boringos/pull/47)

All intended M36 semantics and every M0–M35 gate passed before this freeze.
Temporary diagnostic/transport paths are absent. The GitHub sanitizer gate is
green and unchanged; the local runtime's ptrace `/proc` restriction prevented
LSan startup rather than reporting a leak. Closeout changes only active version
witnesses to **BoringKernel 0.0.37-dev** and documentation. M37 is not included.

---

# Stage 21 — native desktop session startup

## Milestone 37: native desktop session startup — implementation COMPLETE, Semantic Frozen

M37 turns the already-real M36 desktop into a real PID 1 startup path without
adding or renumbering syscalls. The ISO boots exactly one userspace module,
`/boot/user/boring-init.elf`. PID 1 mounts the real persistent BoringFS root and
uses existing syscall 43 `SPAWN` to start `/bin/boring-display` and
`/bin/boringwm` as ordinary waitable children. BoringWM discovers the existing
`boring.display` service and retains the M35/M36 policy and protocol unchanged.

The exact QEMU acceptance drives the existing real QMP -> PS/2 -> M31 input path,
opens `/bin/boring-terminal` with Super+Return, runs a separate PTY-backed
`/bin/boring-shell`, executes `/bin/boringfetch`, validates exact 800x600
framebuffer pixels, proves two terminals plus focus/input isolation, and closes
the complete session with Super+Q. PID 1 waits for and reaps BoringWM and display;
all seven desktop children are finished/reaped while PID 1 remains the sole live
userspace process/task. Final accounting proves zero IPC services/connections/
queued messages/attachments, zero M32 active objects, no input owner, no
framebuffer claim and zero PTY pairs/references/waiters/queued bytes.

The measured M37 BoringFS bundle is 46 blocks and rejects 45 blocks. `boringfsck`
reports VALID. The contained programs are `/bin/boring-display`, `/bin/boringwm`,
`/bin/boring-terminal`, `/bin/boring-shell` and `/bin/boringfetch`; the ISO audit
proves exactly one userspace boot module. The syscall ABI remains exactly 0..43;
legacy `LAUNCH` is not used for the M37 desktop startup path.

## M37 Semantic Freeze record

- Base: `6bbdb94bd0f0a7990af9a37e7a93eef58073f0b9`
- Freeze SHA: `823510dbb9be69abdaa0e90e783c11738c7252c3`
- Freeze tree: `b60d7e3f91f1af2f4591c87361ba6c37a13b03a7`
- M37 exact-head CI: **Run #13 / 33170928371 / SUCCESS**
- Permanent regression CI: **BoringKernel boot test Run #479 / 33170928387 / SUCCESS**
- Event: `pull_request`; branch: `agent/native-desktop-session-startup`; version: `0.0.37-dev`
- Final evidence artifact: `boringos-m37-desktop-reference`, ID `9685574167`
- Artifact ZIP SHA-256: `20a72174806f47458a9da2c1b3ac9e950df72a1a68c8bc512845580573f1109e`
- ISO SHA-256: `e8c752181d52a288eebb285327f6f54b84867f1c52194a13eb2d76e6c897bc5c`
- BoringFS root SHA-256: `a2970b431a9d6d81051aa3b76bbbdba54d494250e4b0896ae6044224cd1716bf`
- PR: [#48](https://github.com/dennishilk/boringos/pull/48)

All intended M37 runtime semantics and every M0–M36 regression gate passed on
the exact freeze head before closeout. Closeout changes only the active current
version witnesses to **BoringKernel 0.0.38-dev** and this documentation. No M38
implementation is included.

---

# Stage 22 — bounded desktop session supervision

## Milestone 38: native desktop session supervision — implementation COMPLETE, Semantic Frozen

M38 keeps the M37 desktop architecture and syscall numbering intact while making
PID 1 a bounded two-child desktop-session supervisor. Existing syscall 17
`WAITPID` gains the compatible selector `WAITPID(0, status)` for any waitable
direct child; exact-PID waits retain their previous contract. PID 1 records the
display and WM child identities, observes whichever central child exits first,
reaps each exactly once, drains the surviving desktop stack through existing
IPC/PTY/buffer/input/framebuffer teardown, and remains alive after the session.

Real QEMU acceptance covers the normal M37-compatible lifecycle plus deterministic
unexpected WM-first and display-first Ring3 exits. Every scenario proves concrete
reap order/status, one surviving PID1 process/task, complete IPC/M32/input/
framebuffer/PTY resource drain, and zero resume/fault residue. No restart loop or
general service-manager semantics are introduced. See
[m38-session-supervision.md](m38-session-supervision.md).

## M38 Semantic Freeze record

- Base: `2662aa44dc00e2d1b1577576f27522554291316d`
- Freeze SHA: `56b80bacf52e04b8f7b2f89830f578cfb9da2765`
- Freeze tree: `c4db3501b52a21d63cfad2d1dd678585eb11fcb6`
- M38 exact-head supervision CI: **Run #6 / 33181145205 / SUCCESS**
- M37 exact-head regression: **Run #39 / 33181145272 / SUCCESS**
- BoringKernel full regression: **Run #505 / 33181145165 / SUCCESS**
- Event: `pull_request`; branch: `agent/native-desktop-session-supervision`; frozen version: `0.0.38-dev`
- Freeze evidence artifact: `boringos-m38-desktop-reference`, ID `9689752268`
- Artifact ZIP SHA-256: `7eb5d4d49cbdc6db443e31b25fcdc451946fed0f7758737f6b29bf02b319589d`
- PR: [#49](https://github.com/dennishilk/boringos/pull/49)

All intended M38 runtime semantics and every inherited regression gate passed on
the exact freeze head before closeout. Closeout changes only this roadmap and the
active current-version witnesses to **BoringKernel 0.0.39-dev**. No M39
implementation is included.


## Milestone 39: native BoringEdit — COMPLETE, Semantic Frozen

`/bin/boring-edit [path]` is a native Ring3 boring.display client, managed by
BoringWM and launched through the existing shell SPAWN path. It provides a
bounded 4096-byte plain ASCII/LF document, visible cursor, letters, spaces,
newlines, backspace and horizontal cursor movement. Ctrl+S saves to real
BoringFS through existing file syscalls; FD_OPEN/READ/CLOSE load documents.
No path starts empty with the visible `/untitled.txt` save destination.
Oversized/non-text files are rejected before writes; failed saves retain the
dirty document. Dirty close waits for save-and-close or explicit discard.

Real QEMU acceptance proves the existing terminal and editor tiled together,
keyboard-driven editing, exact framebuffer pixels, independent Ring3 cat,
shorter rewrites with exact persisted bytes, an empty saved file, failed-save
retention, and complete IPC/input/framebuffer/shared-buffer/PTY/task/process
drain to PID1 only. No syscall ABI change or new kernel storage implementation.
Save remains non-atomic under the current truncate/write API.

Semantic Freeze: `332d4a9f235fb219cd0e2cc3798217044c4531aa`.
Tree: `0e80ba24b0ac32c71f1fc678097bef672e537e0b`.
Exact-head SUCCESS: M39 #1 / 33186371956; M37 #45 / 33186371793;
M38 #12 / 33186371785; full Boot #511 / 33186371797.

This runtime-neutral closeout advances active current-version witnesses to
**BoringKernel 0.0.40-dev**. Exact-head closeout CI and guarded merge are
required before M40 begins. See [native-boringedit.md](native-boringedit.md)
for operation, bounds, acceptance, and the human-runnable QEMU command.


## Milestone 40: native BoringFiles — COMPLETE, Semantic Frozen

`/bin/boring-files [directory]` is a native Ring3 boring.display client managed
by BoringWM. It displays real bounded VFS readdir entries, directory/file types,
current canonical directory and selection. Up/Down select; Enter navigates or
spawns `/bin/boring-edit <path>` with the existing detached SPAWN contract;
Backspace navigates to the parent; R refreshes. Directory scratch uses the
existing private userspace allocator, keeping the unchanged 16-page ELF budget.

Real QEMU acceptance proves guest-created nested directories and text files,
child/parent navigation, file selection, terminal/Files/Edit simultaneously
with independent surfaces and focus, editing and saving, reopening from Files,
independent Ring3 cat, exact persisted `hello world` bytes, and full desktop
resource drain to PID1 only. No syscall, display ABI or kernel changes.

Semantic Freeze: `aeba543bba9b5fd91b5c447a616c73738f00d277`.
Tree: `c0b3af8e2fc52d94c74be1956ee90616dbd41314`.
Exact-head SUCCESS: M40 #1 / 33188714799; M39 #4 / 33188713720;
M38 #15 / 33188713568; M37 #48 / 33188713596; full Boot #514 / 33188713599.

This runtime-neutral closeout advances active version witnesses to
**BoringKernel 0.0.41-dev**. Exact-head closeout CI and guarded merge must pass
before M41 begins. See [native-boringfiles.md](native-boringfiles.md).


## Milestone 41: BoringWM application shortcuts — COMPLETE, Semantic Frozen

Super+Return launches `/bin/boring-terminal`, Super+E `/bin/boring-edit`, and
Super+F `/bin/boring-files` through existing detached SPAWN. Only exact Super
key-down events launch; repeats, key releases and extra modifiers are ignored.
Missing executables report unavailable without placeholder windows. Existing
focus, tiling, reorder, graceful Super+Q and unsaved-editor semantics remain.
No new binary, syscall ABI or display/WM wire protocol.

Real QEMU acceptance launches three independent Ring3 clients by shortcuts,
checks focused terminal/editor input and Files navigation in actual framebuffer
pixels, saves exact `edit` bytes, closes each client with Super+Q and drains all
resources to PID1. Additive host tests cover mappings and modifier/repeat guards.

Semantic Freeze: `e1bfa94c2909e155acb2ad2d23b85ae752efff3f`.
Tree: `c9742ea6cb6f82300b5a588a79af121b009dd00b`.
Exact-head SUCCESS: M41 #1 / 33190350272; M40 #4 / 33190350188;
M39 #7 / 33190350202; M38 #18 / 33190350190; M37 #51 / 33190350187;
complete Boot #517 / 33190350219.

This runtime-neutral closeout advances active version witnesses to
**BoringKernel 0.0.42-dev**. Exact-head closeout CI and guarded merge remain
required before M42 begins. See [m41-application-shortcuts.md](m41-application-shortcuts.md).


## Milestone 42: native desktop client foundation — COMPLETE, Semantic Frozen

The real Terminal/Edit/Files clients now share a small BoringOS-owned C helper
for display/WM connection, bounded surface buffer mapping, surface publication,
checked IPC envelopes, commit, unregister and release. Each application retains
its own EVENT_WAIT/input policy, text model, existing bitmap renderer, PTY or
file I/O and unsaved-document decisions. No new executable, kernel change,
syscall ABI, wire protocol or widget framework.

The three app main files shrink from 1,177 to 625 lines. Shared implementation:
178 C + 30 header lines. Real QEMU proves all three apps simultaneously,
focused input, exact saved bytes and complete PID1-only resource drain; the
original M39/M40 independent cat/save/navigation scenarios also pass. The new
host transport test covers 21 injected failures and malformed envelope/lifecycle
cases. Actual framebuffer evidence is retained by the permanent M42 workflow.


## Semantic Freeze

Implementation: `7c9054e8c0c339c1c8a168c85bc0d6998b4708c8`.
Tree: `04ff69250b70850f698bb5cbde4aafac38e8f383`.
All exact-head gates SUCCESS: M42 #1 / 33192303081; M41 #5 / 33192303109;
M40 #8 / 33192303056; M39 #11 / 33192302977; M38 #22 / 33192302988;
M37 #55 / 33192302974; complete Boot #521 / 33192303044.
This separate closeout changes only documentation and active version witnesses.
Exact-head closeout CI and guarded merge remain required.

Active version after this runtime-neutral closeout: **BoringKernel 0.0.43-dev**.
M43 must wait for exact-head closeout CI, guarded squash and main-push SUCCESS.
See [m42-client-foundation.md](m42-client-foundation.md).


## Milestone 43: real boot CPU CPUID inventory — COMPLETE, Semantic Frozen

A bounded BoringOS-owned collector executes actual x86_64 CPUID and records
vendor, optional brand, family/model/stepping, initial APIC ID, raw relevant
feature registers and maximum addressable logical IDs per package. Unsupported
leaves are not queried and validity flags distinguish absent data. At most
eight CPUID queries, fixed strings, no allocation, no userspace ABI change.
Serial output reports the boot CPU's actual inventory; no QEMU identity constants
exist in the production collector and no advertised feature is enabled by it.

Real QEMU acceptance requires complete normal boots on two single-CPU variants,
including deliberately varied vendor/family/model/stepping, plus an explicitly
inventory-only four-CPU probe. Host fixtures cover bounds, absence and decoding.
The preexisting four-CPU PIC/PIT runtime stall was reproduced on the M42 base;
there is no SMP-runtime or real-PC readiness claim.


## Semantic Freeze

Implementation: `2973b2bba0a86da0332008c1feacb0201baca791`.
Tree: `bd0b4cf05027c020be55459206bcf0e90cd1f30c`.
Exact-head SUCCESS: M43 #1 / 33194111893; M42 #5 / 33194111742;
M41 #9 / 33194111733; M40 #12 / 33194111694; M39 #15 / 33194111690;
M38 #26 / 33194111726; M37 #59 / 33194111685; complete Boot #525 / 33194111719.
This separate closeout changes only documentation and active version witnesses.
Exact-head closeout CI and guarded merge remain required.

Active version after runtime-neutral closeout: **BoringKernel 0.0.44-dev**.
M44 must wait for closeout CI, guarded merge and main-push SUCCESS.
See [m43-cpuid-inventory.md](m43-cpuid-inventory.md).


## Milestone 44: real bounded PCI hardware inventory — COMPLETE, Semantic Frozen

A BoringOS-owned allocation-free collector performs read-only x86 CF8/CFC
enumeration of segment zero across all 256 buses, 32 slots and functions 1–7
only when function zero advertises multifunction. It records real numeric BDF,
vendor/device ID, class/subclass/prog-if, revision and header type. Storage is
bounded to 256 records with explicit total, truncation, config-read and partial
error state. It performs no writes, BAR sizing, bridge configuration, driver
binding or userspace ABI extension.

Host fixtures prove absent/sparse/multifunction functions, the last BDF, the
65,536-function scan bound, 256-record cap, read failures and canaries. Real
QEMU acceptance correlates the dynamically selected 1AF4:1042 VirtIO block BDF
with exactly one inventory entry while retaining actual I/O, persistence and
neighbor-sector checks. Numeric identities are evidence, not device-name or
physical-hardware support claims.

## Semantic Freeze

Implementation: `737e11b65902b5cff39a319ca7e4d7585f125c89`.
Tree: `d9f2fd550b1426d1ec7c856ed5b9735345dc2f39`.
Exact-head SUCCESS: M44 #1 / 33195749180; M43 #5 / 33195749066;
M42 #9 / 33195749078; M41 #13 / 33195749024; M40 #16 / 33195749033;
M39 #19 / 33195749009; M38 #30 / 33195749001; M37 #63 / 33195748983;
complete Boot #529 / 33195749063.
Evidence: `boringos-m44-pci-reference`, artifact 9695677267, GitHub ZIP SHA-256
`18b0e6c60b7586a7f8599d921f50e0231694ef70f25f68dea89e699d018b43cf`.
This separate closeout changes only documentation and active version witnesses.
Exact-head closeout CI and guarded merge remain required.

Active version after runtime-neutral closeout: **BoringKernel 0.0.45-dev**.
M45 must wait for closeout CI, guarded merge and main-push SUCCESS.
See [m44-pci-inventory.md](m44-pci-inventory.md).


## Milestone 45: real bounded SMBIOS platform identity — COMPLETE, Semantic Frozen

The BoringOS-owned allocation-free SMBIOS path validates 2.x and 3.x entry
points, checksums, physical table ranges, HHDM arithmetic and mapped-memory
containment. A 1 MiB / 4,096-structure parser rejects malformed lengths,
truncated structures, unterminated string sets and invalid string indices.
Fixed fields retain actual firmware vendor/version, system manufacturer/product
and board manufacturer/product. Missing optional values remain unavailable.

Bounded type-17 handling records real memory slot/presence counts, total
reported bytes and an explicit unknown-size completeness flag. There is no
Linux DMI code, userspace ABI change, hardcoded emulator identity, driver
support inference or physical-hardware readiness claim.

Host fixtures and ASan/UBSan cover valid 2.x/3.x entries, optional absence,
invalid checksums/lengths/indices/termination and table/structure limits. Real
QEMU completes normal boots on default q35 SMBIOS 3.0/64-bit firmware and a
distinct pc SMBIOS 2.8/32-bit configuration, proving actual guest table
consumption and rejecting production hardcodes.

## Semantic Freeze

Implementation: `3d8f3ec610fb8a834aeb284bf63a3a4ce0c8f6d6`.
Tree: `242876a301f8e40422debda687db05891786692b`.
Exact-head SUCCESS: M45 #1 / 33200536553; M44 #6 / 33200536479;
M43 #10 / 33200536471; M42 #14 / 33200536492; M41 #18 / 33200536477;
M40 #21 / 33200536563; M39 #24 / 33200536516; M38 #35 / 33200536498;
M37 #68 / 33200536559; complete Boot #534 / 33200536623.
Evidence: `boringos-m45-smbios-reference`, artifact 9697574284, GitHub ZIP
SHA-256 `222e1853781abd6c4656a3aa5aafdb7b590e7b5d48943b378760b5365cca89cc`.
This separate closeout changes only documentation and active version witnesses.
Exact-head closeout CI and guarded merge remain required.

Active version after runtime-neutral closeout: **BoringKernel 0.0.46-dev**.
M46 must wait for closeout CI, guarded merge and main-push SUCCESS.
See [m45-smbios-platform.md](m45-smbios-platform.md).


## Milestone 46: boringfetch hardware edition — COMPLETE, Semantic Frozen

The standalone Ring 3 `boringfetch` now renders real bounded facts already
owned by BoringOS. Versioned `INFO` ABI v3 copies a fixed 1,024-byte snapshot
containing CPUID vendor/brand/signature fields, optional SMBIOS system/board/
firmware and memory facts, the complete PCI count plus at most eight numeric
samples, active framebuffer geometry and initialized VirtIO block geometry.
Availability and completeness bits keep absent or partial data explicit; no
kernel pointers, device-name database, driver inference or fabricated hardware
crosses the syscall boundary.

Host tests prove exact uppercase numeric formatting, omitted unavailable facts,
bounded sample counts and unterminated-string rejection. Real QEMU acceptance
cross-checks every visible hardware line against independent boot inventory in
an exact-pixel framebuffer capture after `Super+Return`. The same gate proves
the shipped terminal, BoringEdit and BoringFiles shortcuts, isolated focus,
persisted editor bytes, graceful close and final PID-1-only resource drain.

## Semantic Freeze

Implementation: `2d10161debaa964f872b2f287382773d81dc2b8f`.
Tree: `0b506f0d61f9f89e081c765b07408b4835d5472c`.
Exact-head SUCCESS: M46 #1 / 33203162251; M45 #5 / 33203162242;
M44 #10 / 33203162037; M43 #14 / 33203162020;
M42 #18 / 33203162188; M41 #22 / 33203162095;
M40 #25 / 33203162058; M39 #28 / 33203161993;
M38 #39 / 33203161997; M37 #72 / 33203162049;
complete Boot #538 / 33203161996.
Evidence: `boringos-m46-boringfetch-reference`, artifact 9698590846, GitHub
ZIP SHA-256
`4686494b2987012ca6a3a2b4bdcb61c4b151dc5a6024b5b1301ea3253269f8d6`.
This separate closeout changes only documentation and active version witnesses.
Exact-head closeout CI and guarded merge remain required.

Active version after runtime-neutral closeout: **BoringKernel 0.0.47-dev**.
M47 must wait for closeout CI, guarded merge and main-push SUCCESS.
See [m46-boringfetch-hardware.md](m46-boringfetch-hardware.md).


## Milestone 47: real-hardware boot-readiness boundary — COMPLETE, Semantic Frozen

The exact platform audit classifies boot, memory, framebuffer, boot-CPU
inventory, PCI inventory, SMBIOS, input, storage and desktop without promoting
emulator evidence into a physical-machine claim. The result is a
**REAL-HARDWARE-READY CANDIDATE** only for a deliberately legacy-assisted UEFI
configuration and is **NOT PHYSICALLY VERIFIED**.

A small platform-neutral PMM correction now accepts valid Limine maps larger
than its fixed capacity, manages at most 1,048,576 frames (4 GiB), reports the
cap and leaves excess usable memory unmanaged. Host tests and sanitizers prove
the cap while malformed, overlapping and overflowing maps still fail closed.

Four hashed OVMF scenarios prove normal 5-GiB UEFI boot with the explicit PMM
cap, legacy i8042 + VirtIO desktop completion, a controlled xHCI-only input
failure and a controlled AHCI-only persistent-root failure. ACPI is not
consumed; legacy PIC/PIT, CF8/CFC PCI, PS/2 input and modern VirtIO block remain
the implemented platform contracts. Numeric xHCI/AHCI inventory is not driver
support.

The first interactive blocker selects M48 as the smallest correct xHCI/USB-HID
foundation. AHCI/NVMe root storage remains a separate explicit blocker.

## Semantic Freeze

Implementation: `db2a403f90dafbb1686f295a1547aa0d548f43eb`.
Tree: `2849e0c0a80baa283ac08531abaabbde0ea3746f`.
Exact-head SUCCESS: M47 #1 / 33205793084; M46 #5 / 33205793000;
M45 #9 / 33205792981; M44 #14 / 33205793040;
M43 #18 / 33205793008; M42 #22 / 33205793032;
M41 #26 / 33205793445; M40 #29 / 33205793017;
M39 #32 / 33205793101; M38 #43 / 33205793036;
M37 #76 / 33205792967; complete Boot #542 / 33205793042.
Evidence: `boringos-m47-readiness-reference`, artifact 9699625549, GitHub ZIP
SHA-256 `581ab9de170984279fff882be30a223156d0bd20393ae90cbc3b6914e2d9a596`.
This separate closeout changes only documentation and active version witnesses.
Exact-head closeout CI and guarded merge remain required.

Active version after runtime-neutral closeout: **BoringKernel 0.0.48-dev**.
M48 must wait for closeout CI, guarded squash and main-push SUCCESS.
See [m47-real-hardware-readiness.md](m47-real-hardware-readiness.md).
