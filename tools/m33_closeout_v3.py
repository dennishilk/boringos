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


# Active current-version witnesses. Historical M32 records are deliberately absent.
active_version_counts = {
    "kernel/core/entry.c": 2,
    "kernel/core/syscall.c": 1,
    "kernel/core/boringfs_ro_test.c": 1,
    "README.md": 1,
    "README.de.md": 1,
    "tests/boot-qemu.sh": 1,
    "tests/framebuffer-qemu.sh": 1,
    "tests/block-device-qemu.sh": 1,
    "tests/boringfs-ro-qemu.sh": 1,
    "tests/boringfs-rw-qemu.sh": 1,
    "tests/console-qemu.sh": 1,
    "tests/elf-qemu.sh": 1,
    "tests/exception-divide-qemu.sh": 1,
    "tests/exception-pagefault-qemu.sh": 1,
    "tests/init-qemu.sh": 1,
    "tests/persistent-root-qemu.sh": 3,
    "tests/ramfs-qemu.sh": 1,
    "tests/ring3-qemu.sh": 1,
    "tests/runtime-qemu.sh": 1,
    "tests/shell-host-test.c": 1,
    "tests/shell-qemu.sh": 2,
    "tests/syscall-qemu.sh": 1,
    "tests/vfs-qemu.sh": 1,
    "tests/virtio-block-qemu.sh": 1,
}
for path, expected in active_version_counts.items():
    replace_exact(path, OLD, NEW, expected)

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

replace_exact(
    "docs/ipc.md",
    "This document is part of the in-progress M33 implementation; the kernel remains `BoringKernel 0.0.33-dev` until Semantic Freeze has passed.",
    "The M33 implementation is Semantic Frozen and accepted; the closeout development banner is `BoringKernel 0.0.34-dev`.",
)

# Roadmap current state only; historical milestone records remain byte-for-byte old-version facts.
p = Path("docs/roadmap.md")
text = p.read_text()
old_banner = "The accepted development banner is now:\n\n```text\nBoringKernel 0.0.33-dev\n```"
new_banner = "The accepted development banner is now:\n\n```text\nBoringKernel 0.0.34-dev\n```"
if text.count(old_banner) != 1:
    raise SystemExit("roadmap current banner anchor mismatch")
text = text.replace(old_banner, new_banner, 1)

abi_marker = "The current syscall ABI is exactly:\n\n```text\n"
if text.count(abi_marker) != 1:
    raise SystemExit("roadmap current ABI marker mismatch")
prefix, suffix = text.split(abi_marker, 1)
end = suffix.find("\n```")
if end < 0:
    raise SystemExit("roadmap current ABI closing fence missing")
abi = suffix[:end]
if not abi.endswith("30 BUFFER_CLOSE") or "31 SERVICE_REGISTER" in abi:
    raise SystemExit("roadmap current ABI content mismatch")
abi += "\n31 SERVICE_REGISTER\n32 SERVICE_CONNECT\n33 SERVICE_ACCEPT\n34 IPC_SEND\n35 IPC_RECEIVE\n36 IPC_CLOSE"
text = prefix + abi_marker + abi + suffix[end:]

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

# Audit all repository text. Only the two historical M32 final-version records may remain.
helper_prefixes = ("tools/m33_closeout",)
helper_exact = {".github/workflows/m33-closeout-helper.yml"}
hits = []
for path in sorted(Path(".").rglob("*")):
    rel = path.as_posix().lstrip("./")
    if not path.is_file() or ".git" in path.parts or "build" in path.parts or rel in helper_exact or rel.startswith(helper_prefixes):
        continue
    try:
        data = path.read_text()
    except UnicodeDecodeError:
        continue
    for number, line in enumerate(data.splitlines(), 1):
        if OLD in line:
            hits.append((rel, number, line.strip()))

allowed = {
    ("docs/roadmap.md", "final version after closeout: BoringKernel 0.0.33-dev"),
    ("docs/userspace-memory.md", "final version after closeout: BoringKernel 0.0.33-dev"),
}
if len(hits) != 2 or {(path, line) for path, _, line in hits} != allowed:
    for hit in hits:
        print(f"remaining old-version hit: {hit[0]}:{hit[1]}: {hit[2]}")
    raise SystemExit("unexpected 0.0.33-dev reference after M33 closeout")

print("M33 closeout applied; exactly two historical M32 0.0.33-dev records preserved")
