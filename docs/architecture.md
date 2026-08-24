# BoringOS architecture — current bootstrap state

These decisions remain deliberately narrow and provisional. This document describes implemented behavior only.

## Platform and boot boundary

- **Architecture:** x86_64.
- **Reference machine:** QEMU `q35`, one `qemu64,apic=off` CPU for the current legacy PIC/PIT bootstrap path.
- **Bootloader:** Limine 12.5.2.
- **Kernel:** freestanding, statically linked ELF64, primarily C with small isolated architecture assembly.
- **Privilege:** normal kernel execution remains CPL0; dedicated acceptance paths perform real CPL0 -> CPL3 transitions, real CPL3 -> CPL0 `SYSCALL` entry with `SYSRETQ` return, and final CPL3 -> CPL0 exception transitions through TSS.RSP0. A minimal freestanding C runtime and isolated serial-console client now execute at CPL3, but there is still no scheduled or long-lived general userspace task model.
- **Bootstrap address space:** PID 0 adopts the active Limine-created four-level root.
- **Additional process address spaces:** PMM-backed independent roots with private lower-half mappings and shared higher-half kernel mappings.

The current normal boot path proves PMM, selected bootstrap-VMM mappings, the bounded kernel heap, exception infrastructure, repeated hardware PIT/PIC interrupts, cooperative kernel contexts, timer-driven preemptive kernel scheduling, process identity, real CR3 switching, and independent process address spaces. A separate Ring 3 test mode proves the descriptor/TSS state, user-accessible lower-half mappings, real `iretq` entry to CPL3, a real privileged-instruction #GP, preservation of the hardware user return state, and CPU stack switching through TSS.RSP0. A separate syscall mode proves native x86_64 `SYSCALL`/`SYSRETQ`, trusted syscall-stack entry, validated user-memory copying, and multiple CPL3 round trips. A separate ELF mode proves a validated fixed-address ELF64 x86_64 `ET_EXEC` obtained from a Limine boot module, copied into PMM-owned pages in an independent process address space, entered at its ELF entry point in CPL3, and continued through the existing syscall boundary before a deliberate final CPL3 #GP. A native-runtime mode proves a freestanding compiled C program linked only against the BoringOS-owned minimal runtime. A distinct console mode extends that same runtime with bounded bidirectional COM1 I/O while preserving controlled userspace memory boundaries.

## Physical memory

BoringKernel allocates 4096-byte physical frames only from Limine memory-map entries marked usable. A bounded static bitmap and usable-region table provide deterministic first-fit allocation, free/reuse, and invalid/double-free rejection. Bootloader-reclaimable and all other non-usable types remain non-allocatable.

All new process root tables, process-private lower-level page tables, process-isolation data frames, and the dedicated Ring 3/syscall test pages come from this existing PMM. There is no second physical allocator.

## Bootstrap VMM

The original VMM requests x86_64 four-level paging, discovers Limine's HHDM, reads the inherited root from `CR3`, and controls selected 4-KiB mappings inside the bootstrap address space.

New VMM page-table frames come from PMM and are accessed through the HHDM. Empty tables are returned to PMM only when VMM allocated them itself. The early VMM test window remains:

```text
[0xffffff0000000000, 0xffffff0000200000)
```

The QEMU self-test maps a PMM frame, translates it, performs a real write/read, unmaps it, invalidates the TLB entry, and restores PMM/VMM bookkeeping.

This inherited bootstrap root is represented explicitly as PID 0's address space. It is never freed by the process layer.

## Process address-space layer

BoringKernel 0.0.9-dev added an explicit address-space abstraction alongside the original bootstrap VMM.

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

Sharing the full current higher half preserves the mappings used by kernel code/data, HHDM physical access, heap, task stacks, PMM/VMM metadata, IDT, exception/IRQ code, scheduler metadata, GDT/TSS state, the dedicated syscall stack, and still-referenced Limine structures.

