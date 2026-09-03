#!/usr/bin/env python3
from pathlib import Path
import re

source = Path("kernel/core/m61_wm_terminal_post.c").read_text()
legacy_reserved = (
    set(range(0x30, 0x34)) |
    set(range(0x40, 0x5E)) |
    set(range(0x61, 0x80)) |
    set(range(0x80, 0x9A)) |
    set(range(0xA0, 0xC4)) |
    set(range(0xD0, 0x100))
)
expected = {0x5E, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F}
found = {
    int(value, 16)
    for value in re.findall(
        r"M61_POST_[A-Z0-9_]+\s*=\s*0x([0-9a-fA-F]{2})", source
    )
}
if found != expected:
    raise SystemExit(f"M61 WM/terminal POST code set mismatch: {sorted(found)}")
collision = found & legacy_reserved
if collision:
    raise SystemExit(f"M61 WM/terminal POST collision: {sorted(collision)}")
for frozen in (0x69, 0x6A, 0x6F, 0x90, 0x91):
    if frozen in found:
        raise SystemExit(f"frozen POST code reused: 0x{frozen:02X}")
for token in (
    "--wrap=boring_ipc_service_connect",
    "--wrap=boring_ipc_poll",
    "--wrap=boring_ipc_send",
    "--wrap=boring_ipc_receive",
    "--wrap=x86_64_syscall_dispatch_m36",
):
    if token not in Path("tests/m61-build.sh").read_text():
        raise SystemExit(f"M61 build missing wrapper: {token}")
print("M61 WM->automatic-terminal POST namespace/wrapper audit: PASS")
