#!/usr/bin/env python3
"""Focused M61 contracts for booting to a real empty BoringWM desktop."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WM = (ROOT / "user/boringwm/main.c").read_text()
WM_CORE = (ROOT / "user/boringwm/core.c").read_text()
DISPLAY = (ROOT / "user/boring-display/server.c").read_text()
BREADCRUMBS = (ROOT / "kernel/core/m61_physical_breadcrumbs.c").read_text()
BOOT_HEADER = (ROOT / "kernel/include/boring/boot_console.h").read_text()
BOOT_CONSOLE = (ROOT / "kernel/core/boot_console.c").read_text()
BUILD = (ROOT / "tests/m61-build.sh").read_text()
POST = (ROOT / "tests/m61-physical-trace-kernel.sh").read_text()
GRAPHICS = (ROOT / "kernel/core/graphics.c").read_text()
MAKEFILE = (ROOT / "Makefile").read_text()
M38_BUILD = (ROOT / "tests/m38-build.sh").read_text()
M37_BUNDLE = (ROOT / "tests/m37-bundle-test.sh").read_text()
M54_QEMU = (ROOT / "tests/m54-usb-only-desktop-qemu.py").read_text()


def fail(message):
    raise RuntimeError(message)


def function_body(source, signature):
    start = source.rfind(signature)
    if start < 0:
        fail(f"missing function: {signature}")
    opening = source.find("{", start)
    if opening < 0:
        fail(f"missing function body: {signature}")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening:index + 1]
    fail(f"unterminated function: {signature}")


for token in (
    "BORING_M61_PHYSICAL_DESKTOP_WITNESS",
    "M61 PHYSICAL: automatic terminal spawn requested",
    "M61 PHYSICAL: automatic terminal spawn FAILED",
    "M61 PHYSICAL: automatic terminal spawn pid=",
):
    if token in WM:
        fail(f"obsolete automatic-terminal token remains in BoringWM: {token}")
if "static long launch_application(uint32_t key)" not in WM:
    fail("manual launcher implementation was removed")
for token in (
    'if (key == BORING_KEY_ENTER) { return "/bin/boring-terminal"; }',
    'if (key == BORING_KEY_E) { return "/bin/boring-edit"; }',
    'if (key == BORING_KEY_F) { return "/bin/boring-files"; }',
):
    if token not in WM_CORE:
        fail(f"manual launcher mapping changed: {token}")
if 'wm: Super+Return spawned /bin/boring-terminal' not in WM:
    fail("Super+Return terminal success witness was removed")
if 'wm: Super+E spawned /bin/boring-edit' not in WM:
    fail("Super+E editor success witness was removed")
if 'wm: Super+F spawned /bin/boring-files' not in WM:
    fail("Super+F files success witness was removed")

drain_gate = "BORING_BOUNDED_DESKTOP_ACCEPTANCE"
wm_drain = 'if (ever_managed && (wm.count == 0U)) { desktop_say("wm: session empty; clean exit\\n"); boring_exit(0); }'
wm_gate = WM.find(f"#ifdef {drain_gate}", WM.find("int boring_main(void)"))
wm_exit = WM.find(wm_drain, wm_gate)
wm_end = WM.find("#endif", wm_exit)
if not (0 <= wm_gate < wm_exit < wm_end):
    fail("zero-client WM exit is not isolated behind the bounded acceptance gate")
display_drain = 'if (manager_seen && (managed.manager_endpoint == 0U) && (live == 0U)) {'
display_main = DISPLAY.find("int boring_main(void)")
display_gate = DISPLAY.find(f"#ifdef {drain_gate}", display_main)
display_exit = DISPLAY.find(display_drain, display_gate)
display_end = DISPLAY.find("#endif", display_exit)
if not (0 <= display_main < display_gate < display_exit < display_end):
    fail("empty display drain is not isolated behind the bounded acceptance gate")
if f"RUNTIME_USER_CPPFLAGS += -D{drain_gate}=1" not in MAKEFILE:
    fail("historical desktop builds lost their bounded-session acceptance gate")
for token in ("RUNTIME_USER_CPPFLAGS_STAMP", "check-runtime-user-cppflags"):
    if token not in MAKEFILE:
        fail(f"desktop acceptance flag rebuild contract missing: {token}")
if M38_BUILD.count(f"-D{drain_gate}=1") != 2:
    fail("M38 death variants do not preserve the bounded-session drain gate")
if "TEST_MODE=m36-desktop" not in M37_BUNDLE:
    fail("M37 BoringFS bundle lost its bounded-session acceptance build")
if f"-D{drain_gate}=1" not in M37_BUNDLE:
    fail("M37 BoringFS bundle lost its explicit bounded-session gate")
for token in ("M37_BUNDLE_EXTRA_USER_CPPFLAGS", "-DBORING_M54_USB_ONLY_DESKTOP=1"):
    if token not in M54_QEMU:
        fail(f"M54 BoringFS bundle lost its historical runtime flag: {token}")
if drain_gate in BUILD:
    fail("M61 runtime must not enable the historical bounded-session gate")

for token in (
    "user-boring-files",
    "tests/boringfs-m40-bundle.c",
    "build/user/boring-files.elf",
    "build/boringfsck --cat /bin/boring-files",
    "BORING_FILES_INCLUDED_IN_M61_ROOT=YES",
):
    if token not in BUILD:
        fail(f"M61 boring-files packaging contract missing: {token}")

wm_main = function_body(WM, "int boring_main(void)")
manager = wm_main.find("hello.version = BORING_DISPLAY_CONTROL_VERSION; hello.type = DISPLAY_MANAGER;")
init = wm_main.find("!wm_init(&wm, info.width, info.height)")
empty_present = wm_main.find("sync_layout();", init)
event_loop = wm_main.find("for (;;)", init)
if not (0 <= manager < init < empty_present < event_loop):
    fail("initial empty sync_layout is not immediately after successful WM readiness")
sync = function_body(WM, "static void sync_layout(void)")
for token in (
    "present.version = BORING_DISPLAY_CONTROL_VERSION; present.type = DISPLAY_PRESENT;",
    "present.background = BORING_WM_BACKGROUND;",
    "display_rpc(&present).status != BORING_DISPLAY_STATUS_OK",
):
    if token not in sync:
        fail(f"empty WM present is not the real existing display RPC path: {token}")

for token in ("BORING_BOOT_STAGE_AUTOMATIC_TERMINAL", "automatic terminal"):
    if token in BOOT_HEADER or token in BOOT_CONSOLE:
        fail(f"boot console automatic-terminal row remains: {token}")
if BOOT_CONSOLE.count('"desktop present"') != 1:
    fail("boot console desktop-present row is missing or duplicated")

for token in ("terminal_ready", "stage_number = 27U", "case 27U:",
              "BORING_BOOT_STAGE_AUTOMATIC_TERMINAL"):
    if token in BREADCRUMBS:
        fail(f"terminal still controls M61 desktop readiness: {token}")
present = function_body(
    BREADCRUMBS,
    "enum boring_framebuffer_user_result __wrap_boring_framebuffer_user_present(")
if "const bool final_present = wm_ready;" not in present:
    fail("desktop present is not gated only by BoringWM readiness")
real_call = present.find("result = __real_boring_framebuffer_user_present(process, handle);")
failure_gate = present.find("if (result != BORING_FRAMEBUFFER_USER_OK)")
final_gate = present.find("if (final_present)", failure_gate)
handoff = present.find("boring_boot_console_desktop_handoff();")
if not (0 <= real_call < failure_gate < final_gate < handoff):
    fail("M61 desktop handoff can occur before a successful real framebuffer present")

post_present = function_body(
    POST,
    "enum boring_framebuffer_user_result m61_post_boring_framebuffer_user_present(")
if "terminal_posted" in post_present:
    fail("POST 6F still requires a terminal")
if "(result == BORING_FRAMEBUFFER_USER_OK) && wm_posted &&" not in post_present:
    fail("POST 6F is not gated by successful real present after BoringWM start")
if "M61_POST(M61_POST_DESKTOP_PRESENT);" not in post_present:
    fail("POST 6F emission is missing")

for path in (
    ROOT / "kernel/core/m61_wm_terminal_post.c",
    ROOT / "tests/m61-wm-terminal-post-verifier.py",
):
    if path.exists():
        fail(f"obsolete WM-to-terminal bisector remains: {path.relative_to(ROOT)}")
for token in (
    "m61_wm_terminal_post.c",
    "m61-wm-terminal-post-verifier.py",
    "--wrap=boring_ipc_service_connect",
    "--wrap=boring_ipc_poll",
    "--wrap=boring_ipc_send",
    "--wrap=boring_ipc_receive",
    "--wrap=x86_64_syscall_dispatch_m36",
    "BORING_M61_PHYSICAL_DESKTOP_WITNESS",
):
    if token in BUILD:
        fail(f"obsolete M61 auto-terminal build wiring remains: {token}")

pre = GRAPHICS.find("(uint8_t)M61_NORMAL_FRAMEBUFFER_PRE_POST);")
store = GRAPHICS.find("surface->address[offset + (uint64_t)byte_index] =")
post = GRAPHICS.find("(uint8_t)M61_NORMAL_FRAMEBUFFER_POST_POST);")
if not (0 <= pre < store < post):
    fail("frozen POST 90/store/91 ordering changed")

print("AUTO_TERMINAL_REMOVED=YES")
print("MANUAL_SUPER_RETURN_TERMINAL_PRESERVED=YES")
print("EMPTY_WM_SYNC_LAYOUT_PRESENT=YES")
print("BOOT_CONSOLE_AUTOMATIC_TERMINAL_ROW_REMOVED=YES")
print("DESKTOP_PRESENT_NO_LONGER_REQUIRES_TERMINAL=YES")
print("POST_6F_STILL_REQUIRES_REAL_PRESENT_SUCCESS=YES")
print("POST_90_91_UNCHANGED=YES")
print("OBSOLETE_WM_TERMINAL_BISECTOR_REMOVED=YES")
print("M61_EMPTY_DESKTOP_PERSISTS_AFTER_LAST_WINDOW=YES")
print("M61_WM_DOES_NOT_EXIT_ON_ZERO_CLIENTS=YES")
print("M61_DISPLAY_DOES_NOT_DRAIN_ON_NORMAL_EMPTY_DESKTOP=YES")
print("HISTORICAL_M38_DRAIN_ACCEPTANCE_PRESERVED=YES")
print("MANUAL_TERMINAL_RELAUNCH_AFTER_EMPTY=YES")
print("MANUAL_EDIT_RELAUNCH_AFTER_EMPTY=YES")
print("BORING_FILES_INCLUDED_IN_M61_ROOT=YES")
print("SUPER_F_LAUNCHER_TARGET_PRESENT=YES")
