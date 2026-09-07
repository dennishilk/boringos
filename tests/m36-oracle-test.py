#!/usr/bin/env python3
"""Reject mutations of a real captured framebuffer; never synthesize proof."""
import copy
import json
from pathlib import Path
import runpy
import sys
import tempfile

root = Path(__file__).resolve().parent.parent
oracle = runpy.run_path(str(root / "tests/validate-m36-terminal-screenshot.py"))
folder = Path(sys.argv[1]) if len(sys.argv) == 2 else root / "build/m36-desktop-reference"
ppm = folder / "prompt.ppm"
metadata = json.loads((folder / "prompt.json").read_text())
oracle["validate"](ppm, metadata, "prompt")
width, height, pixels = oracle["parse_ppm"](ppm)
checks = 0
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp) / "deliberately-invalid.ppm"
    for x, y in ((0, 0), (4, 4), (11, 11), (790, 550), (400, 301)):
        damaged = bytearray(pixels)
        damaged[(y * width + x) * 3] ^= 0x80
        path.write_bytes(f"P6\n{width} {height}\n255\n".encode() + damaged)
        try:
            oracle["validate"](path, metadata, "prompt")
        except ValueError:
            checks += 1
        else:
            raise AssertionError(f"wrong pixel accepted at {x},{y}")
    for kind in ("dimensions", "geometry", "focus"):
        changed = copy.deepcopy(metadata)
        if kind == "dimensions": changed["width"] += 1
        if kind == "geometry": changed["tiles"][0]["x"] += 1
        if kind == "focus": changed["focus"] = 0
        try:
            oracle["validate"](ppm, changed, "prompt")
        except ValueError:
            checks += 1
        else:
            raise AssertionError(f"wrong {kind} accepted")
print(f"M36 visual negative oracle: {checks} corruptions rejected.")
