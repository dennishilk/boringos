# M42 — native desktop client foundation

Status: implementation COMPLETE and Semantic Frozen; runtime-neutral closeout to 0.0.43-dev.

Verified base main `df412114c38af9d6d87f9d3992ff1c72a2a8836d`, tree `8dd2fe7698be6249e5a530431fb85b8d5ec7b7ec`, BoringKernel 0.0.42-dev; all six exact-main push workflows SUCCESS.

Extract the demonstrably duplicated userspace display/WM client plumbing from Terminal, Edit and Files into a small BoringOS-owned C helper. Preserve each application's policy, text model, file I/O, input semantics, close behavior and current protocols. No widget framework, new ABI, kernel changes or hardware inventory.

Acceptance: focused helper/error-path host tests, all three real apps using the helper, actual three-window QEMU framebuffer/input/save/close/drain proof, historical desktop tests and complete permanent Boot regression. No version change until proven exact-head Semantic Freeze; then runtime-neutral closeout to 0.0.43-dev, exact-head CI, guarded squash and main push CI SUCCESS.

## Implemented boundary

`user/runtime/client.c` and `<boring/client.h>` hold process-owned endpoints,
surface/window tokens, mapped buffer and validated physical surface geometry.
All three applications link the same object. It implements connection/info,
buffer allocation/mapping, surface create/delegate/register, commit, checked
WM event envelopes, unregister and release. Existing WM asynchronous configure
and unregister key/close handling is preserved. No protocol or syscall changes.

Each application retains EVENT_WAIT and dispatch policy, its own bounded text
model, shared existing bitmap renderer, resize handling and input semantics.
Terminal owns PTY close/spawn; Edit owns dirty prompts and file I/O; Files owns
readdir/navigation/SPAWN and its bounded scratch allocation. Those app policy
functions are byte-identical to the verified base. The already-shared renderer
is reused, not copied into a new widget framework.

The helper requires one zero-initialized process-owned state and non-null
arguments. Errors return false plus a static diagnostic; callers retain their
existing fatal-exit policy and kernel process-resource reclamation. Failed calls
are not a retry API. Successful unregister/release are idempotent; release
rejects a still-registered window. Drawing occurs before initial publication.
Geometry is bounded before buffer size arithmetic. No title/UX changes.

## Acceptance

- All three freestanding ELF binaries link all six `boring_client_*` entrypoints.
- Existing app models and bitmap renderer host tests pass.
- New transport fixture covers 21 injected call failures, malformed lengths,
  attachments, versions, geometry, surface/window identifiers and statuses,
  asynchronous WM ordering, release-before-unregister rejection and idempotence.
- Host test also passes ASan/UBSan locally (LeakSanitizer disabled because the
  worker cannot perform its process inspection; no clean LSan claim).
- `tests/m42-client-qemu.py` reuses the existing M41 real QEMU scenario and full
  framebuffer oracle without duplicating or weakening them. Evidence has a
  separate `build/m42-client-reference` directory. Historical scenario log labels
  retain M41; the final M42 witness additionally verifies shared-helper linkage.
- Actual guest: three shortcuts, independent app processes/surfaces/buffers,
  focused Terminal/Edit/Files input, exact persisted `/untitled.txt` bytes `edit`,
  individual Super+Q closes, seven created/six finished processes and PID1 only.
- Permanent M39/M40 regressions continue to verify real BoringFS navigation,
  editor reopen/save/failure retention and independent guest cat reads.

No new executable, no kernel or ABI change, no hardware support claim. Implementation remained at 0.0.42-dev; the proven freeze now permits the separate
runtime-neutral closeout to 0.0.43-dev.


## Semantic Freeze

Implementation: `7c9054e8c0c339c1c8a168c85bc0d6998b4708c8`.
Tree: `04ff69250b70850f698bb5cbde4aafac38e8f383`.
All exact-head gates SUCCESS: M42 #1 / 33192303081; M41 #5 / 33192303109;
M40 #8 / 33192303056; M39 #11 / 33192302977; M38 #22 / 33192302988;
M37 #55 / 33192302974; complete Boot #521 / 33192303044.
This separate closeout changes only documentation and active version witnesses.
Exact-head closeout CI and guarded merge remain required.