The Ring 3 mapping acceptance adds an explicit effective-permission invariant. `address_space_kernel_mappings_valid()` first requires shared PML4 entries 256-511 in the process root to remain bit-for-bit identical to the bootstrap root, proving the user mapper did not mutate the shared root. Because x86_64 user access requires `U/S=1` on every paging level, `ring3_shared_higher_half_supervisor_only()` then walks each present shared translation path whose ancestors remain user-enabled and rejects any 1-GiB, 2-MiB, or 4-KiB leaf that would be reachable with `U/S=1` at every level. An inherited `U/S=1` on an upper non-leaf entry is therefore not misreported as user access when a lower level still enforces supervisor-only access. User mapping creation remains restricted to canonical lower-half addresses below `ADDRESS_SPACE_SHARED_PML4_START`, and the effective higher-half check runs both after user mappings are installed and again after the test process root is activated.

The syscall and console acceptances reuse this effective-permission rule and validate user ranges page by page before controlled kernel copies through trusted HHDM aliases.

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

BoringKernel owns a 256-entry x86_64 IDT. Vectors 0-31 are CPU exception gates; PIC vectors 32-47 are installed later by the hardware-IRQ layer. Configured entries are DPL0 64-bit interrupt gates using BoringKernel's kernel code selector and IST 0.

The real acceptance suite continues to prove:

```text
Divide Error → vector 0  → BoringKernel diagnostic → controlled halt
Page Fault   → vector 14 → CR2/error decode       → controlled halt
```

The Ring 3 mode additionally produces a real General Protection Fault from CPL3 and routes it through the same BoringKernel-owned vector-13 gate. Fatal exceptions do not return. Double Fault still lacks a dedicated IST/emergency stack.

## GDT, TSS, and first Ring 3 transition

BoringKernel 0.0.10-dev installs its own small x86_64 GDT before building the IDT. The seven 8-byte GDT slots are:

```text
slot  selector  purpose
0     0x00      null
1     0x08      kernel code, DPL0
2     0x10      kernel data, DPL0
3     0x18      user data descriptor, DPL3  (used as selector 0x1B with RPL3)
4     0x20      user code descriptor, DPL3  (used as selector 0x23 with RPL3)
5     0x28      64-bit available TSS descriptor, low half
6     —         TSS descriptor, high half
```

The code therefore uses these public selectors:

```text
kernel CS  0x08
kernel SS  0x10
user SS    0x1B
user CS    0x23
TR/TSS     0x28
```

`descriptor_init()` disables interrupts while replacing the table, performs the architecture-specific GDT load and kernel segment reload, verifies active GDTR/CS/SS/DS, loads TR with `ltr`, and verifies TR with `str` before reporting success.

The current TSS is the architectural 104-byte x86_64 structure. Only `RSP0` is used by this milestone; `RSP1`, `RSP2`, all IST entries, and the I/O permission bitmap remain unused. `iomap_base` is set to the end of the TSS so no I/O bitmap is present.

`RSP0` points at the top of one statically allocated, 16-byte-aligned **16-KiB kernel stack**:

```text
rsp0_stack[16384]
TSS.RSP0 = rsp0_stack + 16384
```

This is intentionally one bootstrap acceptance stack, not scheduler-managed per-task kernel stacks. No TSS/IST scheduler redesign is part of this milestone.

### User mappings

The dedicated Ring 3 mode creates one process address space and two PMM data frames. Its fixed mappings are:

```text
0x0000000040000000  user code page
0x0000000040010000  user stack page
0x0000000040011000  initial user RSP / top of stack
```

The code mapping is `present + user` and deliberately not writable. The stack mapping is `present + user + writable`. The user bit is required and verified at every relevant PML4 -> PDPT -> PD -> PT level. Existing lower-level user tables may be reused only if they are process-owned and already user-accessible; large-page entries are rejected.

The original 0.0.10-dev Ring 3 milestone did not add NX handling and its isolated copied payload remains a separate regression. Milestone 11 later extended the same user-mapping path with explicit executable/NX permissions for BoringKernel-owned ELF mappings and the ELF userspace stack; it did not replace this Ring 3 test with an ELF-specific page-table implementation.

### Real CPL0 -> CPL3 entry

The test disables hardware interrupts for this isolated proof, activates the test process CR3, builds a five-word `iretq` frame, and enters user mode with:

