#!/usr/bin/env python3
"""Static contracts for release-clean M68 boot visuals."""
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ENTRY = (ROOT / "kernel/core/entry.c").read_text()
PERSISTENT = (ROOT / "kernel/core/boringfs_ro_test.c").read_text()
BREADCRUMBS = (ROOT / "kernel/core/m61_physical_breadcrumbs.c").read_text()
FRAMEBUFFER_QEMU = (ROOT / "tests/framebuffer-qemu.sh").read_text()
M61_BUILD = (ROOT / "tests/m61-build.sh").read_text()
M68_BUILD = (ROOT / "tests/m68-build-physical-candidate.sh").read_text()


def require_order(source, fragments, label):
    positions = [source.find(fragment) for fragment in fragments]
    if any(position < 0 for position in positions) or positions != sorted(positions):
        raise RuntimeError(f"{label} is missing or reordered: {positions!r}")


normal = ENTRY[ENTRY.index("#if BORING_TEST_MODE == BORING_TEST_MODE_NORMAL"):]
require_order(
    normal,
    (
        "#if defined(BORING_BOOT_DASHBOARD_DIAGNOSTIC)",
        "boring_boot_dashboard_render(framebuffer_surface, &dashboard_info)",
        "#endif\n    run_cooperative_task_test();",
    ),
    "normal dashboard diagnostic gate",
)
require_order(
    PERSISTENT,
    (
        "#if (BORING_TEST_MODE == 15) && \\\n"
        "    defined(BORING_BOOT_DASHBOARD_DIAGNOSTIC)",
        "boring_boot_dashboard_render(surface, &dashboard_info)",
        "#if BORING_TEST_MODE == 15\n    persistent_input_init();",
    ),
    "persistent-root dashboard diagnostic gate",
)
if "TEST_CPPFLAGS=-DBORING_BOOT_DASHBOARD_DIAGNOSTIC=1" not in FRAMEBUFFER_QEMU:
    raise RuntimeError("dashboard screenshot acceptance lost its explicit diagnostic gate")

release_gate = BREADCRUMBS.index("#if !defined(BORING_M68_PHYSICAL_RELEASE)")
activation = BREADCRUMBS.index("boring_boot_console_activate(fb)", release_gate)
refresh = BREADCRUMBS.index("boring_boot_console_refresh()", activation)
gate_end = BREADCRUMBS.index("#endif", refresh)
if not release_gate < activation < refresh < gate_end:
    raise RuntimeError("M68 release can still activate graphical boot-console success UI")
for token in (
    "boring_m68_physical_release_ui_enabled",
    "M68 graphical success diagnostics disabled; serial and POST preserved",
    "serial_stage(stage_number, mark, label);",
    "M61_HANDOFF_POST(M61_HANDOFF_POST_FINAL_PRESENT_OK);",
    "boring_boot_console_desktop_handoff();",
):
    if token not in BREADCRUMBS:
        raise RuntimeError(f"M68 release diagnostic preservation missing: {token}")

if "M61_EXTRA_TEST_CPPFLAGS=${M61_EXTRA_TEST_CPPFLAGS:-}" not in M61_BUILD:
    raise RuntimeError("M61 runtime cannot receive the isolated M68 release define")
require_order(
    M68_BUILD,
    (
        "M61_EXTRA_TEST_CPPFLAGS='-DBORING_M68_PHYSICAL_RELEASE=1'",
        "sh tests/m61-build.sh",
        "boring_m68_physical_release_ui_enabled",
        "__wrap_boring_boot_console_desktop_handoff",
        "sh tests/m61-build-usb-image.sh",
    ),
    "M68 release rebuild before image construction",
)
for proof in (
    "successful_boot_dashboard_visible=NO",
    "graphical_boot_console_diagnostic=disabled",
    "magenta_scanout_witness_wrapper=absent",
):
    if proof not in M68_BUILD:
        raise RuntimeError(f"M68 candidate proof missing: {proof}")

print("M68_SUCCESSFUL_BOOT_DASHBOARD_VISIBLE=NO")
print("M68_GRAPHICAL_BOOT_CONSOLE_DIAGNOSTIC=DISABLED")
print("M68_MAGENTA_SCANOUT_WITNESS_WRAPPER=ABSENT")
print("M68_SERIAL_POST_FAILURE_DIAGNOSTICS=PRESERVED")
