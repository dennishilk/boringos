# Native file descriptors and stdio foundation

Milestone 29 adds a deliberately small BoringOS-native descriptor layer. It is
not POSIX and it does not provide libc `FILE *`, `stdio.h`, descriptor
duplication, pipes, redirection, asynchronous I/O or TTY semantics.

## Ownership and bound

Every normal process owns one descriptor table directly in its `struct
process`. The table contains exactly 16 fixed slots. There is no unbounded or
global open-file list.

Descriptor identities are process-local:

- `fd 0` — standard input, console-backed, readable only;
- `fd 1` — standard output, console-backed, writable only;
- `fd 2` — standard error, console-backed, writable only;
- `fd 3..15` — regular-file descriptors allocated from the first free slot.

`stdout` and `stderr` currently use the same real serial-console backend but
remain distinct descriptor identities. Explicit close of descriptors 0, 1 or
2 is rejected in Milestone 29.

A newly created process gets a fresh descriptor table containing only 0, 1 and
2. Regular descriptors owned by a parent are not inherited by a `LAUNCH`
child.

## Regular files and retained VFS handles

`FD_OPEN` resolves a bounded userspace path through the current process CWD,
requires a regular VFS node and retains one existing `vfs_handle` inside the
allocated descriptor slot. Opening a directory as a general descriptor is not
supported; directory enumeration remains `FS_READDIR`.

Milestone 29 open flags are BoringOS-owned:

```text
BORING_FD_OPEN_READ
BORING_FD_OPEN_WRITE
```

There is no `O_*` compatibility surface. `CREATE` and `TRUNCATE` are not part
of Milestone 29; the established filesystem syscalls remain responsible for
creation and replacement-style writes.

Each retained VFS handle already owns its own current offset. Consequently two
separate opens of the same file have independent offsets. `FD_READ` advances
by the number of bytes actually read, `FD_WRITE` advances by the number of
bytes actually written, and a regular-file read that reaches EOF returns 0.

## Syscall ABI

Existing syscall numbers 0 through 17 are unchanged. Milestone 29 adds:

```text
18 FD_OPEN
19 FD_READ
20 FD_WRITE
21 FD_CLOSE
```

There is no `FD_SEEK` in Milestone 29. The fixed maximum transfer for one
`FD_READ` or `FD_WRITE` invocation is 4096 bytes.

Userspace uses the freestanding runtime wrappers:

```text
boring_fd_open(...)
boring_fd_read(...)
boring_fd_write(...)
boring_fd_close(...)
```

No raw userspace pointer is passed to VFS code. Paths and I/O buffers cross the
existing checked user-memory boundary first.

## Console behavior

`FD_READ(0, ...)` consumes bytes through the existing blocking serial RX path.
`FD_WRITE(1, ...)` and `FD_WRITE(2, ...)` write bytes through the existing
serial console backend. Reading fd 1 or 2 and writing fd 0 are rejected by the
descriptor access-mode contract.

## Close and process cleanup

`FD_CLOSE` accepts only an open regular descriptor at fd 3 or above. On a
successful close the retained VFS handle is released, the slot is invalidated
and that slot can be reused.

Process transition to the finished/zombie state destroys the process-owned
descriptor table. All still-open regular handles are closed exactly once
before the process can be reported as finished. `WAITPID` then performs the
existing process/address-space reap. Descriptor-table destruction is
idempotent after successful cleanup, preventing a second close during process
destruction.

## Standalone `cat`

`/bin/cat` is a freestanding static ELF64 `ET_EXEC` program stored in the real
persistent BoringFS `/bin`. It opens its file with `FD_OPEN`, reads
sequentially with `FD_READ`, writes file bytes through `FD_WRITE(1, ...)`,
closes the file descriptor and exits explicitly with `boring_exit(status)`.
It does not use the legacy path-based `FS_READ` syscall or `CONSOLE_WRITE` to
fake the acceptance path.

## Explicit non-goals

Milestone 29 does not add POSIX `fork`/`execve`, libc, `FILE *`, `stdio.h`,
`dup`/`dup2`, pipes, shell redirection, job control, signals, sockets,
networking, permissions/users/groups, authentication, configurable `PATH`, a
dynamic linker, shared libraries, `mmap`, asynchronous or nonblocking I/O,
`poll`/`select`/`epoll`, TTY line discipline, PTYs, framebuffer work, keyboard
driver changes, BoringWM, or Milestone 30 functionality.