```text
RIP     0x0000000040000000
CS      0x23
RFLAGS  0x2 (IF deliberately clear)
RSP     0x0000000040011000
SS      0x1B
```

The position-independent payload is copied byte-for-byte into the user code page. Before faulting it reads its live `CS`, records its live `RSP`, stores a marker, and performs a real user-stack push/pop/write. Those values prove that the CPU actually executed the payload at CPL3 rather than merely constructing a synthetic trap frame.

The payload label `x86_64_ring3_payload_cli` contains the privileged instruction:

```asm
cli
ud2
```

At CPL3, `cli` is not permitted because IOPL is zero. The CPU therefore raises a real **#GP / vector 13** at the `cli` RIP; the following `ud2` is only an unreachable fail-safe. The acceptance requires vector 13 with error code zero and requires the saved RIP to equal the copied address of the `cli` label.

### Real CPL3 -> CPL0 exception stack switch

Because the exception crosses from CPL3 to CPL0, x86_64 uses the loaded TSS and switches to `TSS.RSP0` before entering the DPL0 IDT gate. The CPU-saved privilege-transition frame includes the original user RSP and SS.

The acceptance proves all of the following simultaneously:

- saved CS is `0x23` and therefore has RPL3;
- saved SS is `0x1B` and has RPL3;
- the saved RIP is the expected CPL3 `cli` address;
- the saved user RSP is `0x0000000040011000`;
- the C-facing normalized `rsp` equals the hardware-pushed `hardware_rsp`;
- the C-facing normalized `ss` equals the hardware-pushed `hardware_ss`;
- the central `exception_frame_originates_from_cpl3()` API validates those CPL3 and hardware-frame invariants;
- the exception frame address itself lies inside the dedicated RSP0 stack;
- the C handler's live RSP lies inside the same RSP0 stack;
- the saved user RSP does **not** lie inside the RSP0 stack.

This remains the dedicated exception-path proof of a real hardware privilege transition in both directions. The original Ring 3 test intentionally halts after reporting success and does not recover. The separate syscall mode below provides the independently verified `SYSRETQ` return path to CPL3.

## Native x86_64 syscall boundary

BoringKernel 0.0.11-dev adds one deliberately narrow native x86_64 `SYSCALL` / `SYSRETQ` path. It does not add `INT 0x80` or any alternative syscall entry mechanism.

The setup checks CPU support and then programs and reads back:

```text
IA32_EFER   SCE enabled by read-modify-write
IA32_STAR   kernel/user selector relationship
IA32_LSTAR  x86_64_syscall_entry
IA32_FMASK  masks TF/IF/DF/NT/AC on entry
```

The current GDT selector relationship is reused rather than duplicated. `IA32_EFER` ownership is limited to setting SCE; unrelated EFER bits are preserved.

`SYSCALL` does **not** load `TSS.RSP0` and does not create an interrupt frame. Architectural entry leaves the user-controlled RSP active while placing user return RIP in `RCX` and user RFLAGS in `R11`. The assembly entry therefore performs no normal push or C call on that stack: it first saves user RSP to single-CPU bootstrap scratch, immediately switches RSP to a separate statically allocated **16-KiB supervisor-only syscall kernel stack**, constructs a separate 144-byte syscall frame there, executes `CLD`, and only then enters C dispatch.

The provisional register convention is:

```text
RAX = syscall number
RDI = arg0
RSI = arg1
RDX = arg2
R10 = arg3
R8  = arg4
R9  = arg5

RAX = result
RCX/R11 = architectural clobbers
```

The entry/return path preserves the user callee-saved registers `RBX`, `RBP`, and `R12`-`R15`. This convention is explicitly provisional and is not a stable public userspace ABI.

Exactly four provisional syscall numbers are currently dispatched:

```text
0  BORING_SYS_GETPID
1  BORING_SYS_DEBUG_WRITE
2  BORING_SYS_CONSOLE_WRITE
3  BORING_SYS_CONSOLE_READ
```

