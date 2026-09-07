# Minimal native userspace runtime

Milestone 12 adds the first reusable BoringOS-owned C userspace foundation. It is deliberately small and freestanding. It is not libc, a POSIX compatibility layer, a process runtime, a shell runtime, or a general application framework.

The accepted execution path is:

```text
Limine boot module
    ↓
validated Milestone-11 ELF64 ET_EXEC loader
    ↓
PMM-owned process pages + independent address space
    ↓
CPL3
    ↓
BoringOS _start
    ↓
int boring_main(void)
    ↓
BoringOS runtime helpers
    ↓
existing SYSCALL / SYSRETQ boundary
```

## Freestanding build model

The runtime and its C smoke program are compiled as freestanding x86_64 code. The build disables the host startup/runtime path and links the final image directly with `ld -nostdlib -static` and the BoringOS runtime-smoke linker script.

The build deliberately does not link:

- glibc or musl;
- host libc;
- `crt1.o`, `crti.o`, or `crtn.o`;
- libstdc++;
- host syscall wrappers;
- a dynamic linker.

The C compilation also disables compiler builtins, the stack protector, PIC/PIE, unwind-table generation, the red zone, x87, MMX, SSE, and SSE2 for this bootstrap runtime.

`tests/runtime-build-audit.sh` fails closed unless the produced smoke executable is ELF64, little-endian, x86_64, `ET_EXEC`, static and fixed-address. It requires exactly three `PT_LOAD` segments, rejects `PT_INTERP`, `PT_DYNAMIC`, `PT_TLS`, W+X mappings and TLS sections, requires a real `.data` plus NOBITS `.bss`, and rejects dynamic dependencies, runtime relocations, unresolved symbols and host CRT/libc-style symbols.

The current accepted runtime-smoke image is laid out at fixed virtual addresses:

```text
0x40000000 ... text       R-X
0x40001000 ... rodata     R--
0x40002000 ... data/BSS   RW-
0x40010000 ... user stack RW-, NX
```

Milestone 11 remains responsible for validating and loading the ELF image. Module bytes are copied into fresh PMM-owned process pages; code does not execute directly from Limine module memory. The loader preserves W^X, NX, supervisor-only higher-half mappings, loader-zeroed BSS, rollback on load failure, and the requirement that the ELF entry lies inside an executable `PT_LOAD` segment.

Dynamic ELF loading is not supported. There is no `PT_INTERP`, `PT_DYNAMIC`, runtime relocation processing, PIE / `ET_DYN`, TLS setup, or shared-library mechanism.

## `_start` and the C entry point

`user/runtime/entry.S` supplies the BoringOS-owned `_start` symbol. The ELF header entry points directly to `_start` at `0x0000000040000000`.

The current loader enters userspace with a prepared stack top. `_start` checks that `%rsp` is 16-byte aligned before making the C call. This satisfies the x86_64 System V call-site rule: the caller aligns the stack before `call`, and the pushed return address gives `boring_main` the normal callee-entry stack relationship.

The runtime contract is intentionally only:

```c
int boring_main(void);
```

There is currently no `argc`, `argv`, `envp`, auxiliary vector, constructor array, destructor array, TLS initialization, or language runtime initialization.

`_start` calls `boring_main` exactly once. A normal C return is required and is part of acceptance. The signed 32-bit return value in `%eax` is sign-extended into `%r12`, which is then observable in the final hardware exception frame. The current smoke program returns `42`, and the kernel acceptance requires the final saved `%r12` to contain exactly `42`.

There is intentionally no exit syscall yet. BoringOS does not currently have general process-exit semantics, so Milestone 12 must not invent them merely to terminate the smoke program. After `boring_main` returns, `_start` executes `cli` only as an acceptance ending. Because the program is still in CPL3, that privileged instruction produces the expected real #GP / vector 13. The existing TSS.RSP0 exception path then returns control to the kernel-side acceptance handler, which validates the C result and cleans up the loaded process. A following `ud2` is only a defensive fallback if the expected `cli` fault were ever not delivered.

This `cli` ending is test machinery, not an application exit ABI.

## Runtime API

The reusable API is deliberately namespaced and tiny:

```c
uint64_t boring_getpid(void);
long boring_debug_write(const void *buffer, size_t length);
void *boring_memcpy(void *destination, const void *source, size_t length);
void *boring_memset(void *destination, int value, size_t length);
size_t boring_strlen(const char *string);
int boring_main(void);
```

There are no generic `memcpy`, `memset`, or `strlen` compatibility symbols. The three memory/string helpers are BoringOS-owned straightforward loops and do not delegate to a host library.

