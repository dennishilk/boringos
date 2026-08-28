#!/usr/bin/env python3
"""Replay the unchanged real three-client acceptance against the M42 library.

Reuse the scenario and full-frame oracle instead of copying either. A separate
output directory keeps this milestone's actual guest evidence identifiable.
"""
import runpy
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

if __name__ == "__main__":
    subprocess.run(["make", "client-host-test"], cwd=ROOT, check=True)
    scenario = runpy.run_path(str(ROOT / "tests/m41-shortcuts-qemu.py"))
    run = scenario["run"]
    run.__globals__["OUT"] = ROOT / "build/m42-client-reference"
    run()
    # Verify all three shipped binaries actually link the extracted helper.
    for app in ("boring-terminal", "boring-edit", "boring-files"):
        symbols = subprocess.check_output(["nm", str(ROOT / "build/user" / (app + ".elf"))], text=True)
        for name in ("open", "publish", "commit", "receive", "unregister", "release"):
            assert " T boring_client_" + name + "\n" in symbols, (app, name)
    (ROOT / "build/m42-client-reference/SUCCESS.txt").write_text(
        "M42 real three-client acceptance and shared-helper linkage SUCCESS\n")
    print("M42 real three-client framebuffer/input/save/close/drain and shared-helper linkage SUCCESS")
