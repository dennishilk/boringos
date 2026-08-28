# M39 — native BoringEdit

Status: implementation COMPLETE and Semantic Frozen; runtime-neutral closeout to 0.0.40-dev. Exact-head closeout CI and guarded merge remain required.

Base: 456e2c9c5f4b142f17217f58b49101ba367b4f5a (tree d85a932a393ec24e29759182efe06bf0bdb1625a), BoringKernel 0.0.39-dev. Main M38, M37 and complete Boot workflows verified SUCCESS before starting.

Scope: /bin/boring-edit is a real Ring3 boring.display client managed by BoringWM, launched as `boring-edit [path]` through the existing shell SPAWN path. Bounded 4096-byte plain ASCII document, text/cursor rendering, letters/space/newline/backspace, Ctrl+S save through existing VFS/BoringFS operations, graceful WM close. No path starts empty with an explicit /untitled.txt save destination. No new syscall ABI, syntax highlighting, tabs, launcher shortcuts or M40 work.

Acceptance must use real QEMU keyboard events and writable BoringFS: open/type/save/close, independently execute /bin/cat and compare exact persisted disk bytes, inspect actual framebuffer glyphs/cursor, prove lifecycle drain to PID1, and run all inherited regression gates. Oversize/unsupported files must not be silently truncated; failed saves retain dirty state. Save is bounded, not crash-atomic, matching the current filesystem API.

Freeze and closeout require separate exact-head green CI. Version advances only during runtime-neutral closeout; guarded squash merge follows proven closeout, then main push CI SUCCESS.

## Use

Run `sh scripts/run-boring-edit.sh` (builds a fresh writable image; rerunning resets that image). Super+Return starts the existing terminal. Enter `boring-edit /note.txt`; a missing file starts a new document. `boring-edit` starts empty, with `/untitled.txt` visibly named as its save destination. Ctrl+S writes the whole document; Left/Right move the cursor. Super+Q closes a clean document; for a dirty document it waits for Ctrl+S (save and close) or Ctrl+Q (explicit discard). The existing WM sends CLOSE once, so successful save completes a pending close request.

The editor shares only the established bitmap renderer and key translation code with the terminal; its document bytes are not an escape-sequence parser or PTY. Rendering writes cells directly, preserving bounded plain-text semantics. ASCII printable bytes and LF are supported. Other bytes or files larger than 4096 bytes are rejected before any write. Save uses the existing FS_TOUCH and FS_WRITE syscall contracts; FD_OPEN/READ/CLOSE load the real file. No ABI changes. A failed write can leave disk contents changed because the current truncate/write operation is not atomic; the in-memory document stays dirty and available for retry.

`python3 tests/m39-edit-qemu.py` builds the actual desktop from BoringFS binaries, generates QMP keyboard events, verifies every framebuffer pixel with the existing independent font/layout oracle at the actual renderer bounds, then validates the modified disk after QEMU exits. The M38 test harness uses its existing writable mount API only when `BORING_M39_EDIT_ACCEPTANCE` is explicitly compiled; historical read-only M38 scenarios are unchanged. The image has the measured executable minimum plus 32 document blocks, not an invented minimal writable geometry.

## Implementation candidate acceptance

Local real-QEMU acceptance passed: three-line loaded/edited document, full-pixel editor and independent cat screenshots, shorter rewrite with exact persisted bytes `seed\n hello\nos`, empty `/untitled.txt`, missing-parent save failure retaining `keep`, explicit discard, and inherited M38 IPC/input/framebuffer/shared-buffer/PTY/process/task drain to PID1 only. The bounded editor and inherited terminal host tests passed. ASan/UBSan passed with LeakSanitizer disabled because this runtime cannot inspect `/proc` task metadata. All exact-head implementation gates completed SUCCESS; the Semantic Freeze is recorded below.


## Semantic Freeze

Implementation: `332d4a9f235fb219cd0e2cc3798217044c4531aa`.
Tree: `0e80ba24b0ac32c71f1fc678097bef672e537e0b`.
Exact-head SUCCESS: M39 #1 / 33186371956; M37 #45 / 33186371793;
M38 #12 / 33186371785; complete Boot #511 / 33186371797.
The separate closeout changes only documentation and active version witnesses.
