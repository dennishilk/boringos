# M41 — BoringWM application shortcuts

Status: implementation in progress; no Semantic Freeze or closeout claimed.

Verified main base `81dc02ea4d42f39b5a8106406b3203e11acb1018`, tree `c2e3c6fb156633afd74127b3c3efca1cb5c38c58`, BoringKernel 0.0.41-dev; all five merged-main push workflows SUCCESS.

Scope: Super+Return launches real /bin/boring-terminal; Super+E launches real /bin/boring-edit; Super+F launches real /bin/boring-files; existing focused Super+Q close, tiling, focus and reorder semantics remain. Use existing SPAWN and display/WM protocols; no new binaries or ABI. Missing applications fail honestly, preserving historical terminal-unavailable regression. No M42 foundation extraction.

Acceptance: launch all three through their actual shortcuts in QEMU, prove independent Ring3 processes/surfaces/buffers, cycle focus, prove keyboard input reaches only the focused app, save the editor document, individually close each with Super+Q, and prove complete PID1-only drain. Capture and validate real framebuffer pixels. Preserve all historical regression. Only after exact-head Semantic Freeze may documentation/current-version closeout advance to 0.0.42-dev, followed by exact-head CI, guarded squash merge and main-push CI SUCCESS.

The mapping is a small internal WM C helper, not a wire or syscall ABI change.
Only exact Super+Return/E/F key-down events launch; repeats, key-up events and
extra modifiers are ignored. Programs are loaded through real VFS SPAWN with
existing detached-child cleanup. A missing executable reports unavailable;
no placeholder window is created. Super+Q still requests a graceful close,
including BoringEdit's existing unsaved-document guard.

`python3 tests/m41-shortcuts-qemu.py` reuses the M40 writable desktop builder
with the real current WM. It launches all three apps via shortcuts, verifies
WM parent/child identities and independent surfaces, cycles focus, checks
terminal/editor text and Files navigation against real framebuffer pixels,
saves `/untitled.txt`, closes each app via Super+Q, and checks exact disk bytes
and complete PID1-only drain. M39/M40 retain their independent cat acceptance;
M41 does not substitute a fake cat or synthetic framebuffer.

Local implementation acceptance passed: all three actual shortcuts launched
independent managed Ring3 clients, only the focused terminal/editor received
its text, Files received navigation, the saved document contains exactly
`edit`, and all three closed individually through Super+Q. Final process/task
accounting was 7 created, 6 finished, PID1 alone; all desktop counters zero.
The full-frame bitmap oracle and additive WM host checks passed. Exact-head
CI and Semantic Freeze remain pending.
