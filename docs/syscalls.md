# BoringKernel syscall boundary

This remains a deliberately narrow, QEMU-verified bootstrap interface, **not
a stable public ABI and not a general userspace runtime**. The original
0.0.11-dev boundary is retained below; this section records the current M29
extensions that supersede its two-call inventory and process-lifecycle
non-goals.

## Current M29 syscall surface

```text
 0 GETPID             9 FS_READ            18 FD_OPEN
 1 DEBUG_WRITE       10 FS_TOUCH           19 FD_READ
 2 CONSOLE_WRITE     11 FS_WRITE           20 FD_WRITE
 3 CONSOLE_READ      12 FS_UNLINK          21 FD_CLOSE
 4 LAUNCH            13 INFO
 5 FS_READDIR        14 GETCWD
 6 FS_MKDIR          15 PROCESS_SNAPSHOT
 7 FS_RMDIR          16 EXIT
 8 FS_CHDIR          17 WAITPID
```

All pointer-bearing calls retain the established complete-range validation
and checked copy boundary. `GETCWD` copies the current process's canonical
absolute VFS path into a bounded user buffer. `PROCESS_SNAPSHOT` enumerates
real PID, PPID, RUNNING/WAITING/ZOMBIE state and process name; end of the
bounded table is reported without inventing rows.

`INFO` ABI v2 is a fixed 256-byte structure containing real PMM usable/free
memory, PIT ticks and effective frequency when available, active process
count, current PID, hostname, current-process username, OS/kernel/version and
architecture strings, plus root-filesystem and root-device identity selected
by the actual boot mode. It contains no fabricated CPU or network data.

`EXIT` applies only to the active `LAUNCH` child. It preserves the 32-bit exit
status, resumes the saved parent frame, unloads the child ELF mappings and
marks the child as a non-runnable zombie. `WAITPID` succeeds only for that
exact parent/child relationship, returns the preserved status through checked
userspace memory, destroys the inactive child address space and retained CWD,
and releases its process-table slot. The launch model remains synchronous and
permits only one suspended child; this is not general POSIX process control.

The current additional symbolic error `EISDIR` maps directory-vs-regular-file
misuse without changing underlying VFS semantics.

## Original 0.0.11-dev scope

BoringKernel 0.0.11-dev uses the native x86_64 `SYSCALL` / `SYSRETQ` mechanism for the first controlled CPL3 -> CPL0 -> CPL3 call boundary.

The milestone proves that a real copied CPL3 instruction stream can execute `SYSCALL`, enter BoringKernel at a kernel-selected address, abandon the untrusted user stack, run C dispatch on a supervisor-only kernel stack, validate any user memory involved, return a result in `RAX`, execute `SYSRETQ`, and continue executing at CPL3.

This milestone does **not** add ELF loading, libc/CRT, a userspace runtime, user-task scheduling, user-mode timer preemption, VFS/storage, file descriptors, signals, fork/exec/wait, process termination policy, SMP/per-CPU syscall state, SMEP/SMAP, or a stable syscall-versioning promise.

## Why SYSCALL / SYSRETQ

The first boundary uses the architectural 64-bit fast-system-call mechanism rather than `INT 0x80` or a second competing entry method. The current BoringKernel GDT already supplies the selector relationship required by `SYSCALL` and `SYSRETQ`, and the mechanism exposes the important architectural distinction that `SYSCALL` does **not** create an interrupt frame or automatically switch to `TSS.RSP0`.

The existing IDT/TSS exception path remains separate and unchanged in meaning. It is still used by the final CPL3 `CLI` acceptance fault after the syscall round trips have completed.

## CPU feature and MSR setup

Before enabling the boundary, BoringKernel checks CPUID extended feature leaf `0x80000001`, EDX bit 11 for `SYSCALL`/`SYSRET` support. If the facility is absent, the dedicated acceptance mode fails instead of executing undefined entry code.

The configured MSRs are:

```text
IA32_EFER   0xC0000080
IA32_STAR   0xC0000081
IA32_LSTAR  0xC0000082
IA32_FMASK  0xC0000084
```

