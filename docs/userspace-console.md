# Early userspace serial console

Milestone 13 adds a deliberately small bootstrap console path between a compiled freestanding C program at CPL3 and BoringKernel. It exists to prove controlled bidirectional userspace I/O before any VFS, file-descriptor, init, shell, TTY, or general device model exists.

## Current syscall contract

The provisional x86_64 syscall ABI remains:

```text
RAX = syscall number
RDI = arg0
RSI = arg1
RDX = arg2
R10 = arg3
R8  = arg4
R9  = arg5

RAX = result
RCX/R11 = architectural SYSCALL/SYSRETQ clobbers
```

The currently dispatched provisional syscall numbers are exactly:

```text
0  BORING_SYS_GETPID
1  BORING_SYS_DEBUG_WRITE
2  BORING_SYS_CONSOLE_WRITE
3  BORING_SYS_CONSOLE_READ
```

`DEBUG_WRITE` keeps its existing semantics. `CONSOLE_WRITE` and `CONSOLE_READ` are the only Milestone-13 additions. They are not POSIX `write()`/`read()` and do not take file descriptors.

Both console calls accept 1 through 64 bytes. Zero length and lengths above 64 are rejected.

## Output path

`boring_console_write(buffer, length)` executes a real `SYSCALL` through the existing BoringOS runtime wrapper. BoringKernel validates the complete userspace source range in the current process address space and copies it through the existing controlled `copy_from_user` boundary into a bounded kernel buffer. Only after the entire copy succeeds does the kernel emit the accepted bytes through COM1.

That ordering is intentional: an invalid, unmapped, overflowing, higher-half/kernel, or otherwise inaccessible source fails before any partial console output is produced. Userspace never accesses COM1 I/O ports directly.

## Input path

`boring_console_read(buffer, length)` also enters through the same real syscall ABI. Before BoringKernel consumes any serial input, it validates the complete destination range page by page in the current process address space and requires every covered mapping to be writable userspace memory.

This pre-validation rejects null/unmapped destinations, kernel or higher-half addresses, read-only userspace mappings, invalid or overflowing ranges, and lengths outside the current 1..64-byte contract before COM1 RX is touched. Only after that validation succeeds does the kernel poll COM1 and collect the requested bytes into a bounded kernel buffer. The completed input is then copied into the process through the controlled writable `copy_to_user` path.

The kernel may use its HHDM internally to access translated physical pages, but no HHDM or other kernel alias is exposed to userspace and no zero-copy device mapping is created.

## Current COM1 backend and blocking model

For the QEMU bootstrap target, COM1 is the current console backend. Transmission polls the UART transmit-ready state and writes through the kernel's x86 I/O helpers. Receive is intentionally primitive: `CONSOLE_READ` performs blocking, polled COM1 input.

The current receive path is **not** any of the following:

- a TTY subsystem;
- a line discipline;
- canonical or raw terminal modes;
- terminal editing;
- a file descriptor;
- `stdin`, `stdout`, or `stderr`;
- asynchronous I/O;
- interrupt-driven UART receive;
- scheduler-aware sleep/wakeup;
- multiplexed terminal sessions.

A caller waiting in `CONSOLE_READ` therefore occupies the current execution path until the requested serial bytes arrive. That is an accepted bootstrap limitation for Milestone 13.

## BoringOS runtime wrappers

The public freestanding runtime additions are deliberately limited to:

```c
long boring_console_write(const void *buffer, size_t length);
long boring_console_read(void *buffer, size_t length);
```

They use the existing native `SYSCALL` instruction path. The inline assembly supplies the syscall number in RAX and arguments through the established ABI, returns the result in RAX, and declares RCX, R11, condition-code, and compiler memory clobbers as required. No serial hardware header or x86 IN/OUT interface is exposed to userspace.

## Console smoke executable

The acceptance client is a separate real executable at:

```text
build/user/console-smoke.elf
```

It is linked from the same BoringOS-owned minimal runtime objects used by the native C runtime acceptance; there is no second runtime library or fork. The image contains `_start`, `boring_main`, `boring_getpid`, `boring_console_write`, `boring_console_read`, `boring_memcpy`, `boring_memset`, and `boring_strlen`.

The artifact audit requires ELF64, little-endian x86_64 `ET_EXEC`, static fixed-address linking, no `PT_INTERP`, no `PT_DYNAMIC`, no `NEEDED` libraries, no relocations, no unresolved symbols, no TLS, no PIE/`ET_DYN`, W^X-compliant load segments, and no host libc or CRT entry points.

## Real QEMU receive acceptance

The console QEMU gate uses a named serial pipe rather than preloading process stdin. The deterministic sequence is:

1. QEMU starts with COM1 connected to a named pipe.
2. The compiled CPL3 C program reaches `boring_console_write()` and the host observes the exact line `console write from BoringOS userspace`.
3. Only then does the test inject exactly the byte `K` into the QEMU serial input pipe.
4. BoringKernel receives that real COM1 RX byte.
5. The kernel copies it through writable-userspace validation and `copy_to_user` into a local variable on the program's real CPL3 stack.
6. `SYSRETQ` resumes the same compiled C program.
7. C observes ASCII value 75 and writes `K\n` back through `boring_console_write()`.
8. `boring_main()` returns 43.
9. The existing runtime `_start` continuation executes the deliberate CPL3 `cli`; the CPU raises a real #GP and the handler proves the CPL3 origin and TSS.RSP0 kernel-stack transition.
10. The loaded image and process are torn down and PMM free-frame bookkeeping must return exactly to its pre-load value.

The dedicated console test mode is `TEST_MODE=console` with `BORING_TEST_MODE=6`. It selects the console harness, `console-smoke.elf`, and its own Limine module identity without repurposing the native runtime test mode.

## Relationship to later work

This path deliberately has no VFS relationship yet. There are no file descriptors, path lookups, filesystem objects, mounts, block devices, `open/read/write` POSIX APIs, init process, shell, command parser, terminal editor, or general device registry behind these calls.

Future VFS/init/shell work can build higher-level abstractions after this low-level privilege and memory boundary is stable. Milestone 13 does not pre-design those interfaces and does not generalize COM1 into a device framework.

## Known limitations

- QEMU COM1 is the only accepted backend for this bootstrap path.
- RX is polling and blocking.
- Transfers are bounded to 64 bytes per call.
- There is no buffering policy beyond the bounded per-call kernel buffer.
- There is no scheduler-aware blocking or cancellation.
- The syscall numbers and wrappers remain provisional and are not a stable public ABI.
