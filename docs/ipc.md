# BoringOS native IPC / service foundation (M33)

M33 adds a deliberately small, bounded, connection-oriented IPC layer. This document is part of the in-progress M33 implementation; the kernel remains `BoringKernel 0.0.33-dev` until Semantic Freeze has passed.

## Scope

The model consists of named service listeners and full-duplex endpoint pairs. A service owner registers a small ASCII name, clients connect by name, and the server accepts pending connections. Each endpoint owns a bounded incoming FIFO. `IPC_SEND` copies a bounded control payload into kernel-owned queue storage. `IPC_RECEIVE` consumes the oldest message only after all receive-side validation and capability installation can succeed.

The only transferable capability in M33 is an existing M32 shared-buffer capability. Transfer is grant/copy, not move: the sender retains its local handle, a queued message retains the backing object, and a successful receive installs a new process-local generation-protected M32 handle in the receiver. Both handles reference the same kernel-owned backing pages.

## Fixed bounds

- service name: 1..31 bytes
- global live services: 16
- IPC handles per process: 32
- global connections: 64
- pending connections per service: 16
- queued messages per direction: 16
- inline payload: at most 256 bytes
- attached M32 buffers per message: zero or one

No userspace-controlled unbounded kernel allocation is part of M33.

## Service namespace

Service names are not paths. Names are case-sensitive lowercase ASCII and may contain only `a-z`, `0-9`, `.`, `_`, and `-`. Empty names, embedded NUL bytes, uppercase letters, slashes, control characters, non-ASCII bytes, and names longer than 31 bytes are rejected. The registry stores a bounded kernel-owned copy and never retains a Ring-3 pointer.

The first live registration owns a name. A duplicate live registration fails. Closing a listener or exiting its owner unregisters the name. There are no users/groups, ACLs, namespaces, authentication rules, or privileged-service policy in M33; the registry is not a complete security-policy mechanism.

## Handles and connection lifetime

IPC handles are process-local, typed (`LISTENER` or `ENDPOINT`), bounded and generation-protected. A numeric handle from another process is not authority. PIDs are not capabilities.

Closing one endpoint marks that side closed. Messages already queued for the peer may be drained in FIFO order. Once that queue is empty, receive reports peer closure. New sends toward a closed peer fail. A connection object is reclaimed only after both sides are closed and all queued message resources, including retained M32 buffer references, are gone.

## Blocking model

`SERVICE_ACCEPT` and `IPC_RECEIVE` block without busy polling. M33 reuses the existing single-CPU scheduler/interrupt model and adds only the minimum wait/wake state needed to sleep a task and wake it when a pending connection, message, or peer-close event changes the condition. Condition checks and the transition into the wait state are protected against lost wakeups; user-memory copies are performed outside long interrupt-disabled sections.

The pre-M33 syscall entry path uses one trusted syscall stack and explicitly assumes a non-preemptible bootstrap user path. Real multi-process blocking IPC therefore uses each existing process-bound cooperative task's bounded 16 KiB kernel stack as that process's trusted syscall stack while it runs. The historical global syscall stack remains the bootstrap/fallback path. This is an M33 safety prerequisite, not a timer-preemption or scheduler-policy redesign.

## Syscall ABI

M33 reserves the previously free slots 31..36 while preserving slots 0..30 unchanged:

- 31 `SERVICE_REGISTER`
- 32 `SERVICE_CONNECT`
- 33 `SERVICE_ACCEPT`
- 34 `IPC_SEND`
- 35 `IPC_RECEIVE`
- 36 `IPC_CLOSE`

Receive uses a fixed public result structure containing payload length, an optional receiver-local M32 buffer handle, and reserved flags. Buffer handle zero is the existing M32 invalid-handle value and is used as the no-attached-buffer sentinel. Reserved flags are zero in M33.

## Transactionality

Send validates the endpoint, payload bound/range, optional sender buffer handle, peer state and queue capacity before committing a queue entry. Failure does not enqueue a message or leak an attachment reference.

Receive peeks at the front message, validates destination ranges and capacity, verifies that an attached buffer can be installed in the receiver, copies output, then commits exactly one receiver handle and removes exactly one message. Failed receive attempts leave the message queued and do not duplicate capabilities.

## Process exit

Process exit closes all IPC handles, unregisters owned listeners, closes endpoint sides, wakes affected peers/waiters, releases pending connections and queued messages, and releases queued M32 buffer references before the established M32 userspace-memory cleanup runs. Cleanup is bounded and idempotent.

## Non-goals

M33 does not implement sockets, pipes, POSIX IPC, networking, arbitrary capability transfer, file-descriptor transfer, service ACL/security policy, signals, SMP scheduling, display surfaces, framebuffer composition, `boring-display`, cursor handling, BoringWM, terminal UI, audio, GPU acceleration, or M34.
