# Native input foundation

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