`GETPID` returns the actual current process PID from `process_current()`. `DEBUG_WRITE` keeps its original temporary bounded debug semantics and is not POSIX `write()` or a file-descriptor API. `CONSOLE_WRITE` and `CONSOLE_READ` are the only Milestone-13 additions. The console transfer bound is 1..64 bytes per call.

The narrow `copy_from_user` helper does not dereference a raw userspace pointer directly. It validates range overflow and canonical lower-half bounds, walks the active process page tables, requires Present + effective U/S permission at every level, rejects unsupported large-page paths, resolves each physical page through the trusted HHDM alias, and copies page by page into kernel-owned memory before the serial layer sees the bytes. `CONSOLE_WRITE` emits to COM1 only after the entire bounded kernel copy succeeds. For input, the kernel first validates the complete destination range with writable user permissions before polling COM1, then copies the received bytes back through the controlled `copy_to_user` path. Invalid/unmapped/non-user ranges return a negative BoringOS-specific `EFAULT`; invalid lengths return negative `EINVAL`; unknown syscall numbers return negative `ENOSYS`.

Before `SYSRETQ`, the kernel validates the expected GDT selector relationship, canonical lower-half user RIP, effectively user-mapped return RIP, nonzero canonical lower-half writable user RSP, and a live current non-bootstrap process/address space. Return RFLAGS are sanitized to a small allowed arithmetic-status set plus mandatory bit 1. Invalid return state causes a controlled fatal diagnostic rather than an unchecked `SYSRETQ`.

The dedicated original syscall QEMU payload still performs seven real `SYSCALL` instructions: valid `GETPID`, valid `DEBUG_WRITE`, unmapped-user-pointer rejection, higher-half-pointer rejection, overflowing-range rejection, oversized-length rejection, and unknown-syscall rejection. It proves repeated `SYSRETQ` continuation in CPL3, restores the original user RSP, preserves callee-saved registers, and finally executes privileged `cli`. That last real #GP still reaches the independent TSS.RSP0 exception stack, proving the syscall stack has not replaced the exception-entry model. The separate console acceptance does not weaken or replace this gate.

This bootstrap entry state is single-CPU and is not scheduler-integrated for userspace. There are no per-task syscall stacks, per-CPU syscall scratch structures, asynchronous user-mode timer preemption, general process-kill/recovery policy, libc/CRT, or stable ABI/versioning guarantees. The current `copy_from_user`/`copy_to_user` routines remain narrow syscall-boundary helpers rather than a general userspace-memory subsystem.

See [`syscalls.md`](syscalls.md) for the original syscall-boundary acceptance and [`userspace-console.md`](userspace-console.md) for the exact Milestone-13 console contract and limitations.

## Fixed-address ELF64 userspace bootstrap

BoringKernel 0.0.12-dev adds one bounded ELF64 x86_64 userspace loading path for a deterministic Limine boot module. This is not a filesystem or a general `exec` implementation: Limine supplies the configured module bytes directly, and the kernel validates the image before constructing process memory.

The accepted subset is little-endian x86_64 `ET_EXEC` with page-aligned fixed-address `PT_LOAD` segments. `PT_INTERP`, `PT_DYNAMIC`, `ET_DYN`/PIE, runtime relocations and writable-plus-executable segments are rejected. Header, program-table, file, memory and virtual ranges use bounded overflow-safe validation. The loader copies file-backed bytes into fresh PMM-owned process pages rather than executing module memory directly, leaves the real `p_memsz - p_filesz` BSS tail zero, and tracks image-frame ownership independently from process page-table ownership for rollback and cleanup.

The existing lower-half user mapper now expresses and verifies executable permission. RX executable mappings remain non-writable; non-executable read-only, writable and stack mappings carry the x86_64 XD bit. BoringKernel checks CPU NX support and enables only `IA32_EFER.NXE` with read-modify-write verification before relying on NX. The established effective higher-half audit remains in force, so shared kernel mappings stay supervisor-only.

