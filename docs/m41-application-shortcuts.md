# M41 — BoringWM application shortcuts

Status: implementation in progress; no Semantic Freeze or closeout claimed.

Verified main base `81dc02ea4d42f39b5a8106406b3203e11acb1018`, tree `c2e3c6fb156633afd74127b3c3efca1cb5c38c58`, BoringKernel 0.0.41-dev; all five merged-main push workflows SUCCESS.

Scope: Super+Return launches real /bin/boring-terminal; Super+E launches real /bin/boring-edit; Super+F launches real /bin/boring-files; existing focused Super+Q close, tiling, focus and reorder semantics remain. Use existing SPAWN and display/WM protocols; no new binaries or ABI. Missing applications fail honestly, preserving historical terminal-unavailable regression. No M42 foundation extraction.

Acceptance: launch all three through their actual shortcuts in QEMU, prove independent Ring3 processes/surfaces/buffers, cycle focus, prove keyboard input reaches only the focused app, save the editor document, individually close each with Super+Q, and prove complete PID1-only drain. Capture and validate real framebuffer pixels. Preserve all historical regression. Only after exact-head Semantic Freeze may documentation/current-version closeout advance to 0.0.42-dev, followed by exact-head CI, guarded squash merge and main-push CI SUCCESS.
