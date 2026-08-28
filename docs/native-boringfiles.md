# M40 — native BoringFiles

Status: implementation in progress; no Semantic Freeze or closeout claimed.

Verified base: main `913a14c6695ef335b75bbe422ad722f6991d8d94`, tree `e3d758b034ac112807e69b63807c27877513a86f`, BoringKernel 0.0.40-dev. M39/M38/M37/full Boot main-push CI SUCCESS before beginning.

Scope: real Ring3 `/bin/boring-files`, existing boring.display/BoringWM protocols, bounded VFS readdir, visible directory/file types and current directory, keyboard selection, child/parent navigation, refresh, and launching `/bin/boring-edit <path>` through existing SPAWN. No ABI extension, thumbnails, drag/drop, mount manager, or M41 shortcuts.

Acceptance: create real directories/files in writable BoringFS; Files shows and navigates them, opens an actual document in BoringEdit, then independent cat and persisted disk bytes prove the edit. Actual framebuffer must show terminal, files and editor simultaneously with independent surfaces and focus. Graceful close must drain back to PID1. Existing M39 and all prior regression gates remain required. Implementation stays 0.0.40-dev; runtime-neutral closeout to 0.0.41-dev only after exact-head Semantic Freeze.

## Operation and bounds

`boring-files [directory]` starts in that directory or the inherited CWD.
Up/Down select, Enter enters a directory or launches `/bin/boring-edit` with
that exact file path, Backspace navigates to the parent, and R refreshes.
Super+Q closes the browser. Editor children use the existing detached SPAWN
contract so Files continues receiving WM events; ordinary file contents are
not inspected or executed by the browser itself. A selected binary is passed
to BoringEdit, which rejects unsupported or oversized text inputs.

Each readdir refresh reads at most 65 entries: 64 visible bounded entries and
one extra probe to label truncation honestly. Names and types use the
existing bounded dirent ABI. The current canonical CWD comes from GETCWD.
A failed refresh restores the previous directory and retains its view.
The text grid scrolls the selection into view. No new syscall or display ABI.

`sh scripts/run-boring-files.sh` builds a fresh writable image and launches
QEMU. Re-running rebuilds the image, so copy it first to retain interactive
changes. The permanent M40 workflow runs host bounds tests and actual QEMU
keyboard/navigation, full-frame pixel, three-client identity/focus, editor,
independent cat, persisted-image byte and PID1-only drain checks. M39 and all
historical regression workflows are unchanged.

The transactional directory scratch list uses the existing private userspace memory allocator and is explicitly freed on close. This keeps the static ELF image inside the unchanged 16-page loader budget; the ELF audit checks the real load-segment page total against the kernel header.

The existing bitmap font displays printable ASCII; other name bytes are shown as `?`, but full bounded names remain intact for path resolution. No file-size metadata is invented or displayed.

## Implementation candidate acceptance

Local real QEMU acceptance passed cleanly: actual guest-created `/work/nested`
and `/work/note.txt`, child and parent navigation, file selection, detached
SPAWN from Files to BoringEdit, three independent managed Ring3 clients,
focus-only editing, save, reopen through Files, independent Ring3 cat, exact
persisted bytes `hello world`, and final 9-created/8-finished PID1-only drain.
All IPC/shared-buffer/input/framebuffer/PTY counters are zero. Full-frame pixel
checks passed at each captured state. Host model/ELF tests and ASan/UBSan passed
(with unsupported LeakSanitizer disabled). Exact-head CI and Semantic Freeze
remain pending.
