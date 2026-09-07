#!/usr/bin/env python3
"""The serial oracle must accept streaming prefixes, never malformed frames."""
import runpy
from pathlib import Path

frames = runpy.run_path(str(Path(__file__).with_name("validate-wm-screenshot.py")))["frames"]
sample = ("Syscall DEBUG_WRITE: wm: frame=4 count=1 focus=257\n\n"
          "Syscall DEBUG_WRITE: wm: tile 257 257 4 4 792 592 3 3\n\n"
          "Syscall DEBUG_WRITE: wm: frame ready\n")
for length in range(len(sample)):
    assert frames(sample[:length]) == [], length
assert len(frames(sample)) == 1
assert frames(sample + "wm: frame=5 count=1 focus=257\nwm: tile 257 257") == frames(sample)
for malformed in (sample.replace("792 592", "792"), sample.replace("count=1", "count=2")):
    try:
        frames(malformed)
    except ValueError:
        pass
    else:
        raise AssertionError("completed malformed witness was accepted")
print("M35 streaming serial oracle tests passed.")
