#!/usr/bin/env python3
from pathlib import Path

OLD = "0.0.33-dev"
NEW = "0.0.34-dev"


def replace_exact(path: str, old: str, new: str, expected: int = 1) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != expected:
        raise SystemExit(f"closeout anchor mismatch {path}: expected {expected}, got {count}: {old!r}")
    p.write_text(text.replace(old, new, expected))


# Active runtime/version witnesses only.
replace_exact("kernel/core/entry.c", OLD, NEW, 2)
replace_exact("kernel/core/syscall.c", OLD, NEW, 1)
replace_exact("tests/boot-qemu.sh", f"BoringKernel {OLD}", f"BoringKernel {NEW}", 1)
replace_exact("tests/framebuffer-qemu.sh", f"BoringKernel {OLD}", f"BoringKernel {NEW}", 1)
replace_exact("README.md", f"BoringKernel {OLD}", f"BoringKernel {NEW}", 1)
replace_exact("README.de.md", f"BoringKernel {OLD}", f"BoringKernel {NEW}", 1)

# Current README status: add only the newly frozen M33 capability summary.
replace_exact(
    "README.md",
    "plus M32 dynamic anonymous Ring-3 memory, a minimal userspace heap, and generic kernel-owned shared byte buffers with process-local capability handles and `/bin/memory-test`.",
    "plus M32 dynamic anonymous Ring-3 memory, a minimal userspace heap, and generic kernel-owned shared byte buffers with process-local capability handles and `/bin/memory-test`, plus the M33 bounded native service registry and blocking connection-oriented IPC with transactional M32 shared-buffer capability grants and `/bin/ipc-test`.",
)
replace_exact(
    "README.md",
    "the M30 framebuffer foundation, the M31 native PS/2 input foundation, and the M32 userspace-memory/shared-buffer foundation;",
    "the M30 framebuffer foundation, the M31 native PS/2 input foundation, the M32 userspace-memory/shared-buffer foundation, and the M33 native IPC/service/capability-grant foundation;",
)
replace_exact(
    "README.de.md",
    "sowie die dynamische anonyme Ring-3-Speicherverwaltung aus M32, einen minimalen Userspace-Heap und generische kernel-eigene Shared-Byte-Buffer mit prozesslokalen Capability-Handles und `/bin/memory-test`.",
    "sowie die dynamische anonyme Ring-3-Speicherverwaltung aus M32, einen minimalen Userspace-Heap und generische kernel-eigene Shared-Byte-Buffer mit prozesslokalen Capability-Handles und `/bin/memory-test` sowie die begrenzte native M33-Service-Registry und blockierendes verbindungsorientiertes IPC mit transaktionalen M32-Shared-Buffer-Capability-Grants und `/bin/ipc-test`.",
)
replace_exact(
    "README.de.md",
    "die M30-Framebuffer-Grundlage, die native M31-PS/2-Input-Grundlage und die M32-Userspace-Memory-/Shared-Buffer-Grundlage;",
    "die M30-Framebuffer-Grundlage, die native M31-PS/2-Input-Grundlage, die M32-Userspace-Memory-/Shared-Buffer-Grundlage und die native M33-IPC-/Service-/Capability-Grant-Grundlage;",
)

# M33 document moves from in-progress freeze text to accepted closeout state.
replace_exact(
    "docs/ipc.md",
    "This document is part of the in-progress M33 implementation; the kernel remains `BoringKernel 0.0.33-dev` until Semantic Freeze has passed.",
    "The M33 implementation is Semantic Frozen and accepted; the closeout development banner is `BoringKernel 0.0.34-dev`.",
)

# Roadmap: update only current state, preserve historical M32 evidence verbatim.
p = Path("docs/roadmap.md")
text = p.read_text()
old_banner = "The accepted development banner is now:\n\n```text\nBoringKernel 0.0.33-dev\n```"
new_banner = "The accepted development banner is now:\n\n```text\nBoringKernel 0.0.34-dev\n```"
if text.count(old_banner) != 1:
    raise SystemExit("roadmap current banner anchor mismatch")
text = text.replace(old_banner, new_banner, 1)

old_syscalls = """25 MEMORY_ALLOC
26 MEMORY_FREE
27 BUFFER_CREATE
28 BUFFER_MAP
29 BUFFER_UNMAP
30 BUFFER_CLOSE
```"""
new_syscalls = """25 MEMORY_ALLOC
26 MEMORY_FREE
27 BUFFER_CREATE
28 BUFFER_MAP
29 BUFFER_UNMAP
30 BUFFER_CLOSE
31 SERVICE_REGISTER
32 SERVICE_CONNECT
33 SERVICE_ACCEPT
34 IPC_SEND
35 IPC_RECEIVE
36 IPC_CLOSE
```"""
if text.count(old_syscalls) != 1:
    raise SystemExit("roadmap syscall list anchor mismatch")
text = text.replace(old_syscalls, new_syscalls, 1)

