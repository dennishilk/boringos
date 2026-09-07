#!/usr/bin/env python3
"""M66 real QEMU USB hub mouse through the existing Ring3 desktop."""
import os
import runpy
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
os.environ["M66_USB_HUB_MOUSE"] = "1"
runpy.run_path(str(ROOT / "tests/m54-usb-only-desktop-qemu.py"),
               run_name="__main__")
