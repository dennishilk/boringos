# M40 — native BoringFiles

Status: implementation in progress; no Semantic Freeze or closeout claimed.

Verified base: main `913a14c6695ef335b75bbe422ad722f6991d8d94`, tree `e3d758b034ac112807e69b63807c27877513a86f`, BoringKernel 0.0.40-dev. M39/M38/M37/full Boot main-push CI SUCCESS before beginning.

Scope: real Ring3 `/bin/boring-files`, existing boring.display/BoringWM protocols, bounded VFS readdir, visible directory/file types and current directory, keyboard selection, child/parent navigation, refresh, and launching `/bin/boring-edit <path>` through existing SPAWN. No ABI extension, thumbnails, drag/drop, mount manager, or M41 shortcuts.

Acceptance: create real directories/files in writable BoringFS; Files shows and navigates them, opens an actual document in BoringEdit, then independent cat and persisted disk bytes prove the edit. Actual framebuffer must show terminal, files and editor simultaneously with independent surfaces and focus. Graceful close must drain back to PID1. Existing M39 and all prior regression gates remain required. Implementation stays 0.0.40-dev; runtime-neutral closeout to 0.0.41-dev only after exact-head Semantic Freeze.