The ELF acceptance creates a real process, activates its independent CR3, enters the ELF header entry at CPL3 with a separate one-page RW+NX user stack, proves BSS zero/write behavior from the loaded executable, reuses only the already implemented `GETPID` and `DEBUG_WRITE` syscalls, returns to the ELF through `SYSRETQ`, and finally executes privileged `CLI`. That final instruction raises a real #GP from CPL3 through the existing TSS.RSP0 exception path. The final fault is an acceptance endpoint, not a new process-exit mechanism.

See [`elf-userspace.md`](elf-userspace.md) for the exact supported subset, validation limits, permissions, ownership rules, measured ELF layout, malformed-image tests and known limitations.

## Minimal native C userspace runtime

The merged native-runtime milestone links a freestanding BoringOS-owned `_start` with a compiled C `boring_main()` and small memory/string/syscall helpers. The runtime is built with `-ffreestanding`, no host libc/CRT, no dynamic loader, no PIE, no runtime relocations, and no host startup objects. Its dedicated QEMU acceptance proves initialized data, loader-zeroed BSS, a real local userspace stack, the BoringOS memory/string helpers, `GETPID`, `DEBUG_WRITE`, repeated `SYSRETQ` continuation, a normal `boring_main()` return value of 42, and the same final CPL3 `CLI` -> #GP -> TSS.RSP0 proof.

Milestone 13 reuses these runtime objects rather than forking a second runtime. The separate `console-smoke.elf` supplies only its own compiled `boring_main()` and links the same `_start`, syscall, memory, and string objects.

## Early userspace serial console

The current console acceptance adds only provisional `CONSOLE_WRITE` and `CONSOLE_READ`. Output crosses CPL3 -> `SYSCALL` -> complete controlled `copy_from_user` -> kernel COM1 TX. Input validates the complete writable CPL3 destination before consuming COM1 RX, collects bytes in a bounded kernel buffer, copies them through `copy_to_user`, and resumes the same C program through `SYSRETQ`.

The QEMU gate uses a serial named pipe and injects `K` only after the compiled C client has emitted `console write from BoringOS userspace`. C receives ASCII 75 into a local stack variable, echoes `K`, and returns 43 before the final deliberate CPL3 #GP. RX is blocking and polled. This is not a TTY, line discipline, file descriptor, stdin/stdout abstraction, scheduler-aware sleep/wakeup mechanism, interrupt-driven UART receive path, or general device model.

See [`userspace-console.md`](userspace-console.md) for the exact syscall numbers, 64-byte bound, memory-safety ordering, artifact constraints, named-pipe acceptance sequence, and known limitations.

## Complete normalized x86_64 trap frame

The exception and IRQ entry paths share one **192-byte** normalized frame:

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

For the privilege-changing CPL3 -> CPL0 exception path, the hardware RSP/SS tail contains the actual user stack state pushed by the CPU. The first RSP/SS pair is the C-facing normalized copy and is explicitly compared against those hardware words by the Ring 3 acceptance. IRQ vector stubs synthesize error code zero and preserve all 15 integer GPRs before entering C.

This frame is also the preemptive kernel-task context. An interrupted kernel task's saved frame remains on that task's own stack until the task is resumed. Ring 3 scheduling is not implemented by this milestone. The syscall boundary deliberately uses its own separate 144-byte frame and does not modify this established exception/IRQ ABI.

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

Every task carries an owning process pointer. The preemptive kernel-task path is:

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

Existing PID-0 preemption regression tasks still use the same bootstrap root and therefore do not cause unnecessary CR3 reloads. The Ring 3, syscall, ELF, runtime, and console acceptances do not alter this scheduler or place CPL3 code under timer preemption.

## Fresh tasks and bootstrap restoration

Each new preemptive kernel task receives an independent 16-KiB heap-backed stack plus a synthetic complete IRQ/`iretq` frame on that stack. The frame targets `kernel_task_trampoline`, uses the current kernel CS/SS, has IF set, and supplies a correctly aligned resumed RSP.

Bootstrap remains task ID 0 in process PID 0. When preemption starts, the next real PIT interrupt creates an authentic bootstrap IRQ frame on that stack. The scheduler retains it. When all ordinary preemptive tasks finish, it activates PID 0/bootstrap root if needed and restores that saved bootstrap frame through `iretq`.

