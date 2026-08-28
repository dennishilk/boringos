# M42 — native desktop client foundation

Status: scope only; implementation and Semantic Freeze pending.

Verified base main `df412114c38af9d6d87f9d3992ff1c72a2a8836d`, tree `8dd2fe7698be6249e5a530431fb85b8d5ec7b7ec`, BoringKernel 0.0.42-dev; all six exact-main push workflows SUCCESS.

Extract the demonstrably duplicated userspace display/WM client plumbing from Terminal, Edit and Files into a small BoringOS-owned C helper. Preserve each application's policy, text model, file I/O, input semantics, close behavior and current protocols. No widget framework, new ABI, kernel changes or hardware inventory.

Acceptance: focused helper/error-path host tests, all three real apps using the helper, actual three-window QEMU framebuffer/input/save/close/drain proof, historical desktop tests and complete permanent Boot regression. No version change until proven exact-head Semantic Freeze; then runtime-neutral closeout to 0.0.43-dev, exact-head CI, guarded squash and main push CI SUCCESS.