old_current = """Milestone 32 adds
bounded dynamic anonymous Ring-3 memory, the minimal native userspace heap and
generic kernel-owned shared byte buffers with process-local capability handles,
multiple alias mappings and process-exit reclamation. There is still no partition
layer, networking, cross-process buffer transfer, service registry, cursor, display
server, terminal graphics stack or GUI/window system."""
new_current = """Milestone 32 adds
bounded dynamic anonymous Ring-3 memory, the minimal native userspace heap and
generic kernel-owned shared byte buffers with process-local capability handles,
multiple alias mappings and process-exit reclamation. Milestone 33 adds the bounded
native service registry, blocking connection-oriented IPC and transactional grants
of existing M32 shared-buffer capabilities between processes. There is still no
partition layer, networking, display/surface protocol, cursor, `boring-display`,
terminal graphics stack, BoringWM or GUI/window system."""
if text.count(old_current) != 1:
    raise SystemExit("roadmap current M32 boundary anchor mismatch")
text = text.replace(old_current, new_current, 1)

m33_section = r'''

---

# Stage 17 — native IPC and service foundation

## Milestone 33: native IPC and service foundation — COMPLETE

Milestone 33 adds a deliberately bounded, connection-oriented native IPC layer
above the existing process and scheduler foundations. A fixed global registry
owns bounded kernel copies of service names; process-local generation-protected
listener/endpoint handles carry authority, and PIDs do not.

`SERVICE_ACCEPT` and `IPC_RECEIVE` block without busy polling. The existing
cooperative process-bound 16-KiB task stack becomes that process's trusted
SYSCALL stack while it runs, allowing a blocked syscall to sleep and a different
process to enter the kernel safely. Wakeups cover pending connections, queued
messages and peer-close transitions without weakening the established scheduler
or introducing timer-preemptive userspace scheduling.

M33 extends the syscall ABI only in the previously free slots 31..36:

```text
31 SERVICE_REGISTER
32 SERVICE_CONNECT
33 SERVICE_ACCEPT
34 IPC_SEND
35 IPC_RECEIVE
36 IPC_CLOSE
```

Inline control payloads are bounded to 256 bytes. The only transferable
capability is an existing M32 shared-buffer capability. Transfer is grant/copy,
not move: the sender retains its local handle, a queued message retains the
kernel-owned backing object, and a successful receive transaction installs a new
receiver-local generation-protected handle referring to the same backing pages.
Failed sends/receives do not partially enqueue, dequeue or duplicate capability
state.

Process exit closes remaining IPC handles, unregisters owned listeners, wakes
peers/waiters, releases pending/queued IPC state and retained M32 buffer
references, then proceeds through the established M32 userspace-memory cleanup.
The three-process CPL3 acceptance proves three distinct process address spaces,
service register/connect/accept, blocking wakeups, FIFO/queue-full transaction
behavior, negative syscall paths, shared-buffer grants and aliases, peer close,
service removal/re-registration, process-local handle isolation, resource
reclamation and PMM recovery.

Acceptance record:

```text
PR: #44
base: 97abc3849145a66d84e938bc51d0f5c5d4670f41
Semantic Freeze SHA: 72ba384fa46c23a7115940da47e6197adb11a2cb
Semantic Freeze tree: 3191d784a65ef067a3469738245a1d16492767e7
exact-head Semantic Freeze CI: Run #399 / 32976425779 / SUCCESS
final version after closeout: BoringKernel 0.0.34-dev
```

The permanent QEMU bundle keeps the historical fixture geometries intact:
legacy fixtures remain 64 blocks, M32's `/bin/memory-test` fixture remains 80
blocks, and only the M33 bundle including `/bin/ipc-test` uses 96 blocks.

M33 does **not** add a display/surface protocol, framebuffer ownership transfer,
cursor, compositor, `boring-display`, terminal graphics stack, BoringWM or GUI
application. Those remain outside the M33 Semantic Freeze.
'''
if "## Milestone 33: native IPC and service foundation — COMPLETE" in text:
    raise SystemExit("roadmap already contains M33 closeout section")
text += m33_section
p.write_text(text)

# Final closeout audit: the only retained 0.0.33-dev reference is the historical
# M32 final-version record in docs/roadmap.md.
hits = []
for path in sorted(Path(".").rglob("*")):
    if not path.is_file() or ".git" in path.parts or "build" in path.parts:
        continue
    try:
        data = path.read_text()
    except UnicodeDecodeError:
        continue
    for number, line in enumerate(data.splitlines(), 1):
        if OLD in line:
            hits.append((path.as_posix(), number, line.strip()))

if len(hits) != 1 or hits[0][0] != "docs/roadmap.md" or \
   "final version after closeout: BoringKernel 0.0.33-dev" not in hits[0][2]:
    for hit in hits:
        print(f"remaining old-version hit: {hit[0]}:{hit[1]}: {hit[2]}")
    raise SystemExit("unexpected 0.0.33-dev reference after M33 closeout")

print("M33 closeout applied; historical M32 0.0.33-dev preserved exactly once")