## Full integer-register preservation

Kernel-task preemption preserves:

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

The dedicated Ring 3 test intentionally ends in a controlled halt immediately after validating the fatal #GP privilege-transition path, so it does not exercise normal post-test process destruction. The syscall acceptance likewise remains an isolated acceptance mode rather than a scheduler-managed userspace lifecycle. The ELF, native-runtime, and console acceptances perform explicit image/process cleanup only after their final CPL3 fault has transferred control through the trusted exception path and the test has restored the bootstrap process context. These are explicit bootstrap acceptance lifecycles, not a general process-exit implementation.

## C / assembly boundary

High-level policy remains in C: memory ownership, address-space policy, process identity, user-page validation, descriptor/TSS construction, exception diagnostics, CPL3-frame validation, syscall dispatch and user-memory validation/copy policy, console validation-before-I/O ordering, `SYSRETQ` return-state policy, ELF validation/loading/ownership policy, PIC/timer dispatch, cooperative selection, preemptive round robin, task/process association, cleanup, and acceptance invariants.

Architecture assembly remains isolated to exception/IRQ entry and restore mechanics, GDT/segment/TR CPU instructions, `iretq`, native `SYSCALL` entry and `SYSRETQ` return mechanics, minimal cooperative context save/restore, the position-independent Ring 3/syscall test payloads, the freestanding assembly-only ELF smoke executable, the minimal runtime `_start`, deliberate fault triggers, register probes, and CPU instructions that C cannot express directly. CR3 read/write, MSR read/write, CPUID facility probing, NX enablement verification, port I/O, and `invlpg` remain tiny isolated architecture-specific operations. COM1 port I/O remains kernel-only.

## Current limitations

BoringKernel still does **not** provide:

- a general or long-lived userspace task model beyond the controlled Ring 3/syscall/ELF/runtime/console acceptance paths;
- user-task scheduling or scheduler-managed per-task TSS.RSP0 stacks;
- per-task syscall stacks or per-CPU syscall entry/scratch state;
- SMP-safe syscall entry or asynchronous user-mode timer preemption;
- a stable/public syscall ABI or syscall versioning contract;
- broad general-purpose user-memory APIs beyond the current narrow syscall-boundary copy helpers;
- a general ELF program loader beyond the bounded fixed-address `ET_EXEC` boot-module subset, dynamic linking, PIE/`ET_DYN`, or runtime relocations;
- libc/CRT, process termination/recovery policy for arbitrary userspace, or a general userspace lifecycle;
- fork, exec, wait, signals, copy-on-write, or PID namespaces;
- file descriptors, credentials, process trees, sessions, or process groups;
- `stdin`, `stdout`, `stderr`, a TTY subsystem, line discipline, or terminal editing;
- interrupt-driven UART RX, scheduler-aware I/O sleeping, wait queues, or wakeups;
- PCID, demand paging, or swap;
- FPU/SIMD task/process state;
- mutexes, semaphores, or condition variables;
- priorities or realtime scheduling;
- SMP or per-CPU scheduler/process/TSS state;
- LAPIC, IOAPIC, APIC timer, HPET, or ACPI/MADT;
- a Double Fault IST/emergency stack;
- VFS, RAMFS, BoringFS implementation, path lookup, mounts, block devices, or storage drivers;
- networking, USB, audio, graphics, keyboard/mouse input, or native BoringWM.

BoringKernel 0.0.14-dev proves a **real compiled freestanding C userspace path with a BoringOS-owned minimal runtime and bounded bidirectional serial-console I/O: output crosses CPL3 -> `SYSCALL` -> validated kernel copy -> COM1, real COM1 input is accepted only after complete writable-userspace validation and returns through `copy_to_user` -> `SYSRETQ` into the same C program, which observes and echoes the byte before a final CPL3 `CLI` #GP through TSS.RSP0 and exact image/process cleanup**. It preserves the earlier kernel, Ring 3, syscall, ELF-loader, and native-runtime acceptance surfaces while still providing no VFS, file descriptors, shell/init, TTY model, dynamic linking, or scheduler-managed userspace execution.
