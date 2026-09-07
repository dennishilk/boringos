# BoringOS userspace executable loading

Milestone 28 adds filesystem-backed static userspace program loading without
adopting the POSIX process model.

## One bounded ELF source contract

The ELF64 validator/loader consumes a bounded `boring_elf_source`. The
existing Limine boot-module path is exposed through a memory adapter and VFS
regular files through a VFS adapter. Both paths therefore share the same ELF
header, program-header, segment, virtual-address, entry-point, W^X and
rollback checks.

VFS execution is limited to regular files. Reads are bounded and performed
through the VFS source adapter; the kernel does not create a second whole-file
shadow copy merely to execute a program.

## Supported executable format

M28 intentionally supports only static freestanding little-endian x86_64
`ET_EXEC` ELF64 programs with validated `PT_LOAD` segments. Load segments must
remain within allowed Ring-3 virtual-address bounds, may not overlap or
overflow, writable+executable mappings are rejected, the entry point must be
executable, and BSS bytes are zero-initialized.

There is no dynamic linker or shared-library ABI in M28.

## Native foreground launch model

`LAUNCH` creates a BoringOS child process with its own address space and
inherited VFS current working directory. It is a native foreground handoff,
not POSIX `fork()` plus `exec()`.

The launched child exits through `EXIT`. Its status is preserved while the
process is a non-runnable zombie. The parent uses `WAITPID` for the exact
child and reaps its process resources before continuing. PID allocation is
monotonic; a rejected launch may therefore leave a PID-number gap without
leaving a live process or zombie behind.

## Path resolution

The shell has no configurable `PATH`.

- A command name containing `/` is an explicit path and is passed as supplied.
- A command name without `/` resolves through the fixed `/bin/<name>` rule.
- Command completion can enumerate regular files from the real `/bin` VFS
  directory when it exists.

## argc/argv ABI

BoringOS copies launch arguments across the syscall boundary with explicit
bounds:

- maximum argument count: 16;
- maximum copied argument bytes in total: 1024;
- maximum launch path length: 1024 bytes.

The child receives `RDI = argc` and `RSI = argv` at its userspace entry point.
Argument strings and the argv vector are prepared on the existing 4 KiB
userspace stack. That stack remains writable and non-executable.

This is a BoringOS ABI contract, not a claim of System V process-start or POSIX
compatibility.

## `/bin/boringfetch`

`boringfetch` is a standalone freestanding ELF program in M28. The shell no
longer contains a `boringfetch` built-in. Persistent-root fixtures and the
human-runnable QEMU bundle seed the actual program as `/bin/boringfetch` in
BoringFS.

Acceptance covers bare-name resolution, explicit-path execution, `/bin`
completion, argc/argv delivery, child exit status, wait/reap, malformed ELF
rejection without a process leak, repeated launches, and execution again after
reboot from the same persistent filesystem image.

## Deliberate exclusions

M28 does not add a dynamic linker, shared libraries, POSIX `fork`/`exec`,
signals, job control, pipes, redirection, configurable `PATH`, permissions,
package management, framebuffer support, networking or BoringWM.