### IA32_EFER

BoringKernel reads the existing EFER value, sets only `EFER.SCE` (bit 0), writes the result, reads it back, and requires every unrelated EFER bit to remain unchanged.

One verified QEMU run reported:

```text
IA32_EFER: 0x0000000000000D01
```

The exact unrelated EFER bits are platform/bootstrap state; only SCE ownership is claimed here.

### IA32_STAR

STAR is derived from the centralized GDT selector definitions rather than independent magic selector constants.

Current selectors:

```text
kernel CS  0x08
kernel SS  0x10
user SS    0x1B
user CS    0x23
TSS/TR     0x28
```

For 64-bit `SYSRETQ`, STAR's upper selector base is `0x10`, producing user SS `0x1B` and user CS `0x23`. The current programmed value is:

```text
IA32_STAR: 0x0010000800000000
```

Static assertions enforce the required kernel-CS/kernel-SS and SYSRET selector relationships at build time.

### IA32_LSTAR

LSTAR contains the canonical higher-half address of the isolated assembly entry symbol `x86_64_syscall_entry`.

One verified QEMU run reported:

```text
IA32_LSTAR: 0xFFFFFFFF8000C007
```

CPL3 does not choose this address; the CPU loads it from LSTAR when `SYSCALL` executes.

### IA32_FMASK

The bootstrap FMASK is:

```text
IA32_FMASK: 0x0000000000044700
```

It clears these user-controlled flags on kernel entry:

```text
TF  bit 8
IF  bit 9
DF  bit 10
NT  bit 14
AC  bit 18
```

Assembly also executes `CLD` before calling C even though DF is already masked. The current syscall acceptance intentionally runs without user-mode timer preemption; this FMASK policy is therefore future-facing hardening as well as an entry invariant.

## SYSCALL does not use TSS.RSP0

This is intentionally different from the existing CPL3 -> CPL0 exception path.

On `SYSCALL`, x86_64 provides:

```text
RCX = user return RIP
R11 = user RFLAGS
RSP = still the user-controlled RSP
RAX = syscall number by the provisional BoringOS convention
```

The processor does **not** load `TSS.RSP0` and does not push a hardware interrupt frame.

The LSTAR entry therefore performs no push and no C call while the user RSP is active. Its first stack-related actions are conceptually:

```text
save user RSP to single-CPU bootstrap scratch
load dedicated kernel syscall-stack top into RSP
build syscall frame on trusted stack
CLD
call C dispatcher
```

The scratch word is acceptable only because the current acceptance model is single-CPU, has no scheduled user tasks, and keeps asynchronous user-mode interruption out of scope. It is not the final per-task or per-CPU entry design.

## Dedicated bootstrap syscall stack

The syscall boundary owns one statically allocated, 16-byte-aligned **16-KiB** kernel stack:

```text
x86_64_syscall_stack[16384]
```

It is linked into the shared higher-half kernel mapping and is therefore supervisor-only under the current effective higher-half permission invariant.

One verified QEMU run reported:

```text
Syscall stack base:    0xFFFFFFFF80030D70
Syscall stack top:     0xFFFFFFFF80034D70
Live syscall C RSP:    0xFFFFFFFF80034C18
Saved user RSP:        0x0000000040011000
```

The acceptance checks the live C-dispatch RSP and syscall frame address against the stack bounds. It also checks that the saved user RSP is not in this kernel stack.

This stack is intentionally distinct from the 16-KiB TSS.RSP0 exception stack used by real CPL3 exceptions.

## Separate syscall entry frame

The syscall boundary does not reuse or redesign the established 192-byte IRQ/exception trap frame.

Its bootstrap frame is 144 bytes and records:

```text
user RSP
user return RIP (architectural RCX)
user RFLAGS (architectural R11)
syscall number
RBX RBP R12 R13 R14 R15
RDI RSI RDX R10 R8 R9
result
reserved
```

The 144-byte allocation preserves the required SysV AMD64 stack alignment before the assembly stub calls normal C code.

