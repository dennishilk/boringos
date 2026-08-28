# M39 — native BoringEdit

Status: implementation in progress; no Semantic Freeze or closeout claimed.

Base: 456e2c9c5f4b142f17217f58b49101ba367b4f5a (tree d85a932a393ec24e29759182efe06bf0bdb1625a), BoringKernel 0.0.39-dev. Main M38, M37 and complete Boot workflows verified SUCCESS before starting.

Scope: /bin/boring-edit is a real Ring3 boring.display client managed by BoringWM, launched as `boring-edit [path]` through the existing shell SPAWN path. Bounded 4096-byte plain ASCII document, text/cursor rendering, letters/space/newline/backspace, Ctrl+S save through existing VFS/BoringFS operations, graceful WM close. No path starts empty with an explicit /untitled.txt save destination. No new syscall ABI, syntax highlighting, tabs, launcher shortcuts or M40 work.

Acceptance must use real QEMU keyboard events and writable BoringFS: open/type/save/close, independently execute /bin/cat and compare exact persisted disk bytes, inspect actual framebuffer glyphs/cursor, prove lifecycle drain to PID1, and run all inherited regression gates. Oversize/unsupported files must not be silently truncated; failed saves retain dirty state. Save is bounded, not crash-atomic, matching the current filesystem API.

Freeze and closeout require separate exact-head green CI. Version advances only during runtime-neutral closeout; guarded squash merge follows proven closeout, then main push CI SUCCESS.
