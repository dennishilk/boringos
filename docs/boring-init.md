# boring-init bootstrap model

Milestone 16 introduces the first real long-lived native BoringOS userspace process: `boring-init`.

## Boot path

The current bootstrap acceptance path is deliberately small:

```text
Limine
    ↓
BoringKernel
    ↓
validated boring-init.elf boot module
    ↓
PID 1 + independent x86_64 address space
    ↓
existing ELF64 ET_EXEC loader
    ↓
existing BoringOS C runtime
    ↓
boring-init at CPL3
```

`boring-init.elf` is a freestanding static x86_64 `ET_EXEC` built from the existing BoringOS-owned userspace runtime. It has no host libc or CRT dependency and is loaded through the existing validated ELF loader into PMM-owned process pages.

The bootstrap module is still delivered by Limine. Milestone 16 does not imply that executable loading from VFS, RAMFS or persistent storage exists.

## PID 1 behavior

The initial program uses only the already-established syscall ABI:

- syscall 0: `GETPID`;
- syscall 2: `CONSOLE_WRITE`.

It verifies that it is PID 1, emits deterministic serial-console evidence, touches real writable userspace state, and then remains alive in CPL3.

There is intentionally no exit syscall in this milestone. Returning from `boring_main()` is not used as the PID 1 lifecycle model.

## Acceptance mode

`TEST_MODE=init` uses `BORING_TEST_MODE=9` and is independent from the established syscall, runtime, console, VFS and RAMFS acceptance modes.

The QEMU acceptance requires evidence that:

- the expected `boring-init.elf` Limine module was found;
- the ELF image validated and loaded through the existing loader;
- NX/W^X and supervisor-only higher-half protections remain in force;
- the created process is PID 1;
- the user stack is mapped RW/NX;
- execution enters the ELF entry at CPL3;
- the real userspace program prints `boring-init: starting`, `boring-init: pid 1`, and `boring-init: online` through the existing console syscall;
- no fatal exception or init failure marker occurs before the online state is observed.

The host acceptance harness terminates QEMU only after `boring-init: online` is observed because PID 1 is intentionally long-lived.

## Non-goals

Milestone 16 does not add:

- `boring-shell` or any Milestone 17 behavior;
- a file-descriptor table;
- stdin/stdout/stderr abstractions;
- file-related userspace syscalls;
- executable loading from VFS or RAMFS;
- BoringFS, block devices, partitions or persistent storage;
- an exit syscall or process reaping model;
- general userspace task scheduling;
- networking, display/input, APIC migration or SMP.
