# BoringKernel ELF userspace loading — 0.0.12-dev bootstrap

This document describes only the implemented and QEMU-verified BoringKernel Milestone 11 ELF userspace loader. The loader is deliberately small and bounded. It is not a filesystem, not a general `exec` implementation, not a userspace runtime, and not a promise of broad ELF compatibility.

## Boot-module source

The acceptance executable is built separately from the kernel as:

```text
build/user/elf-smoke.elf
```

The ISO build copies it to:

```text
/boot/user/elf-smoke.elf
```

The Limine configuration registers exactly one module for this milestone:

```text
module_path: boot():/boot/user/elf-smoke.elf
module_string: boringos-elf-smoke
```

BoringKernel obtains the module through the pinned Limine module-request protocol. The acceptance path requires the expected module path/string and consumes the byte range Limine reports for that module.

This is **not filesystem I/O**. BoringKernel does not open a path, mount a volume, resolve a directory, use a VFS, or read storage blocks. Limine has already loaded the configured boot module into memory before BoringKernel starts. The module path is bootloader configuration/provenance, not a BoringOS pathname API.

The module memory is only the input image. BoringKernel does **not** execute instructions directly from Limine module memory. Validated `PT_LOAD` contents are copied into fresh PMM-owned frames mapped into an independent process address space, and CPL3 entry uses the mapped ELF entry virtual address.

## Supported ELF subset

Milestone 11 accepts only a narrow fixed-address static subset:

```text
ELF class       ELF64
endianness      little-endian
machine         x86_64 / EM_X86_64
file type       ET_EXEC
ELF version     EV_CURRENT
loading         PT_LOAD segments only
page size       4096 bytes
```

The loader intentionally does **not** support:

- `ET_DYN` or PIE;
- `PT_INTERP`;
- `PT_DYNAMIC`;
- a dynamic linker;
- runtime relocations;
- TLS;
- libc or a CRT;
- argc/argv, envp, or auxv;
- a reusable userspace syscall-wrapper library.

`PT_INTERP` and `PT_DYNAMIC` cause validation failure. The acceptance artifact is linked as a fixed-address static executable and the host build audit requires zero runtime relocations.

## Bounded validation

Validation happens before process data frames or process-private paging structures are allocated for the image. Malformed images therefore fail before the loader begins constructing a process image.

Current hard limits are:

```text
maximum module size          65536 bytes
maximum program headers      8
maximum PT_LOAD segments     4
maximum image pages          16
userspace stack pages        1
page size                    4096 bytes
```

The ELF header must fit completely inside the supplied module. The program-header entry size must be the expected ELF64 size and the complete program-header table must fit inside the module using overflow-checked arithmetic.

For every `PT_LOAD`, BoringKernel requires:

- nonzero `p_memsz`;
- `p_filesz <= p_memsz`;
- the file range `p_offset .. p_offset + p_filesz` to be entirely inside the module;
- the virtual memory range to be overflow-safe and entirely canonical lower-half userspace;
- no overlap between page-rounded load segments;
- no overlap with the separately reserved userspace stack;
- only the supported `PF_R`, `PF_W`, and `PF_X` bits;
- read permission to be present;
- writable-plus-executable (`W+X`) to be rejected;
- `p_align == 4096`;
- page-aligned `p_vaddr` and `p_offset`;
- the required `p_vaddr % p_align == p_offset % p_align` relationship;
- the total bounded page count to remain within the image limit.

The validator also rejects higher-half load addresses and arithmetic overflow instead of truncating or wrapping them.

The dedicated QEMU acceptance mutates the real built ELF deterministically and proves rejection of malformed magic, class, endianness, machine, type, truncated program-header data, `p_filesz > p_memsz`, out-of-module file ranges, virtual-address overflow, higher-half segments, overlapping load segments, `W+X`, and an invalid entry point. The malformed cases are checked without consuming PMM frames.

## Entry-point policy

The ELF header entry value is not trusted merely because it is a canonical address.

The entry must:

- remain in canonical lower-half userspace;
- lie inside the memory range of a validated `PT_LOAD` segment;
- be inside a segment carrying `PF_X`;
- not be inside a writable executable segment, because `W+X` is rejected globally;
- resolve through the real process page tables to the PMM-owned executable user mapping created by the loader.

The acceptance executable has:

```text
entry = 0x0000000040000000
```

CPL3 is entered at that ELF-derived mapped address, not at a copied kernel payload label and not at Limine module memory.

## Page permissions, W^X, and NX

Milestone 11 extends the existing Ring 3 user-mapping path rather than adding a second ELF-specific page-table implementation.

The effective user mappings used by the loader are:

```text
ELF PF_R | PF_X       -> user read/execute, not writable (RX)
ELF PF_R              -> user read, not writable, NX      (R+NX)
ELF PF_R | PF_W       -> user read/write, NX              (RW+NX)
userspace stack        -> user read/write, NX              (RW+NX)
```

Writable-plus-executable ELF segments are rejected before mapping.

For non-executable BoringKernel-owned user mappings, NX is architecturally enforced through the x86_64 page-table XD bit. Before those mappings are used, BoringKernel checks CPUID extended feature information for NX support and ensures `IA32_EFER.NXE` is set. Enabling NX uses a read-modify-write of EFER and verifies that unrelated EFER bits remain unchanged. The mapping query path then checks the effective writable/executable leaf permissions rather than assuming requested flags were installed.

The existing shared higher half remains supervisor-only. User-map creation is still restricted to the canonical lower half, and the established effective higher-half audit is run after ELF mappings are built and after the process root is active.