## Provisional BoringOS syscall ABI v0

The current register convention is deliberately provisional:

```text
RAX = syscall number
RDI = arg0
RSI = arg1
RDX = arg2
R10 = arg3
R8  = arg4
R9  = arg5

RAX = result
```

`RCX` and `R11` are architectural `SYSCALL`/`SYSRETQ` clobbers. They are not syscall arguments.

The current entry path preserves user callee-saved registers:

```text
RBX RBP R12 R13 R14 R15
```

It also restores the untouched argument registers used by this bootstrap interface. No stability guarantee is made for a future public ABI.

## Original 0.0.11-dev syscall numbers

Only two real calls exist:

```text
0  BORING_SYS_GETPID
1  BORING_SYS_DEBUG_WRITE
```

There is no large syscall table.

### GETPID

`GETPID` takes no pointer. It returns `process_current()->pid`; it does not hard-code PID 1 in the dispatcher.

The dedicated acceptance process is the first ordinary process and the verified result is:

```text
GETPID result: 1
```

### DEBUG_WRITE

`DEBUG_WRITE` is a temporary bring-up/debug call. It is **not POSIX `write()`**, has no file descriptor, creates no TTY abstraction, and is not a final userspace I/O ABI.

Arguments:

```text
RDI = user buffer address
RSI = byte length
```

Current length policy:

```text
1 .. 64 bytes
```

On success the kernel copies all bytes to a kernel-owned local buffer first. Only that kernel buffer is passed to the serial layer. The serial code never receives or dereferences the raw userspace pointer.

The acceptance message is:

```text
hello from boring syscall
```

and the verified return is 25 bytes.

## Error-return model

The first boundary uses small BoringOS-specific symbolic errors:

```text
ENOSYS = 1
EFAULT = 2
EINVAL = 3
```

They are returned in `RAX` as negative signed 64-bit values. These names do **not** claim POSIX errno compatibility.

Current behavior:

- unknown syscall number -> `-ENOSYS`;
- invalid/unmapped/non-user user range -> `-EFAULT`;
- zero or oversized DEBUG_WRITE length -> `-EINVAL`.

Unknown syscall numbers never index an unchecked table and do not fault the kernel.

## Safe copy_from_user strategy

The kernel does not implement this pattern:

```text
memcpy(kernel_buffer, raw_user_pointer, length)
```

Instead, the narrow helper validates the complete requested range and copies page by page:

```text
user VA range
  ↓ overflow + canonical lower-half validation
current process address space
  ↓ PML4 -> PDPT -> PD -> PT walk
Present + effective U/S permission on every level
  ↓
physical frame + page offset
  ↓
trusted HHDM kernel alias
  ↓
kernel-owned destination buffer
```

The validation additionally rejects unsupported large-page paths and requires the lower-half intermediate tables used by this process to be process-owned. Missing pages are errors; there is no demand paging.

The helper supports crossing a 4-KiB page boundary by repeating the validation/copy per page, although the current short acceptance string remains within one page.

No `copy_to_user` helper is added merely for symmetry. Return values use `RAX`; a kernel-to-user buffer copy will be introduced only when a later milestone has a concrete need.

## User-memory security checks

The range helper requires:

- nonzero length;
- no `start + length - 1` overflow;
- canonical lower-half start and end;
- the complete range to remain below the user/kernel split;
- every covered page to be present;
- effective `U/S=1` through every page-table level;
- the current real process address space, not a second syscall-specific process global.

The acceptance proves:

```text
valid mapped user pointer             PASS
unmapped lower-half pointer           -> -EFAULT
higher-half kernel pointer            -> -EFAULT
overflowing address + length           -> -EFAULT
oversized DEBUG_WRITE length          -> -EINVAL
unknown syscall                       -> -ENOSYS
```

None of these negative cases creates a kernel-mode page fault.

The inherited shared higher half is also walked using the existing effective-access logic; no present higher-half leaf may be reachable through a fully user-enabled paging path.

## SYSRETQ return-state validation

