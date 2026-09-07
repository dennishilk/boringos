# M38 — Native desktop session supervision

Milestone 38 turns the M37 desktop startup path into a bounded PID 1 session
supervisor. It is intentionally not a general service manager and it does not
restart failed desktop processes.

## Ownership and state

`boring-init` remains PID 1 and owns exactly two central desktop children:

- `/bin/boring-display`
- `/bin/boringwm`

Both are normal waitable direct children created with `SPAWN`. PID 1 tracks the
PID, running/exited/reaped state and exit status of each child. The bounded
session state machine is:

```text
STARTING -> RUNNING -> DRAINING -> DRAINED
                    
STARTING/RUNNING -> FAILED -> DRAINING -> DRAINED
```

The session is started exactly once. `FAILED` records an unexpected central
child exit; it does not panic the kernel, exit PID 1, send a signal, or start a
replacement process. Existing IPC, service-registry, descriptor, PTY, shared
buffer, input and framebuffer teardown propagates the loss through the
remaining desktop stack. PID 1 observes and reaps both central children and
then remains alive.

A non-zero central-child exit status is a session failure. A display exit while
the WM is still unreaped is also a session failure, even if the display status
is zero. The normal M37 order remains clean: WM exits with status 0 after the
last managed client closes, then display drains and exits with status 0.

## WAITPID ABI extension

The syscall numbering remains unchanged. `WAITPID` is still syscall 17 and
`SPAWN` is still syscall 43.

M38 extends the existing syscall 17 selector semantics compatibly:

- `WAITPID(real_pid, status)` keeps the pre-M38 contract unchanged: the target
  must be a waitable direct child of the calling process; the call blocks until
  that exact child is finished, writes its exit status, reaps it exactly once,
  and returns that same PID.
- `WAITPID(0, status)` means **wait for any waitable direct child**. PID 0 is the
  kernel/bootstrap process identifier and was rejected by the pre-M38 scheduled
  userspace WAITPID path, so it cannot collide with a userspace child PID.
- The wait-any selector never observes or reaps another process's child and
  never reaps PID 0.
- Detached children are not wait-any candidates; their existing detached reap
  path is unchanged.
- If a direct waitable child is already finished, wait-any reaps immediately.
  Otherwise the caller is scheduler-blocked. Child exit wakes the waiting
  parent; there is no process-snapshot polling or busy loop.
- If several eligible children are already finished when the parent runs,
  M38 deterministically selects the lowest concrete child PID. The returned
  value is always the concrete reaped PID, never zero.
- The status copy and task/process destruction use the same existing reap path
  as exact-PID WAITPID.

The legacy exact-child path remains covered by the existing M36 real-QEMU SPAWN
and WAITPID acceptance. M38 adds real-QEMU wait-any use through PID 1.

## Failure injection and acceptance

Failure injection exists only in dedicated M38 acceptance binaries:

- the WM acceptance binary exits from real Ring3 with status 72 on an injected
  F12 key-down;
- the display acceptance binary exits from real Ring3 with status 73 on an
  injected F11 key-down.

Those branches are compile-time guarded and are absent from the normal desktop
binaries. They are deterministic QMP/PS2 triggers, not time-based exits.

The permanent M38 acceptance runs three complete sessions:

1. normal M37-compatible desktop lifecycle, including terminal, PTY, shell,
   `boringfetch`, two terminals, focus/input isolation and Super+Q;
2. unexpected WM-first exit and controlled display/session drain;
3. unexpected display-first exit, WM loss detection, and controlled session
   drain.

Every scenario verifies the real kernel/subsystem counters after PID 1 reaps
both central children: one active process, one active task, current PID 1, no
IPC service/connection/message/attachment, no active M32 shared object, no input
owner, no framebuffer claim, and no PTY pair/reference/waiter/queued byte.
Created/finished process and task counters must differ by exactly one because
PID 1 remains alive.

## Non-goals

M38 adds no restart policy, restart loop, general service supervisor, login
manager, user session model, signals, kill API, process groups, POSIX TTY/job
control, pipes, networking, audio, hardware inventory, installer, GUI settings,
BoringEdit, BoringFiles, browser work, or M39 work.
