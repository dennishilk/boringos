#!/usr/bin/env python3
from pathlib import Path
import subprocess

OLD = "0.0.31-dev"
NEW = "0.0.32-dev"


def replace_once(path, old, new):
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one closeout anchor, found {count}")
    p.write_text(text.replace(old, new, 1))


def grep_version():
    proc = subprocess.run(
        ["git", "grep", "-n", OLD], text=True, capture_output=True
    )
    if proc.returncode not in (0, 1):
        raise SystemExit(proc.stderr)
    return [line for line in proc.stdout.splitlines() if line]


before = grep_version()
print("M31 closeout pre-change 0.0.31-dev matches:")
for line in before:
    print(line)

for line in before:
    path = line.split(":", 1)[0]
    if path in ("README.md", "README.de.md", "docs/roadmap.md"):
        continue
    if path.startswith("kernel/") or path.startswith("tests/"):
        continue
    raise SystemExit(f"unexpected 0.0.31-dev location: {line}")

# Active runtime/test version contracts. Kernel and test-tree references are
# current executable acceptance contracts, not historical milestone prose.
for line in before:
    path = line.split(":", 1)[0]
    if not (path.startswith("kernel/") or path.startswith("tests/")):
        continue
    p = Path(path)
    text = p.read_text()
    if OLD in text:
        p.write_text(text.replace(OLD, NEW))

# README current-state text only.
replace_once("README.md", "BoringKernel 0.0.31-dev", "BoringKernel 0.0.32-dev")
replace_once("README.de.md", "BoringKernel 0.0.31-dev", "BoringKernel 0.0.32-dev")

replace_once(
    "README.md",
    "a 16-slot per-process native descriptor/stdio foundation used by standalone `/bin/boringfetch` and `/bin/cat` from persistent BoringFS, and an optional validated native framebuffer graphics foundation with a one-shot kernel-rendered graphical boot dashboard.",
    "a 16-slot per-process native descriptor/stdio foundation used by standalone `/bin/boringfetch` and `/bin/cat` from persistent BoringFS, an optional validated native framebuffer graphics foundation with a one-shot kernel-rendered graphical boot dashboard, and a bounded native i8042/PS/2 keyboard-and-mouse input foundation exposed through exclusive blocking Ring-3 event syscalls and standalone `/bin/input-test`.",
)
replace_once(
    "README.de.md",
    "eine prozesslokale native 16-Slot-Descriptor-/stdio-Schicht für die eigenständigen `/bin/boringfetch` und `/bin/cat` im persistenten BoringFS sowie eine optionale validierte native Framebuffer-Grafikgrundlage mit einem einmalig vom Kernel gerenderten grafischen Boot-Dashboard.",
    "eine prozesslokale native 16-Slot-Descriptor-/stdio-Schicht für die eigenständigen `/bin/boringfetch` und `/bin/cat` im persistenten BoringFS, eine optionale validierte native Framebuffer-Grafikgrundlage mit einem einmalig vom Kernel gerenderten grafischen Boot-Dashboard sowie eine begrenzte native i8042-/PS/2-Tastatur-und-Maus-Input-Grundlage mit exklusiven blockierenden Ring-3-Event-Syscalls und dem eigenständigen `/bin/input-test`.",
)
replace_once(
    "README.md",
    "the M29 native descriptor/stdio layer; the exact current state is tracked in [`docs/roadmap.md`](docs/roadmap.md).",
    "the M29 native descriptor/stdio layer, the M30 framebuffer foundation and the M31 native PS/2 input foundation; the exact current state is tracked in [`docs/roadmap.md`](docs/roadmap.md).",
)
replace_once(
    "README.de.md",
    "die native M29-Descriptor-/stdio-Schicht; den exakten aktuellen Stand dokumentiert [`docs/roadmap.md`](docs/roadmap.md).",
    "die native M29-Descriptor-/stdio-Schicht, die M30-Framebuffer-Grundlage und die native M31-PS/2-Input-Grundlage; den exakten aktuellen Stand dokumentiert [`docs/roadmap.md`](docs/roadmap.md).",
)

# Running guide: preserve serial shell semantics while documenting the real M31
# input consumer. No graphical shell/cursor/display-server claim is introduced.
replace_once(
    "docs/RUNNING.md",
    "The graphical screen is informational in Milestone 30; keyboard input remains\nserial-only.\n",
    "The graphical dashboard remains informational. Milestone 31 adds native PS/2\nkeyboard and mouse events, but the shell's stdin/stdout remain on the serial\nconsole. From the shell, `/bin/input-test` claims the native input stream and\nblocks until real keyboard/mouse events arrive; its `--teardown` mode is used by\nthe lifecycle acceptance. There is still no cursor, graphical shell, display\nserver or window system.\n",
)

