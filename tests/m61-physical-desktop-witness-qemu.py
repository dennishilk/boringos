#!/usr/bin/env python3
"""Run the established M61 two-boot acceptance with the physical desktop witness."""
import runpy
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "tests/m61-bootable-persistent-usb-qemu.py"
TEMP = ROOT / "tests/.m61-bootable-persistent-usb-qemu-witness.py"


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"M61 witness patch {label} expected once, found {count}")
    return text.replace(old, new, 1)


def run():
    text = SOURCE.read_text()
    helper_anchor = "    def settled_capture(frame):\n"
    helper = r'''    def capture_auto_terminal_wallpaper(frame, terminal_pid):
        geometry = re.search(r"boring-framebuffer: (\d+)x(\d+)x(?:24|32)", text())
        if geometry is None:
            raise RuntimeError("missing framebuffer geometry for automatic terminal witness")
        width, height = map(int, geometry.groups())
        if (width, height) != (800, 600):
            raise RuntimeError("M61 wallpaper witness requires exact 800x600 QEMU scanout")
        current = text()
        requested = "M61 PHYSICAL: automatic terminal spawn requested"
        if current.count(requested) != 1:
            raise RuntimeError("automatic terminal spawn request was not exactly once")
        pid_matches = re.findall(r"M61 PHYSICAL: automatic terminal spawn pid=(\d+)", current)
        if pid_matches != [str(terminal_pid)]:
            raise RuntimeError(f"automatic terminal pid witness mismatch: {pid_matches!r}")
        if "M61 PHYSICAL: automatic terminal spawn FAILED" in current:
            raise RuntimeError("automatic terminal spawn reported failure")
        if "wm: Super+Return spawned /bin/boring-terminal" in current:
            raise RuntimeError("keyboard launcher fired before automatic-terminal proof")
        if current.count("boring-spawn: VFS executable source /bin/boring-terminal") != 1:
            raise RuntimeError("automatic witness did not execute exactly one terminal image")
        if frame["count"] != 1 or len(frame["tiles"]) != 1:
            raise RuntimeError(f"automatic terminal did not become the sole WM tile: {frame!r}")
        tile = frame["tiles"][0]
        if tile["pid"] != terminal_pid or frame["focus"] != tile["token"]:
            raise RuntimeError("automatic terminal WM pid/focus does not match spawn pid")
        witness("M61 TRACE [26+] DESKTOP PRESENT")
        witness("M61 TRACE [27+] TERMINAL START")

        qmp("stop")
        try:
            ppm = out / "automatic-terminal-wallpaper.ppm"
            qmp("screendump", {"filename": str(ppm)})
            actual_width, actual_height, actual = WM["parse_ppm"](ppm)
            if (actual_width, actual_height) != (width, height):
                raise ValueError("automatic terminal screenshot geometry mismatch")
            expected = WM["desktop_background"](width, height)

            clean_margin = None
            clean_hash = None
            for name, first_x, last_x in (("left", 0, 4), ("right", width - 4, width)):
                actual_region = bytearray()
                expected_region = bytearray()
                for row in range(height):
                    first = (row * width + first_x) * 3
                    last = (row * width + last_x) * 3
                    actual_region.extend(actual[first:last])
                    expected_region.extend(expected[first:last])
                if actual_region == expected_region:
                    clean_margin = name
                    clean_hash = hashlib.sha256(actual_region).hexdigest()
                    break
            if clean_margin is None:
                raise ValueError("normal DISPLAY_PRESENT did not preserve exact wallpaper margin")

            x0 = tile["x"] + tile["border"] + 16
            y0 = tile["y"] + tile["border"] + 16
            x1 = min(tile["x"] + tile["width"] - tile["border"], x0 + 240)
            y1 = min(tile["y"] + tile["height"] - tile["border"], y0 + 160)
            different = 0
            sampled = 0
            for row in range(y0, y1):
                for column in range(x0, x1):
                    offset = (row * width + column) * 3
                    sampled += 1
                    if actual[offset:offset + 3] != expected[offset:offset + 3]:
                        different += 1
            if sampled == 0 or different < 1000:
                raise ValueError(
                    f"automatic terminal is not visually distinct from wallpaper: {different}/{sampled}")
            (out / "physical-desktop-witness.txt").write_text(
                "AUTO_TERMINAL_PROVEN=YES\n"
                f"automatic_spawn_pid={terminal_pid}\n"
                "automatic_spawn_requests=1\n"
                "keyboard_launcher_before_proof=NO\n"
                "wm_frame_count=1\n"
                f"terminal_tile_pid={tile['pid']}\n"
                "WALLPAPER_PROVEN=YES\n"
                "wallpaper_composition=normal DISPLAY_PRESENT\n"
                f"exact_wallpaper_margin={clean_margin}\n"
                f"wallpaper_margin_sha256={clean_hash}\n"
                f"terminal_pixels_distinct={different}/{sampled}\n"
                "EXTRA_EARLY_PRESENT_ADDED=NO\n")
        finally:
            qmp("cont")

'''
    text = replace_once(text, helper_anchor, helper + helper_anchor,
                        "automatic desktop screenshot helper")

    after_retained = '''        capture_retained_trace(trace_meta)\n        current = text()\n'''
    automatic = '''        capture_retained_trace(trace_meta)\n        automatic_match = wait(\n            lambda current: re.search(\n                r"M61 PHYSICAL: automatic terminal spawn pid=(\\d+)", current),\n            "automatic terminal spawn pid")\n        automatic_pid = int(automatic_match.group(1))\n        witness("boring-spawn: VFS executable source /bin/boring-terminal")\n        witness("boring-spawn: VFS executable source /bin/boring-shell")\n        automatic_frame = latest(1)\n        capture_auto_terminal_wallpaper(automatic_frame, automatic_pid)\n        current = text()\n'''
    text = replace_once(text, after_retained, automatic,
                        "automatic terminal proof before input")

    manual = '''        key("ret", super_key=True)\n        witness("wm: Super+Return spawned /bin/boring-terminal")\n        witness("boring-spawn: VFS executable source /bin/boring-terminal")\n        witness("boring-spawn: VFS executable source /bin/boring-shell")\n        latest(1)\n\n'''
    text = replace_once(text, manual, "",
                        "remove obsolete keyboard terminal launch")

    TEMP.write_text(text)
    try:
        runpy.run_path(str(TEMP), run_name="__main__")
    finally:
        TEMP.unlink(missing_ok=True)


if __name__ == "__main__":
    run()