### `boring_getpid`

`boring_getpid()` wraps the already existing provisional `GETPID` syscall only. It places `BORING_SYS_GETPID` in `%rax`, executes the real x86_64 `syscall` instruction, and receives the kernel return value in `%rax`.

The x86_64 `SYSCALL` instruction uses `%rcx` and `%r11` for architectural return state, so the wrapper declares both clobbered, together with condition codes. `GETPID` does not transfer a userspace memory buffer, so this wrapper does not require a compiler `memory` clobber.

### `boring_debug_write`

`boring_debug_write()` wraps the already existing bounded debug-only `DEBUG_WRITE` syscall. Its current provisional register ABI is:

```text
RAX = BORING_SYS_DEBUG_WRITE
RDI = userspace buffer pointer
RSI = byte length
RAX = signed return value
```

The wrapper declares `%rcx`, `%r11`, condition codes and `memory` clobbered. The `memory` clobber prevents the compiler from moving userspace memory effects across a call whose kernel side reads the supplied buffer.

The kernel continues to enforce the existing bounded `DEBUG_WRITE` contract and validated `copy_from_user` path. Milestone 12 does not add another console API and does not expose direct serial hardware access to userspace.

The shared syscall constants remain the existing provisional ABI:

```text
GETPID      = 0
DEBUG_WRITE = 1
DEBUG_WRITE maximum length = 64 bytes
```

No new syscall number is introduced by this milestone.

## Data, BSS, and ordinary C state

The runtime smoke program deliberately contains both initialized writable data and real zero-initialized BSS.

Its initialized marker resides in `.data` and must retain the compile-time marker value after the ELF loader copies the segment into process-owned frames. Its BSS probe resides in `.bss`; the loader must present it as zero before C runs, and `boring_main` then writes a non-zero marker and verifies that the write persists.

The smoke program also uses ordinary automatic arrays and a local guard on the CPL3 user stack. This makes the acceptance exercise normal compiler-generated C stack accesses rather than only globals or assembly registers.

## Acceptance proof

The dedicated runtime acceptance keeps the existing assembly ELF smoke as a separate gate and then loads the C runtime smoke through the same constrained ELF loader. Before entering CPL3 it proves:

- the expected Limine boot module was found;
- the runtime ELF validates under the constrained ELF64 subset;
- NX is enabled;
- an independent process/address space was created;
- the runtime image loaded into PMM-owned pages;
- segment permissions are correct;
- the entry mapping is executable and not writable;
- loader-owned BSS is initially zero;
- the user stack is mapped RW+NX;
- the shared higher half remains supervisor-only.

The real compiled C program then proves at CPL3:

- `_start` reached `boring_main`;
- initialized `.data` is intact;
- `.bss` starts at zero and is writable;
- ordinary local stack storage works;
- `boring_strlen`, `boring_memset`, and `boring_memcpy` work;
- `boring_getpid()` enters and returns through the existing syscall path;
- `boring_debug_write()` enters the kernel, uses validated user-memory copying, emits the supplied C string, and returns through `SYSRETQ`;
- the same C program resumes after `SYSRETQ`;
- `boring_main` returns normally to `_start`;
- the return value remains observable in the later hardware trap frame;
- the acceptance-only CPL3 `cli` produces a real #GP;
- exception entry uses the existing TSS.RSP0 kernel stack;
- the runtime process, mappings and PMM-owned image frames are cleaned up.

The human-visible debug proof is the string owned by `user/runtime-smoke/main.c`:

```text
hello from BoringOS C userspace
```

The path is compiled C → `boring_debug_write()` → real `SYSCALL` → BoringKernel → validated `copy_from_user` → debug serial output → `SYSRETQ` → the same C program resumes.

## Current limitations

Milestone 12 intentionally does not provide:

- libc or CRT compatibility;
- generic C standard-library names;
- an exit syscall or general process-exit API;
- `argc`, `argv`, `envp`, or an auxiliary vector;
- constructors or destructors;
- TLS;
- dynamic linking or shared libraries;
- PIE / `ET_DYN` userspace;
- runtime relocations;
- malloc/free or a userspace heap;
- stdio, `printf`, file descriptors, or streams;
- a userspace console or TTY abstraction;
- keyboard/input handling;
- general userspace task scheduling;
- VFS, RAMFS, BoringFS implementation, or storage drivers;
- networking;
- display/window-system work;
- SMP or an APIC timer path.

The runtime is therefore a real freestanding C foundation, but deliberately not yet a general-purpose userspace environment.