# Roadmap current-state banner only; historical M30 0.0.31-dev records below
# remain untouched by replacing this exact top block once.
replace_once(
    "docs/roadmap.md",
    "The accepted development banner is now:\n\n```text\nBoringKernel 0.0.31-dev\n```",
    "The accepted development banner is now:\n\n```text\nBoringKernel 0.0.32-dev\n```",
)
replace_once(
    "docs/roadmap.md",
    "20 FD_WRITE\n21 FD_CLOSE\n```",
    "20 FD_WRITE\n21 FD_CLOSE\n22 INPUT_CLAIM\n23 INPUT_READ\n24 INPUT_RELEASE\n```",
)
replace_once(
    "docs/roadmap.md",
    "VFS-backed executable loading and standalone `/bin/boringfetch` are real\nsince Milestone 28. Milestone 29 adds the bounded per-process descriptor and\nstandard-I/O foundation plus standalone `/bin/cat`. There is still no partition\nlayer, networking or GUI.",
    "VFS-backed executable loading and standalone `/bin/boringfetch` are real\nsince Milestone 28. Milestone 29 adds the bounded per-process descriptor and\nstandard-I/O foundation plus standalone `/bin/cat`; Milestone 30 adds optional\nkernel framebuffer output; Milestone 31 adds bounded native PS/2 keyboard/mouse\nevents and exclusive blocking userspace input ownership. There is still no\npartition layer, networking, cursor, display server, terminal graphics stack or\nGUI/window system.",
)

roadmap = Path("docs/roadmap.md")
roadmap_text = roadmap.read_text()
if "## Milestone 31: native keyboard and mouse input foundation — COMPLETE" in roadmap_text:
    raise SystemExit("roadmap already contains M31 closeout")
roadmap_text += r'''

---

# Stage 15 — native input foundation

## Milestone 31: native keyboard and mouse input foundation — COMPLETE

Milestone 31 adds a bounded BoringOS-owned native input path for the current
QEMU x86_64 reference machine. The i8042 controller initializes translated PS/2
Set-1 keyboard input and three-byte PS/2 mouse packets with bounded waits,
explicit ACK/RESEND handling and non-fatal unavailable-device fallback. IRQ1 and
IRQ12 are routed through the existing legacy PIC/IDT path without changing the
historical timer-only acceptance contract.

Hardware bytes are normalized into the fixed 24-byte BoringOS input-event ABI.
Keyboard events use project-owned keycodes, make/break state, Shift/Ctrl/Alt/Super
modifier bits and an explicit repeat flag. Mouse events expose relative X/Y and
left/middle/right button transitions. The kernel owns one fixed 128-event FIFO;
overflow drops the newest event and increments a saturating dropped-event
counter.

Userspace access is deliberately exclusive and narrow:

```text
22 INPUT_CLAIM
23 INPUT_READ
24 INPUT_RELEASE
```

One PID owns the input stream at a time. Same-owner claim is idempotent, another
PID receives busy/access errors, explicit release clears queue/key state, and
process exit releases stale ownership before the parent resumes. `INPUT_READ`
accepts 1..16 events, validates the complete writable destination before any
dequeue, and blocks in the kernel when the queue is empty. On the current
single-CPU trusted-SYSCALL-stack design the wait arm and empty check run with
interrupts disabled and `sti; hlt` closes the empty-to-sleep race; wakeup always
rechecks the queue. Userspace does not busy-poll.

The standalone static `/bin/input-test` exercises the real Ring-3 wrappers and
prints normalized keyboard/mouse events. Host tests cover keyboard decoding,
E0/malformed recovery, modifiers/repeat, mouse sign/button/overflow/resync,
queue FIFO/wrap/overflow/ownership/cleanup, and real CPL3 negative syscall
cases. The permanent QEMU acceptance injects actual QMP keyboard and mouse
input, proves Super+Q, Super+Enter, relative movement, left-button down/up,
blocking wakeup and owner teardown/reclaim while preserving the complete
historical BoringOS suite and M30 framebuffer acceptance.

Semantic freeze acceptance record:

```text
feature head: 065fa085ee7534012037e9eab9b3cfbdcacec39e
feature tree: b9687ac96c097fba2e262206ffee25d64946b88c
exact-head permanent CI: Run 32946932189 / SUCCESS
final version after closeout: BoringKernel 0.0.32-dev
```

M31 does not add a cursor, framebuffer mapping for userspace, `boring-display`,
a terminal graphics stack, GUI clients, BoringWM, USB input, general HID, SMP
input routing or Milestone 32 implementation. The next roadmap target is the
native display/compositor/window-system foundation; it is not part of M31.
'''
roadmap.write_text(roadmap_text)