The kernel does not execute `SYSRETQ` with unchecked architectural state.

Before returning it requires:

- the centralized GDT selectors still have the expected kernel/user values;
- saved user RIP is canonical lower-half and effectively user mapped;
- saved user RSP is nonzero and `RSP - 1` lies in canonical lower-half writable user memory;
- the current process is alive and owns the active non-bootstrap address space.

User RFLAGS are sanitized centrally. The bootstrap policy returns only arithmetic status flags `CF/PF/AF/ZF/SF/OF` plus mandatory reserved bit 1. In particular it does not return user-controlled `IOPL`, `NT`, `VM`, `TF`, `IF`, `DF`, or `AC` in this controlled non-preemptible user path.

If return state is invalid, the current kernel enters a controlled fatal diagnostic rather than attempting unsafe `SYSRETQ`. A general process-kill/recovery mechanism does not exist yet.

## Real QEMU round-trip proof

The dedicated `TEST_MODE=syscall` copies another tiny position-independent assembly payload into the real process user code page at:

```text
user code        0x0000000040000000
user stack       0x0000000040010000 .. 0x0000000040011000
user message     0x0000000040010200
```

The payload executes seven real `SYSCALL` instructions:

1. `GETPID`;
2. valid `DEBUG_WRITE`;
3. `DEBUG_WRITE` with an unmapped lower-half pointer;
4. `DEBUG_WRITE` with a higher-half kernel pointer;
5. `DEBUG_WRITE` with an overflowing pointer/range;
6. `DEBUG_WRITE` with an oversized length;
7. an unknown syscall number.

After `GETPID`, CPL3 code records a marker, live CS and live RSP **after `SYSRETQ`**, proving that userspace actually resumed. It also checks sentinel values in `RBX`, `RBP`, and `R12`-`R15` after all syscall round trips.

Finally the payload executes the same style of privileged `CLI` instruction used by the earlier Ring 3 milestone. That instruction produces a real #GP / vector 13 from CPL3, through the independent TSS.RSP0 exception path. The final #GP is not the syscall proof itself; it proves that the payload continued executing in real CPL3 after the `SYSRETQ` returns.

One verified QEMU run reported:

```text
IA32_EFER: 0x0000000000000D01
IA32_STAR: 0x0010000800000000
IA32_LSTAR: 0xFFFFFFFF8000C007
IA32_FMASK: 0x0000000000044700

Syscall stack base: 0xFFFFFFFF80030D70
Syscall stack top:  0xFFFFFFFF80034D70
Live syscall kernel RSP: 0xFFFFFFFF80034C18
Saved syscall user RSP:  0x0000000040011000

GETPID result: 1
DEBUG_WRITE result: 25
Syscall dispatches: 7
```

The same run passed:

```text
msr-config
gdt-selectors
user-code-mapped
user-stack-mapped
higher-half-supervisor-only
entered-cpl3
syscall-entered-cpl0
syscall-kernel-stack
getpid
valid-user-copy
unmapped-user-pointer-rejected
kernel-pointer-rejected
overflowing-user-range-rejected
oversized-length-rejected
unknown-syscall
sysret-cpl3
user-rsp-restored
callee-saved-preserved
final-cpl3-proof
final-tss-rsp0
```

## Current security boundary and missing work

The milestone establishes a real controlled boundary, but it is not a complete userspace security model.

Still missing deliberately:

- general/scheduled CPL3 tasks;
- per-task syscall stacks;
- per-CPU syscall scratch/stack state;
- SMP-safe syscall entry;
- asynchronous user-mode timer preemption;
- general process termination/recovery for malformed syscall return state;
- `copy_to_user` and broader user-memory APIs;
- stable syscall ABI/versioning;
- ELF validation/loading;
- libc/CRT/userspace runtime;
- signals and process-lifecycle semantics;
- file descriptors, VFS and storage;
- SMEP/SMAP;
- FPU/SIMD ownership/context switching.

The next roadmap milestone remains separate. This implementation must not be interpreted as having started the ELF userspace loader.
