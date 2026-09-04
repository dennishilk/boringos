#!/usr/bin/env python3
"""Static M61 safety contracts for the native boot console."""
import hashlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BREADCRUMBS = ROOT / "kernel/core/m61_physical_breadcrumbs.c"
CONSOLE = ROOT / "kernel/core/boot_console.c"
IO_HEADER = ROOT / "kernel/include/boring/io.h"
POST_SCRIPT = ROOT / "tests/m61-physical-trace-kernel.sh"
BASE_IO_SHA256 = "cd22d43b16a5fa39322ad5dd90d78000c79dc7c56bb0a9d3a3b6d3137d922c1e"


def function_body(source, signature):
    start = source.rfind(signature)
    if start < 0:
        raise RuntimeError(f"missing function: {signature}")
    opening = source.find("{", start)
    if opening < 0:
        raise RuntimeError(f"missing function body: {signature}")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening:index + 1]
    raise RuntimeError(f"unterminated function body: {signature}")


def require_order(source, fragments, label):
    positions = [source.find(fragment) for fragment in fragments]
    if any(position < 0 for position in positions):
        raise RuntimeError(f"{label} is incomplete: {positions!r}")
    if positions != sorted(positions):
        raise RuntimeError(f"{label} was reordered: {positions!r}")


def main():
    breadcrumbs = BREADCRUMBS.read_text()
    console = CONSOLE.read_text()
    post_script = POST_SCRIPT.read_text()

    acquire = function_body(breadcrumbs, "static void acquire_framebuffers(void)")
    for forbidden in (
            "boring_graphics_", "boring_pixel_font_", "volatile",
            "boring_boot_console_activate", "boring_boot_console_refresh"):
        if forbidden in acquire:
            raise RuntimeError(
                f"acquire_framebuffers contains a pixel-write path: {forbidden}")

    render = function_body(console, "static bool boot_render(void)")
    write_calls = (
        "boring_graphics_clear", "boring_graphics_fill_rect",
        "boring_graphics_stroke_rect", "boring_graphics_horizontal_line",
        "boring_pixel_font_draw_text", "boring_pixel_font_draw_text_scaled",
    )
    for call in write_calls:
        if console.count(call) != render.count(call):
            raise RuntimeError(f"framebuffer write escaped boot_render: {call}")
    if "if (!boot_framebuffer_active ||" not in render:
        raise RuntimeError("boot_render lost its activation guard")

    present = function_body(
        breadcrumbs,
        "enum boring_framebuffer_user_result __wrap_boring_framebuffer_user_present(")
    require_order(
        present,
        (
            "result = __real_boring_framebuffer_user_present(process, handle);",
            "if (result != BORING_FRAMEBUFFER_USER_OK)",
            "boring_boot_console_activate(fb)",
        ),
        "safe normal-present activation")
    require_order(
        present,
        (
            "if (final_present)",
            "desktop_presented = true;",
            "boring_boot_console_desktop_handoff();",
        ),
        "final desktop handoff")

    activation_calls = breadcrumbs.count("boring_boot_console_activate(")
    other_activation_calls = 0
    for path in (ROOT / "kernel").rglob("*.c"):
        if path in (BREADCRUMBS, CONSOLE) or path.name.startswith("."):
            continue
        other_activation_calls += path.read_text().count(
            "boring_boot_console_activate(")
    if activation_calls != 1 or other_activation_calls != 0:
        raise RuntimeError(
            "boot-console activation call count changed: "
            f"M61={activation_calls} other={other_activation_calls}")

    if any(token in console for token in (
            "malloc(", "calloc(", "realloc(", "free(", "heap_")):
        raise RuntimeError("boot-console early history gained a heap dependency")
    if ("boot_history[BORING_BOOT_CONSOLE_HISTORY_CAPACITY]" not in console or
            "reason[BORING_BOOT_CONSOLE_REASON_CAPACITY]" not in console):
        raise RuntimeError("boot-console history/reason storage is not static and bounded")

    io_digest = hashlib.sha256(IO_HEADER.read_bytes()).hexdigest()
    if io_digest != BASE_IO_SHA256:
        raise RuntimeError(
            f"generic x86_64 I/O primitive changed: {io_digest}")
    combined = breadcrumbs + console + post_script
    if "__wrap_x86_64_out8" in combined or "--wrap=x86_64_out8" in combined:
        raise RuntimeError("generic x86_64_out8 hook introduced")
    framebuffer_fault = function_body(
        breadcrumbs,
        "static void framebuffer_fault_halt(")
    handoff_gap_posts = (
        "M61_HANDOFF_POST(M61_HANDOFF_POST_OUTER_PRESENT_RESUMED);",
        "M61_HANDOFF_POST(M61_HANDOFF_POST_SERIAL_ENTER);",
        "M61_HANDOFF_POST(M61_HANDOFF_POST_SERIAL_RETURNED);",
        "M61_HANDOFF_POST(M61_HANDOFF_POST_FINAL_PRESENT_ENTER);",
        "M61_HANDOFF_POST(M61_HANDOFF_POST_FINAL_PRESENT_OK);",
        "M61_HANDOFF_POST(M61_HANDOFF_POST_FINAL_PRESENT_ERROR);",
    )
    post37_witness = function_body(
        breadcrumbs,
        "void boring_m61_post37_witness(")
    runtime_hid_post = function_body(
        breadcrumbs,
        "void boring_m61_runtime_hid_post(")
    runtime_xhci_observe = function_body(
        breadcrumbs,
        "void boring_m61_runtime_xhci_observe(")
    if (console.count("x86_64_out8") != 0 or
            breadcrumbs.count("x86_64_out8") != 5 or
            framebuffer_fault.count("x86_64_out8") != 1 or
            post37_witness.count("x86_64_out8") != 1 or
            runtime_hid_post.count("x86_64_out8") != 1 or
            runtime_xhci_observe.count("x86_64_out8") != 1):
        raise RuntimeError(
            "M61 direct port-0x80 diagnostics escaped their bounded seams")
    for required in (
            "!m61_runtime_hid_armed",
            "m61_runtime_hid_highest >=",
            "(m61_post37_observed & bit) != 0U",
            "m61_post37_observed = (uint16_t)(m61_post37_observed | bit);",
    ):
        if required not in post37_witness:
            raise RuntimeError(
                f"M61 POST37 control-flow diagnostic lost bounded guard: {required}")
    if (present.count("M61_HANDOFF_POST(") != 6 or
            any(present.count(marker) != 1 for marker in handoff_gap_posts) or
            breadcrumbs.count("M61_HANDOFF_POST(") != 7):
        raise RuntimeError(
            "M61 handoff-gap POST diagnostics escaped the present wrapper")
    if ("x86_64_out8((uint16_t)M61_POST_PORT" not in post_script or
            "M61_POST(M61_POST_KERNEL_ENTRY)" not in post_script or
            "M61_POST(M61_POST_DESKTOP_PRESENT)" not in post_script):
        raise RuntimeError("independent port-0x80 POST layer is incomplete")

    print("EARLY_FRAMEBUFFER_WRITE_ADDED=NO")
    print("ACQUIRE_FRAMEBUFFERS_WRITES_PIXELS=NO")
    print("PRE_SAFE_POINT_BOOT_CONSOLE_FRAMEBUFFER_WRITES=0")
    print("GENERIC_X86_64_OUT8_MODIFIED=NO")
    print("POST_FALLBACK_INDEPENDENT=YES")
    print("POST90_FAULT_DIAGNOSTIC_BOUNDED=YES")
    print("HANDOFF_GAP_POST_DIAGNOSTIC_BOUNDED=YES")
    print("POST37_CONTROL_FLOW_DIAGNOSTIC_BOUNDED=YES")
    print("EARLY_STATIC_HISTORY_BOUNDED=YES")
    print("FRAMEBUFFER_ACTIVATION_POINT=after successful normal framebuffer present")


if __name__ == "__main__":
    main()
