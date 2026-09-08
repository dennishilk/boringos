#!/usr/bin/env python3
"""Bounded M66 descriptor-fallback POST80 and diagnostic contract."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
HEADER = (ROOT / "kernel/include/boring/m61_runtime_hid.h").read_text()
BREADCRUMBS = (ROOT / "kernel/core/m61_physical_breadcrumbs.c").read_text()
MIXED = (ROOT / "kernel/arch/x86_64/xhci_mixed.c").read_text()
XHCI = (ROOT / "kernel/arch/x86_64/xhci.c").read_text()

EXPECTED = {
    "M66_POST_CONFIGURATION_VALID": 0x80,
    "M66_POST_BOOT_MOUSE_FOUND": 0x81,
    "M66_POST_GENERIC_MOUSE_CANDIDATE": 0x82,
    "M66_POST_HID_DESCRIPTOR_ACCEPTED": 0x83,
    "M66_POST_REPORT_DESCRIPTOR_REQUESTED": 0x84,
    "M66_POST_REPORT_DESCRIPTOR_RECEIVED": 0x85,
    "M66_POST_REPORT_DESCRIPTOR_PARSED": 0x86,
    "M66_POST_MOUSE_LAYOUT_SELECTED": 0x87,
    "M66_POST_DOWNSTREAM_HID_SUPPORTED": 0xD7,
    "M66_POST_DOWNSTREAM_HID_UNSUPPORTED": 0xD8,
    "M66_POST_DOWNSTREAM_INTERRUPT_ARMED": 0xD9,
    "M66_POST_DOWNSTREAM_MOUSE_REPORT": 0xDA,
    "M66_POST_CANONICAL_MOUSE_MOVE": 0xDB,
}

codes = {
    name: int(value, 16)
    for name, value in re.findall(
        r"\b(M66_POST_[A-Z0-9_]+)\s*=\s*0x([0-9a-fA-F]{2})", HEADER
    )
}
for name, value in EXPECTED.items():
    if codes.get(name) != value:
        raise RuntimeError(f"{name} expected 0x{value:02X}, got {codes.get(name)!r}")

if len(set(EXPECTED.values())) != len(EXPECTED):
    raise RuntimeError("M66 HID phase POST codes collide")

for path in (
    ROOT / "kernel/arch/x86_64/xhci.c",
    ROOT / "kernel/core/pmm.c",
    ROOT / "kernel/core/usb_mass_storage_impl.inc",
    ROOT / "tests/m61-physical-trace-kernel.sh",
):
    text = path.read_text()
    for name, value in re.findall(
        r"\b([A-Z0-9_]*POST[A-Z0-9_]*)\s*=\s*0x([0-9a-fA-F]{2})", text
    ):
        numeric = int(value, 16)
        if 0x80 <= numeric <= 0x87:
            raise RuntimeError(
                f"M66 descriptor phase collides with {path.name}:{name}=0x{numeric:02X}"
            )

for token in (
    "20U +",
    "M66_POST_CONFIGURATION_VALID",
    "M66_POST_MOUSE_LAYOUT_SELECTED",
    "M66_POST_ALL_XHCI_READY",
    "M66_POST_RESET_SPEED_VALID",
):
    if token not in BREADCRUMBS:
        raise RuntimeError(f"M66 one-shot POST mapping missing {token}")

for token in (
    "M66 HID DEVICE",
    " controller=",
    " root_port=",
    " route=",
    " downstream_port=",
    " vid=",
    " pid=",
    "M66 HID INTERFACE",
    " alternate=",
    "M66 HID DESCRIPTOR",
    " report_length=",
    "M66 HID ENDPOINT",
    " direction=",
    " transfer=",
    "M66 HID REJECTION reason=",
):
    if token not in MIXED:
        raise RuntimeError(f"M66 bounded descriptor diagnostic missing {token}")

sequence = (
    "M66_POST_GENERIC_MOUSE_CANDIDATE",
    "M66_POST_HID_DESCRIPTOR_ACCEPTED",
    "M66_POST_REPORT_DESCRIPTOR_REQUESTED",
    "M66_POST_REPORT_DESCRIPTOR_RECEIVED",
    "M66_POST_REPORT_DESCRIPTOR_PARSED",
    "M66_POST_MOUSE_LAYOUT_SELECTED",
)
combined = MIXED + XHCI
positions = [combined.find(token) for token in sequence]
if any(position < 0 for position in positions):
    raise RuntimeError("M66 generic report-descriptor witness is incomplete")

print("M66_POST80_COLLISION_AUDIT=PASS")
print("M66_DESCRIPTOR_DIAGNOSTICS=BOUNDED")
print("M66_REPORT_DESCRIPTOR_WITNESSES=COMPLETE")
