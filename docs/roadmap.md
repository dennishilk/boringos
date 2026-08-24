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
    ↓
controlled CPL0 → CPL3 transition + TSS.RSP0 exception return
    ↓
real x86_64 SYSCALL/SYSRETQ boundary
    ↓
validated ELF64 x86_64 ET_EXEC userspace loader from a Limine boot module
    ↓
PMM-owned PT_LOAD mappings + W^X/NX + real BSS zeroing
    ↓
real ELF entry execution at CPL3 using existing GETPID/DEBUG_WRITE via SYSCALL/SYSRETQ
    ↓
BoringOS-owned freestanding C userspace runtime
    ↓
_start → int boring_main(void) with no host libc/CRT
    ↓
bounded userspace CONSOLE_WRITE/CONSOLE_READ through kernel-only COM1
    ↓
real named-pipe serial input → SYSRETQ → same compiled C program resumes
```

BoringKernel **0.0.14-dev** now has a deliberately constrained real Ring 3 path, a native x86_64 `SYSCALL` / `SYSRETQ` boundary, a real constrained ELF64 x86_64 `ET_EXEC` userspace loader, a BoringOS-owned freestanding C userspace runtime, and a bounded early userspace serial-console path. A task remains an execution/scheduling entity; a process remains an identity plus address-space owner. General user-task scheduling is still not implemented.

PID 0 represents the inherited bootstrap/kernel address space. New process roots are allocated from PMM. PML4 slots 0–255 are process-private in the current bootstrap model, while slots 256–511 are shared from the bootstrap root so kernel code/data, HHDM, heap, task stacks, interrupt code and scheduler state remain available in every current process address space.

The process layer owns only page-table frames allocated specifically for that process. It never frees the inherited bootstrap/Limine root, shared higher-half tables, VMM-owned shared tables, or page tables belonging to another process.

The merged-main process acceptance proof creates PID 1 and PID 2, maps the same lower-half virtual address to different physical frames, performs real CR3 switches, dereferences the same VA through each active address space, and then lets the real PIT scheduler switch between two process-owned CPU-bound tasks with no `task_yield()` calls.

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

Milestone 9 adds one fixed lower-half user code page and one fixed user stack page, a BoringKernel-owned CPL0/CPL3 GDT, a loaded 64-bit TSS with a dedicated 16-KiB RSP0 stack, a real `iretq` transition to CPL3 and a real CPL3 `cli` instruction that produces #GP / vector 13. The normalized exception-frame user RSP/SS values are checked against the hardware privilege-transition frame, and the handler/frame locations are verified inside the TSS.RSP0 stack. Shared higher-half mappings are checked for effective supervisor-only access across the complete page-table path.

Milestone 10 adds the first controlled userspace/kernel call boundary using real x86_64 `SYSCALL` / `SYSRETQ`, a dedicated 16-KiB trusted syscall kernel stack, a provisional register ABI, `GETPID`, bounded debug-only `DEBUG_WRITE`, validated `copy_from_user`, negative BoringOS error returns and checked `SYSRETQ` return state. The final CPL3 `cli` acceptance fault still returns through the independent TSS.RSP0 exception path.

Milestone 11 adds a strict ELF64 little-endian x86_64 `ET_EXEC` loader whose bytes arrive through a Limine boot module rather than a filesystem. Validated `PT_LOAD` contents are copied into PMM-owned process pages, W+X is rejected, NX is enabled and enforced for non-executable user mappings, real BSS bytes are zeroed, a separate user stack is mapped, the ELF entry executes at CPL3, and the existing `GETPID` / `DEBUG_WRITE` syscalls return through `SYSRETQ`. The final CPL3 `cli` still produces a real #GP through the TSS.RSP0 exception path.

Milestone 12 adds the BoringOS-owned freestanding C userspace runtime. Its `_start` glue invokes `int boring_main(void)`, the runtime links without a host libc/CRT, exposes native `GETPID` and `DEBUG_WRITE` wrappers, and supplies tiny `memcpy`, `memset` and `strlen` helpers. A real compiled static `ET_EXEC` C program executes at CPL3, performs real `SYSCALL` / `SYSRETQ` round trips, returns normally from `boring_main()`, and then retains the existing final CPL3 `cli` → #GP → TSS.RSP0 privilege proof.

Milestone 13 adds bounded early userspace serial-console I/O through provisional `CONSOLE_WRITE` syscall 2 and `CONSOLE_READ` syscall 3. Transfers are limited to 64 bytes, COM1 remains kernel-only, writes use bounded `copy_from_user`, and reads validate the complete writable destination before any COM1 RX byte is consumed and then use bounded `copy_to_user`. RX remains deliberately blocking/polled. The real QEMU named-pipe acceptance runs a compiled C program that writes `console write from BoringOS userspace`, receives exactly host-injected `K` / ASCII 75 through BoringKernel, resumes the same C program through `SYSRETQ`, echoes `K`, returns 43 from `boring_main()`, and still finishes with the existing CPL3 `cli` → #GP → TSS.RSP0 proof.

All earlier PMM, VMM, heap, IRQ/EOI, cooperative task, full-GPR preemption, process/address-space, Divide Error, Page Fault, Ring 3, syscall, ELF userspace, native C runtime and userspace serial-console acceptance checks remain green.

For the deliberately legacy bootstrap timer proof, QEMU remains:

```text
-M q35 -cpu qemu64,apic=off -m 128M
```

The current serial-console path is **not** a TTY, line discipline, stdin/stdout/stderr, file-descriptor layer, asynchronous I/O path, UART IRQ receive path, or scheduler-aware blocking-I/O design. There is still no general user-task scheduling, VFS, RAMFS, block-device stack, BoringFS implementation, init, shell, networking, SMP or modern APIC timer path. ELF dynamic linking, `PT_INTERP`, `PT_DYNAMIC`, runtime relocations and PIE / `ET_DYN` are also not implemented.

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

Milestone 8 by itself did **not** prove userspace execution or isolation from privileged kernel code. Milestone 9 adds the first narrowly controlled Ring 3 transition while deliberately leaving general userspace and syscalls for later work.

## Milestone 9: ring 3 transition — COMPLETE

Implemented, merged and accepted on `main` in QEMU:

- install a BoringKernel-owned GDT with kernel code/data, DPL3 user data/code and one 64-bit available TSS;
- use selectors `0x08` kernel CS, `0x10` kernel SS, `0x1B` user SS, `0x23` user CS and `0x28` TR/TSS;
- load and verify the TSS with `ltr` / `str`;
- provide one dedicated, statically allocated, 16-byte-aligned 16-KiB TSS.RSP0 kernel stack for the acceptance path;
- map fixed lower-half user code at `0x0000000040000000` as present + user + read-only/executable;
- map fixed lower-half user stack at `0x0000000040010000` as present + user + writable, with initial RSP `0x0000000040011000`;
- propagate and verify `U/S=1` through every required PML4 → PDPT → PD → PT level for those user mappings;
- preserve the copied shared PML4 entries and verify no existing shared higher-half leaf is effectively reachable from CPL3 through a fully user-enabled page-table path;
- enter CPL3 through a real five-word `iretq` frame;
- execute a real position-independent user payload that records live CS/RSP and exercises the mapped user stack;
- execute privileged `cli` at CPL3 with IOPL zero and receive a real General Protection Fault on vector 13 with error code zero;
- require the saved fault RIP to match the copied CPL3 `cli` instruction;
- centralize CPL3-origin validation in the exception-frame API;
- verify saved CS/SS RPL3 state and require normalized saved user RSP/SS to match the hardware-pushed privilege-transition RSP/SS words exactly;
- verify both the exception frame and live C handler RSP are inside the dedicated TSS.RSP0 stack while the saved user RSP is outside it;
- preserve the existing scheduler design rather than introducing user-task scheduling;
- retain and pass the complete PMM, VMM, heap, exception, IRQ, cooperative-task, timer-preemption, process/address-space, CR3-switching, Divide Error and Page Fault regression surface.

The final implementation was squash-merged as:

```text
b77b7e6d4f4b277d04e15b2a71a717a38716d6c6
```

Merged-main verification completed successfully in GitHub Actions:

```text
BoringKernel boot test
Run #96
Event: push
Branch: main
Result: SUCCESS
```

Milestone 9 proves a real, narrowly controlled x86_64 CPL0 → CPL3 transition and a real CPL3 → CPL0 exception transition through TSS.RSP0. It does **not** implement a syscall boundary, `SYSCALL`/`SYSRET`, an ELF loader, a userspace runtime, user-task scheduling, or a general userspace execution environment.

## Milestone 10: system call boundary / syscall ABI — COMPLETE

Implemented, merged and accepted on `main` in QEMU:

- use native x86_64 `SYSCALL` / `SYSRETQ` as the sole syscall mechanism;
- enable `IA32_EFER.SCE` and program/read back `IA32_STAR`, `IA32_LSTAR` and `IA32_FMASK`;
- keep the syscall entry path separate from the existing IDT/TSS exception path;
- abandon the untrusted user RSP before any normal kernel stack use or C call;
- switch to a dedicated statically allocated 16-byte-aligned 16-KiB supervisor-only bootstrap syscall stack;
- use the provisional register ABI `RAX` number, `RDI/RSI/RDX/R10/R8/R9` arguments, `RAX` result, with `RCX/R11` clobbered;
- implement `GETPID` from the current process identity;
- implement bounded debug-only `DEBUG_WRITE` without introducing file descriptors or a TTY abstraction;
- validate complete user ranges and copy page-by-page through the current process address space into a kernel-owned buffer before serial output;
- return negative BoringOS-specific `ENOSYS`, `EFAULT` and `EINVAL` values;
- validate canonical lower-half user RIP/RSP, active process/address space, selectors and sanitized return RFLAGS before `SYSRETQ`;
- reject unmapped lower-half, higher-half kernel and overflowing user ranges without kernel-mode page faults;
- reject oversized debug writes and unknown syscall numbers safely;
- prove multiple real `SYSCALL`/`SYSRETQ` round trips, restored user RSP and preserved callee-saved registers;
- finish with a real CPL3 `cli` → #GP proof that still enters through the independent TSS.RSP0 exception stack;
- retain the complete normal, Divide Error, Page Fault and Ring 3 regression surface.

The final implementation was squash-merged as:

```text
212eb7498d66188aae70e013f80299e6b156e4dd
```

Merged-main verification completed successfully in GitHub Actions:

```text
BoringKernel boot test
Run #104
Event: push
Branch: main
Result: SUCCESS
```

Milestone 10 establishes a real but deliberately provisional syscall boundary. The ABI is not stable, and this milestone does **not** start ELF loading, libc/CRT/userspace runtime work, VFS/RAMFS/BoringFS, user-task scheduling, networking, display work or SMP/APIC work.

---

# Stage 4 — load and run native BoringOS programs

## Milestone 11: ELF userspace loader — COMPLETE

Implemented, merged and accepted on `main` in QEMU:

- obtain the deterministic smoke executable from a Limine boot module, not from a filesystem abstraction;
- accept only a deliberately constrained ELF64, little-endian, x86_64, `ET_EXEC` subset;
- validate ELF/program-header bounds, overflow-safe file and virtual ranges, program-header count, segment count and page limits before image allocation;
- load only validated `PT_LOAD` segments and reject unsupported dynamic/PIE forms;
- require the constrained fixed-address alignment policy and reject higher-half or overlapping user segments;
- reject writable + executable segments under W^X;
- require CPU NX support, enable `IA32_EFER.NXE` without clobbering unrelated EFER state, and enforce executable vs NX user mappings;
- map executable segments RX, read-only non-executable segments R+NX, writable data segments RW+NX, and the separate user stack RW+NX;
- allocate process image pages from PMM and copy executable bytes out of Limine module memory into PMM-owned process pages rather than executing the module in place;
- track loader-owned physical frames explicitly and roll back partial mappings/frames on failure;
- zero the real BSS tail where `p_memsz > p_filesz` and verify it before entering userspace;
- require the ELF entry point to resolve inside an executable loaded segment;
- create a real process with an independent address-space root and preserve shared higher-half supervisor-only mappings;
- enter the ELF header entry point at CPL3 using the existing Ring 3 machinery;
- reuse only the existing `GETPID` and bounded debug-only `DEBUG_WRITE` syscall ABI through real `SYSCALL` / `SYSRETQ`;
- prove `SYSRETQ` resumes the loaded ELF at CPL3 after both calls;
- finish with a real CPL3 `cli` → #GP / vector 13 and verify exception entry through the existing TSS.RSP0 stack;
- clean up the ELF process mappings, owned frames and process address space after the acceptance;
- retain the complete normal boot, Divide Error, Page Fault, Ring 3 and syscall regression surface.

The accepted ELF artifact is a 13,176-byte static `ET_EXEC` with entry `0x0000000040000000` and three `PT_LOAD` segments:

```text
0x40000000 - 0x400000EB  filesz=235  memsz=235  R-X
0x40001000 - 0x40001021  filesz=33   memsz=33   R--
0x40002000 - 0x40002108  filesz=96   memsz=264  RW-
```

The final implementation was squash-merged as:

```text
92ceb628b44dd9718c68567a5d2e9c191d8f1bcb
```

Merged-main verification completed successfully in GitHub Actions:

```text
BoringKernel boot test
Run #117
Event: push
Branch: main
Commit: 92ceb628b44dd9718c68567a5d2e9c191d8f1bcb
Result: SUCCESS
boot-qemu: SUCCESS
```

Milestone 11 proves real constrained ELF64 x86_64 userspace execution from a Limine boot module through PMM-owned process mappings and the existing syscall boundary. It does **not** add libc/CRT, a C userspace runtime, dynamic linking, `PT_INTERP`, `PT_DYNAMIC`, relocations, PIE / `ET_DYN`, user-task scheduling, VFS/RAMFS/BoringFS, storage, networking or display work.

## Milestone 12: minimal native userspace runtime — COMPLETE

Implemented, merged and accepted on `main` in QEMU:

- add BoringOS-owned freestanding C userspace entry glue with `_start -> int boring_main(void)`;
- link the runtime and smoke program without host libc or CRT;
- provide native `GETPID` and `DEBUG_WRITE` wrappers over the existing x86_64 syscall ABI;
- provide tiny BoringOS-owned `memcpy`, `memset` and `strlen` helpers;
- build a real compiled static ELF64 x86_64 `ET_EXEC` C userspace program;
- execute that program at CPL3 in an independent process address space;
- perform real `SYSCALL` / `SYSRETQ` round trips from compiled C;
- return normally from `boring_main()` with acceptance value 42;
- continue in runtime `_start` after the C return;
- finish with the existing real CPL3 `cli` → #GP / vector 13 through TSS.RSP0;
- preserve all earlier artifact and QEMU acceptance gates.

Milestone 12 establishes a minimal native C execution environment for BoringOS programs. It is deliberately **not** a host libc/CRT, full libc, dynamic loader, POSIX runtime, general userspace scheduler, VFS or filesystem API.

## Milestone 13: early userspace serial console — COMPLETE

Implemented, merged and accepted on `main` in QEMU:

- keep provisional syscall 0 `GETPID` and syscall 1 `DEBUG_WRITE` unchanged;
- add provisional syscall 2 `CONSOLE_WRITE` and syscall 3 `CONSOLE_READ`;
- bound each console transfer to 1..64 bytes;
- keep all COM1 port access in the kernel;
- validate and copy complete userspace write ranges through bounded `copy_from_user` before any COM1 TX output;
- validate the complete userspace read destination as present, userspace and writable before consuming any COM1 RX byte;
- receive through deliberately blocking/polled COM1 RX and copy received bytes to userspace through bounded `copy_to_user`;
- use a separate compiled C `console-smoke.elf` linked from the same BoringOS-owned runtime objects as Milestone 12;
- use a real QEMU named serial pipe for bidirectional acceptance rather than pre-seeded stdin;
- wait for the compiled C program to write `console write from BoringOS userspace`, then inject exactly `K` from the host;
- receive `K` as ASCII 75 in BoringKernel and copy it into a real local C stack variable;
- return through `SYSRETQ` to the same compiled C program and echo `K` through `CONSOLE_WRITE`;
- return 43 normally from `boring_main()`;
- retain the final real CPL3 `cli` → #GP / vector 13 → TSS.RSP0 privilege-transition proof;
- preserve all earlier artifact and QEMU acceptance gates.

The Milestone 13 console is intentionally **not** a TTY, line discipline, stdin/stdout/stderr layer, file-descriptor API, asynchronous I/O design, UART IRQ receive path, or scheduler-aware blocking-I/O mechanism. Those abstractions remain future work.

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

Milestone 14 has **not** been started in this roadmap closeout.

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

## Milestone 14 — VFS core

The next roadmap item is **Milestone 14**.

It has **not** been started by this roadmap reconciliation and requires a separate implementation change.

The intended Milestone 14 scope remains deliberately limited to filesystem-independent kernel objects, bounded path walking, mount relationships, kernel-internal open handles and process working directories. Do not jump ahead from this VFS foundation to:

- RAMFS or BoringFS implementation;
- block devices, partitions or persistent storage;
- file descriptors, stdin/stdout/stderr or file-related userspace syscalls;
- boring-init or boring-shell;
- POSIX compatibility;
- networking, display/input, APIC migration or SMP.