## PMM ownership and rollback

Executable bytes never become process memory by aliasing or directly executing the Limine module pages.

For each image page BoringKernel:

1. obtains a fresh frame from the existing PMM;
2. records explicit image-frame ownership;
3. reaches the frame through the trusted HHDM alias;
4. zeroes the complete 4096-byte page;
5. copies only validated file-backed ELF bytes into that page;
6. maps the frame at the validated process-private user virtual address with the required permissions;
7. verifies the resulting mapping and permissions.

ELF image data-frame ownership is separate from the address-space layer's ownership of process-private page-table frames.

If construction fails after allocation has begun, the loader walks owned image pages in reverse order. A mapped page must be successfully unmapped before its physical frame is returned to PMM. A frame is not silently freed while a live user mapping still references it. Newly created lower-level user paging tables also have rollback bookkeeping in the shared Ring 3 mapping layer.

The loader acceptance checks cleanup/bookkeeping after returning to the bootstrap process.

## BSS zeroing

BSS behavior follows the validated ELF `p_filesz`/`p_memsz` distinction. A newly allocated page is zeroed before file-backed bytes are copied, and only `p_filesz` bytes are copied from the module. Therefore the in-memory tail from `p_filesz` up to `p_memsz` remains zero.

The real smoke executable deliberately contains:

```text
RW PT_LOAD
p_filesz = 96 bytes
p_memsz  = 264 bytes
```

so the acceptance contains a genuine `p_memsz > p_filesz` BSS region. BoringKernel verifies the BSS as zero before entering CPL3; the ELF program independently checks it again in userspace, writes to the BSS, and records that the write succeeded.

## Separate userspace stack

The stack is not inferred from an ELF `PT_GNU_STACK` header and is not part of any load segment. Milestone 11 creates one explicit 4096-byte stack mapping at:

```text
0x0000000040010000 .. 0x0000000040011000
```

The stack is user-accessible, writable, and non-executable. Initial RSP is the top of that mapping:

```text
0x0000000040011000
```

There is no stack growth, guard page, argc/argv, envp, auxv, TLS setup, or general userspace process bootstrap contract yet.

## Real smoke executable

The committed smoke program is assembly-only and freestanding. It is intentionally not a C program and has no CRT or libc dependency.

The host `readelf` audit for the verified artifact reports:

```text
ELF size: 13176 bytes
Entry:    0x0000000040000000
Program headers: 3

PT_LOAD 0
0x0000000040000000 - 0x00000000400000EB
filesz = 235
memsz  = 235
flags  = R-X

PT_LOAD 1
0x0000000040001000 - 0x0000000040001021
filesz = 33
memsz  = 33
flags  = R--

PT_LOAD 2
0x0000000040002000 - 0x0000000040002108
filesz = 96
memsz  = 264
flags  = RW-
```

These three segments intentionally exercise all three supported effective permission classes: RX, R+NX, and RW+NX.

## Existing syscall boundary is reused

Milestone 11 does not add a new syscall.

The smoke executable includes only the two provisional numeric constants already implemented by BoringKernel 0.0.11-dev:

```text
0  GETPID
1  DEBUG_WRITE
```

The constants are shared through a tiny ABI-constant header so the assembly source does not duplicate magic syscall numbers. This header is not a userspace runtime or wrapper library.

The real loaded ELF performs:

```text
GETPID
DEBUG_WRITE("hello from BoringOS ELF userspace")
```

The verified acceptance process is PID 1. `GETPID` returns 1, `DEBUG_WRITE` returns 33, and the kernel reports two real syscall dispatches. Each `SYSRETQ` returns execution to the same loaded ELF in CPL3.

## Final CPL3 / exception proof

After the syscall round trips, the ELF executes a privileged `CLI` instruction followed by an unreachable `UD2` fail-safe.

With CPL3 and IOPL 0, `CLI` causes a real General Protection Fault, vector 13. The final exception evidence requires the saved user CS/SS and ELF RIP to identify CPL3 origin and requires the kernel handler/frame to be on the established TSS.RSP0 kernel exception stack.

This proves the complete acceptance path:

```text
Limine boot module
  -> strict ELF64 validation
  -> PMM-owned PT_LOAD pages
  -> independent process address space / real CR3
  -> ELF header entry at CPL3
  -> GETPID / DEBUG_WRITE via SYSCALL
  -> SYSRETQ back into the ELF
  -> privileged CLI
  -> real #GP through TSS.RSP0
```

The final fault is a deliberate acceptance endpoint, not an `exit` mechanism. No exit syscall or process-lifecycle API is introduced.

## Known limitations

BoringKernel 0.0.12-dev still does not provide:

- ELF32, big-endian ELF, or non-x86_64 ELF;
- PIE / `ET_DYN`;
- dynamic linking, `PT_INTERP`, or `PT_DYNAMIC`;
- runtime relocations;
- TLS;
- libc, a CRT, or a C userspace runtime;
- argc/argv, envp, or auxv;
- general `exec`, `fork`, `wait`, or an exit syscall;
- `mmap` or `brk`;
- VFS, RAMFS, BoringFS, block devices, or storage drivers;
- scheduled user tasks or user-mode timer preemption;
- signals or copy-on-write;
- per-task/per-CPU syscall entry state or SMP;
- APIC migration;
- networking or display services.

The Milestone 11 loader is therefore a real, validated ELF userspace bootstrap path, but intentionally not yet a general program-loading subsystem.