# Keep the syscall document's historical M11 body, but make its current surface
# truthful and add a bounded M31-specific contract section.
replace_once(
    "docs/syscalls.md",
    "0.0.11-dev boundary is retained below; this section records the current M29\nextensions that supersede its two-call inventory and process-lifecycle\nnon-goals.",
    "0.0.11-dev boundary is retained below; this section records the current M31\nextensions that supersede its two-call inventory and process-lifecycle\nnon-goals.",
)
replace_once("docs/syscalls.md", "## Current M29 syscall surface", "## Current M31 syscall surface")
replace_once(
    "docs/syscalls.md",
    " 0 GETPID             9 FS_READ            18 FD_OPEN\n 1 DEBUG_WRITE       10 FS_TOUCH           19 FD_READ\n 2 CONSOLE_WRITE     11 FS_WRITE           20 FD_WRITE\n 3 CONSOLE_READ      12 FS_UNLINK          21 FD_CLOSE\n 4 LAUNCH            13 INFO\n 5 FS_READDIR        14 GETCWD\n 6 FS_MKDIR          15 PROCESS_SNAPSHOT\n 7 FS_RMDIR          16 EXIT\n 8 FS_CHDIR          17 WAITPID",
    " 0 GETPID             9 FS_READ            18 FD_OPEN\n 1 DEBUG_WRITE       10 FS_TOUCH           19 FD_READ\n 2 CONSOLE_WRITE     11 FS_WRITE           20 FD_WRITE\n 3 CONSOLE_READ      12 FS_UNLINK          21 FD_CLOSE\n 4 LAUNCH            13 INFO               22 INPUT_CLAIM\n 5 FS_READDIR        14 GETCWD             23 INPUT_READ\n 6 FS_MKDIR          15 PROCESS_SNAPSHOT   24 INPUT_RELEASE\n 7 FS_RMDIR          16 EXIT\n 8 FS_CHDIR          17 WAITPID",
)
replace_once(
    "docs/syscalls.md",
    "The current additional symbolic error `EISDIR` maps directory-vs-regular-file\nmisuse without changing underlying VFS semantics.\n\n## Original 0.0.11-dev scope",
    "The current additional symbolic error `EISDIR` maps directory-vs-regular-file\nmisuse without changing underlying VFS semantics.\n\n## Milestone 31 input calls\n\n`INPUT_CLAIM` gives the current process exclusive ownership of the native input\nqueue; a repeated claim by the same PID succeeds and a competing PID is rejected.\n`INPUT_RELEASE` requires the owner and clears queued events and held modifier\nstate. Process exit performs the same ownership cleanup before the parent resumes.\n\n`INPUT_READ(events, max_events)` requires `max_events` in the inclusive range\n1..16 and validates the complete writable user destination before dequeuing. An\nempty owned queue blocks inside the kernel rather than polling in userspace. On\nthe current single-CPU trusted syscall stack the empty check and wait arm are\natomic with interrupts disabled, then `sti; hlt` waits for an interrupt and the\nqueue is rechecked after wakeup. The ABI event is the fixed 24-byte structure\ndocumented in `docs/input.md`; queue capacity is 128 events with drop-newest\noverflow accounting.\n\nThese calls expose normalized keyboard/mouse events only. They do not introduce\na TTY, cursor, display server, windowing API, USB HID or a general asynchronous\nI/O facility.\n\n## Original 0.0.11-dev scope",
)

