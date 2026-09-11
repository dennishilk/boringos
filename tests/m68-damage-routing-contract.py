#!/usr/bin/env python3
"""Bind the M68 dynamic damage matrix to the production server routes."""
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DISPLAY = (ROOT / "user/boring-display/server.c").read_text()
WM = (ROOT / "user/boringwm/main.c").read_text()
HOST = (ROOT / "tests/m68-safe-higher-gop-host.sh").read_text()


def ordered(source, fragments, label):
    positions = [source.find(fragment) for fragment in fragments]
    if any(position < 0 for position in positions) or positions != sorted(positions):
        raise RuntimeError(f"{label} missing or reordered: {positions!r}")


old_request = DISPLAY[
    DISPLAY.index("static void old_request(") : DISPLAY.index("static void control(")
]
legacy_commit = old_request[
    old_request.index("r->type == BORING_DISPLAY_REQUEST_COMMIT") :
    old_request.index("r->type == BORING_DISPLAY_REQUEST_DESTROY")
]
if "present();" not in legacy_commit:
    raise RuntimeError("legacy full COMMIT no longer routes to full present")

damage_commit = old_request[
    old_request.index("r->type == BORING_DISPLAY_REQUEST_COMMIT_DAMAGE") :
]
ordered(
    damage_commit,
    (
        "boring_display_surface_commit(&core, endpoint, token)",
        "present_damage(token, &region)",
    ),
    "COMMIT_DAMAGE production route",
)

layout = DISPLAY[
    DISPLAY.index("static void present_layout(") :
    DISPLAY.index("static bool present_damage(")
]
ordered(
    layout,
    (
        "boring_display_cursor_damage_restore",
        "display_managed_compose_scene_region",
        "boring_display_cursor_damage_reset",
        "boring_framebuffer_present_region",
    ),
    "bounded layout cursor/compose/present route",
)

scene = DISPLAY[
    DISPLAY.index("static bool present_damage(") :
    DISPLAY.index("static void present_focus_borders(")
]
ordered(
    scene,
    (
        "display_managed_damage_region",
        "boring_display_cursor_damage_restore",
        "display_managed_compose_scene_region",
        "boring_display_cursor_damage_reset",
        "boring_framebuffer_present_region",
    ),
    "bounded scene cursor/compose/present route",
)

cursor = DISPLAY[
    DISPLAY.index("static void present_cursor_move(") :
    DISPLAY.index("static void forget_peer(")
]
if cursor.count("boring_framebuffer_present_region") != 2:
    raise RuntimeError("pure cursor move no longer has exactly two regional presents")
if "display_managed_compose_scene" in cursor or "present();" in cursor:
    raise RuntimeError("pure cursor move unexpectedly routes through scene/full compose")

wm_input = WM[WM.index("static void handle_input(") : WM.index("static long connect_display_service(")]
ordered(
    wm_input,
    (
        "acknowledge_input();\n            acknowledged = true;",
        'desktop_say(action == WM_FOCUS ? "wm: action focus',
        "sync_focus();",
    ),
    "immediate pointer ACK before focus synchronization",
)

display_control = DISPLAY[
    DISPLAY.index("static void control(") : DISPLAY.index("static void receive(")
]
if "focus_present_pending = true;" not in display_control:
    raise RuntimeError("focus transition is no longer queued atomically")
display_loop = DISPLAY[DISPLAY.rindex("int boring_main(void)") :]
ordered(
    display_loop,
    (
        "focus_present_pending && !input_pending",
        "BORING_EVENT_QUERY",
        "present_focus_borders();",
        "focus_present_pending = false;",
    ),
    "deferred/coalesced focus presentation",
)

for test in (
    "tests/m68-layout-host.c",
    "tests/m68-damage-matrix-host.c",
    "tests/m68-performance-host.c",
    "framebuffer-present-region-host-test",
    "mouse-latency-host-test",
):
    if test not in HOST:
        raise RuntimeError(f"M68 focused gate lost required damage test: {test}")

print("M68_DAMAGE_ROUTING_LEGACY_COMMIT=FULL")
print("M68_DAMAGE_ROUTING_COMMIT_DAMAGE=BOUNDED")
print("M68_DAMAGE_ROUTING_LAYOUT=BOUNDED")
print("M68_FOCUS_INPUT_CRITICAL_FULL_PRESENTS=0")
print("M68_FOCUS_POINTER_ACK=IMMEDIATE")
print("M68_FOCUS_PRESENT=ATOMIC_DEFERRED_COALESCED")