# New M31 architecture document.
input_doc = r'''# Native input foundation

Milestone 31 adds the first BoringOS-owned keyboard and mouse path. It is a
bounded input foundation for the current QEMU x86_64 reference machine, not a
TTY, GUI, cursor, USB HID stack or general device framework.

## Hardware boundary

The driver uses the legacy i8042 controller and the existing PIC/IDT path.
Initialization disables both PS/2 ports, performs a bounded output flush, reads
and writes the controller configuration with bounded waits, enables translated
keyboard Set 1, and configures keyboard and mouse with PS/2 defaults plus
reporting. ACK/RESEND handling is bounded to three command attempts.

The keyboard is IRQ1 and the mouse is IRQ12. The IRQ handler reads controller
status before the data byte and uses the AUX/source bit to keep keyboard and
mouse streams separate. Timeout/parity status resets the relevant decoder.
Missing or unsupported devices are non-fatal: serial boot and the rest of
BoringOS continue.

## Keyboard normalization

The keyboard decoder consumes translated Set-1 make/break bytes. It handles E0
extended keys and discards the bounded E1 Pause sequence. Unknown/malformed E0
input resets the prefix so the next valid key can decode normally.

The public BoringOS keycode enum is independent of raw scan codes. It includes
A-Z, 0-9, Escape, Tab, Enter, Backspace, Space, punctuation, Insert/Delete,
Home/End/PageUp/PageDown, arrows, F1-F12 and left/right Shift, Ctrl, Alt and
Super. Key events carry down/up in `value1`, the post-transition modifier mask
and `BORING_INPUT_FLAG_REPEAT` when a make arrives for an already-held key.

## Mouse normalization

The mouse decoder consumes synchronized three-byte PS/2 packets. X and Y are
sign-extended from packet flags. BoringOS defines positive `dy` as screen-down,
so native PS/2 Y is inverted during normalization. X/Y overflow suppresses the
movement sample while still allowing button transitions. A malformed first byte
is ignored until the mandatory synchronization bit is present.

Mouse buttons use project-owned values LEFT=1, MIDDLE=2 and RIGHT=3. Movement
and button transitions are separate normalized events.

## Event ABI

The shared kernel/userspace event is exactly 24 bytes:

```c
struct boring_input_event {
    uint32_t type;
    uint32_t code;
    int32_t value1;
    int32_t value2;
    uint32_t modifiers;
    uint32_t flags;
};
```

Types are key, relative mouse move and mouse button. Modifier bits are Shift,
Ctrl, Alt and Super. Size and field offsets are compile-time asserted.

## Queue and ownership

The kernel owns one fixed 128-event FIFO. There is no heap allocation in the
event path. When full, the newest incoming event is dropped; existing FIFO
order is preserved and a saturating `dropped_events` counter records the loss.

One process PID can own the stream. `INPUT_CLAIM` is idempotent for that PID and
returns busy for a second process. Claim and release clear the queue and held-key
modifier state. Explicit release is not required for correctness: `SYS_EXIT`
releases ownership before returning control to the parent, which allows an
immediate later claimant to take the device without rebooting.

## Blocking userspace read

Syscall numbers 22, 23 and 24 are `INPUT_CLAIM`, `INPUT_READ` and
`INPUT_RELEASE`. `INPUT_READ` accepts 1..16 events per call. The kernel validates
the entire writable userspace destination before any event can be removed from
the queue.

When an owner reads an empty queue, the current single-CPU implementation arms
an owner-wait flag while interrupts are disabled and executes `sti; hlt` on the
existing trusted syscall stack. An IRQ pending before or immediately after HLT
wakes the kernel; the read path disables interrupts again and rechecks the queue
before returning. This closes the empty-check/sleep race without introducing a
second scheduler model or userspace busy polling. User-memory copy is performed
with interrupts enabled and the syscall entry interrupt state is restored before
return.

## Userspace proof

`/bin/input-test` is a freestanding static ELF using only BoringOS runtime
wrappers. It verifies claim/error contracts, blocks in `INPUT_READ`, prints
normalized events and releases explicitly in normal mode. `--teardown` exits
after its first event without releasing so acceptance can prove automatic owner
cleanup and immediate reclaim.

`tests/input-host-test.c` covers keyboard make/break and extended recovery,
modifiers/repeat, mouse sign/button/overflow/resynchronization, FIFO order,
wrap-around, drop-newest overflow, ownership, wait state and cleanup.
`tests/input-qemu.sh` boots the real persistent-root system and uses QMP
`input-send-event` to inject keyboard/mouse input. It proves Super+Q,
Super+Enter, relative movement, left-button down/up, no pre-injection events,
blocking wakeup, owner teardown/reclaim, child exit/reap and no leaked zombie.

## Deliberate non-goals

Milestone 31 does not implement a cursor, graphical shell, userspace framebuffer
mapping, `boring-display`, compositor/window-system protocol, terminal graphics,
GUI clients, BoringWM, USB input, general HID or Milestone 32.
'''
Path("docs/input.md").write_text(input_doc)

after = grep_version()
print("M31 closeout remaining 0.0.31-dev matches (historical only):")
for line in after:
    print(line)
    if line.split(":", 1)[0] != "docs/roadmap.md":
        raise SystemExit(f"non-historical 0.0.31-dev remained after closeout: {line}")

# Ensure active current version is now represented in all intended classes.
for path in ("README.md", "README.de.md", "docs/roadmap.md", "kernel/core/entry.c", "kernel/core/syscall.c"):
    if NEW not in Path(path).read_text():
        raise SystemExit(f"missing active {NEW} contract in {path}")

print("M31 documentation/version closeout edits applied.")
