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
    helper = r'''    def prove_diagnostic_write_bypass():
        witness("M61 FRAMEBUFFER DIAGNOSTIC WRITES BYPASSED count=")
        witness("M61 TRACE [01+] SERIAL PROBE")
        current = text()
        forbidden = (
            "M61 FRAMEBUFFER TINY PROBE FAILED",
            "M61 FRAMEBUFFER WITNESS index=",
            "M61 FRAMEBUFFER TRACE SELECTED WITNESS FAILED",
            "M61 FRAMEBUFFER TRACE READY index=",
        )
        present = [marker for marker in forbidden if marker in current]
        if present:
            raise RuntimeError(
                f"diagnostic framebuffer write path unexpectedly executed: {present!r}")
        (out / "physical-trace-proof.txt").write_text(
            "DIAGNOSTIC_FRAMEBUFFER_WRITES_BYPASSED=YES\n"
            "EARLY_DIAGNOSTIC_FRAMEBUFFER_WRITE_COUNT=0\n"
            "capture=none; metadata-only acquire_framebuffers\n")

    def boot_console_glyphs():
        source = (ROOT / "kernel/core/pixel_font.c").read_text()
        glyphs = {}
        for character, values in re.findall(
                r"case '(.)': GLYPH\(([^)]*)\); break;", source):
            glyphs[character] = tuple(
                int(value.strip()) for value in values.split(","))
        return glyphs

    def boot_console_text_matches(pixels, width, height, x, y,
                                  value, color, scale=1):
        glyphs = boot_console_glyphs()
        foreground = bytes(color)
        cursor_x = x
        for character in value:
            rows = glyphs.get(character)
            if rows is None:
                raise ValueError(f"missing boot-console glyph {character!r}")
            for glyph_y, bits in enumerate(rows):
                for glyph_x in range(5):
                    expected = (bits & (1 << (4 - glyph_x))) != 0
                    for scale_y in range(scale):
                        for scale_x in range(scale):
                            pixel_x = cursor_x + glyph_x * scale + scale_x
                            pixel_y = y + glyph_y * scale + scale_y
                            if pixel_x >= width or pixel_y >= height:
                                return False
                            offset = (pixel_y * width + pixel_x) * 3
                            actual = pixels[offset:offset + 3] == foreground
                            if actual != expected:
                                return False
            cursor_x += 6 * scale
        return True

    def capture_boot_console():
        marker = "BOOT-CONSOLE framebuffer activated at safe normal present"
        deadline = time.monotonic() + 120
        while marker not in text():
            current = text()
            if "BOOT-CONSOLE framebuffer activation rejected" in current:
                raise RuntimeError("boot-console framebuffer activation rejected")
            if "BOOT-CONSOLE framebuffer handoff complete" in current:
                raise RuntimeError("boot-console capture missed the desktop handoff")
            if vm.poll() is not None:
                raise RuntimeError(
                    f"QEMU exited ({vm.returncode}) before boot-console capture")
            if time.monotonic() >= deadline:
                raise RuntimeError(
                    f"first missing witness: {marker}\n{current[-16000:]}")
            time.sleep(0.001)

        qmp("stop")
        try:
            current = text()
            if "BOOT-CONSOLE framebuffer handoff complete" in current:
                raise RuntimeError("boot-console was replaced before QMP capture")
            geometry = re.search(
                r"boring-framebuffer: (\d+)x(\d+)x(?:24|32)", current)
            if geometry is None:
                raise RuntimeError("missing framebuffer geometry for boot console")
            width, height = map(int, geometry.groups())
            if (width, height) != (800, 600):
                raise RuntimeError(
                    "M61 boot-console witness requires exact 800x600 QEMU scanout")

            ppm = out / "boot-console-during-boot.ppm"
            qmp("screendump", {"filename": str(ppm)})
            actual_width, actual_height, pixels = WM["parse_ppm"](ppm)
            if (actual_width, actual_height) != (width, height):
                raise ValueError("boot-console screenshot geometry mismatch")

            text_color = (0xe8, 0xef, 0xf2)
            cyan = (0x3a, 0xcd, 0xdc)
            status_colors = {
                "[ OK ]": (0x72, 0xd6, 0x8a),
                "[ .. ]": (0xe4, 0xb8, 0x68),
                "[FAIL]": (0xff, 0x60, 0x60),
            }
            labels = (
                "CPU inventory", "PCI inventory", "SMBIOS",
                "Physical memory manager", "Virtual memory manager",
                "Kernel heap", "Exceptions", "Input", "IRQ", "PIT",
                "xHCI controller", "USB addressing", "USB descriptors",
                "USB HID", "USB mass storage",
                "BoringFS persistent root", "boring-init", "boring-display",
                "BoringWM", "automatic terminal", "desktop present",
            )
            if not boot_console_text_matches(
                    pixels, width, height, 44, 34,
                    "BoringOS booting...", text_color, 2):
                raise ValueError("graphical boot-console header pixels absent")
            if not boot_console_text_matches(
                    pixels, width, height, 44, 58,
                    "BoringKernel 0.0.62-dev", cyan):
                raise ValueError("graphical boot-console version pixels absent")

            status_summary = []
            stage_stride = (height - 92 - 44) // len(labels)
            for index, label in enumerate(labels):
                matches = re.findall(
                    rf"^BOOT-CONSOLE (\[ OK \]|\[ \.\. \]|\[FAIL\]) "
                    rf"{re.escape(label)}(?:\: [^\n]+)?$",
                    current, re.MULTILINE)
                status = matches[-1] if matches else "[ .. ]"
                if status == "[FAIL]" and not re.search(
                        rf"^BOOT-CONSOLE \[FAIL\] {re.escape(label)}: .+$",
                        current, re.MULTILINE):
                    raise ValueError(f"FAIL stage lacks a real reason: {label}")
                y = 92 + index * stage_stride
                if not boot_console_text_matches(
                        pixels, width, height, 44, y, status,
                        status_colors[status]):
                    raise ValueError(
                        f"graphical boot-console status mismatch: {status} {label}")
                if not boot_console_text_matches(
                        pixels, width, height, 92, y, label, text_color):
                    raise ValueError(
                        f"graphical boot-console stage text absent: {label}")
                status_summary.append(f"{status} {label}")

            early_success = [
                f"BOOT-CONSOLE [ OK ] {label}" for label in labels[:18]
            ]
            positions = [current.find(line) for line in early_success]
            if any(position < 0 for position in positions):
                missing = [line for line, position in zip(early_success, positions)
                           if position < 0]
                raise RuntimeError(
                    f"missing real boot-console successes: {missing!r}")
            if positions != sorted(positions):
                raise RuntimeError("boot-console success replay order changed")
            if any(summary.startswith("[FAIL]") for summary in status_summary):
                raise RuntimeError(
                    f"successful M61 boot reported a failed stage: {status_summary!r}")

            (out / "boot-console-witness.txt").write_text(
                "BOOT_CONSOLE_VISIBLE_DURING_BOOT=YES\n"
                "GRAPHICAL_BOOT_STAGE_TEXT_PROVEN=YES\n"
                "REPLAY_STAGE_ORDER_PROVEN=YES\n"
                "REAL_SUCCESS_ONLY_PROVEN=YES\n"
                "EARLY_FRAMEBUFFER_WRITE_ADDED=NO\n"
                "PRE_SAFE_POINT_BOOT_CONSOLE_FRAMEBUFFER_WRITES=0\n"
                f"framebuffer_geometry={width}x{height}\n"
                f"screenshot_sha256={hashlib.sha256(ppm.read_bytes()).hexdigest()}\n"
                + "\n".join(f"stage_{index + 1:02d}={summary}"
                             for index, summary in enumerate(status_summary))
                + "\n")
        finally:
            qmp("cont")

    def capture_auto_terminal_wallpaper(frame, terminal_pid):
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
            if boot_console_text_matches(
                    actual, width, height, 44, 34,
                    "BoringOS booting...", (0xe8, 0xef, 0xf2), 2):
                raise ValueError("boot-console text remains over final desktop")
            (out / "physical-desktop-witness.txt").write_text(
                "AUTO_TERMINAL_PROVEN=YES\n"
                f"automatic_spawn_pid={terminal_pid}\n"
                "automatic_spawn_requests=1\n"
                "keyboard_launcher_before_proof=NO\n"
                "wm_frame_count=1\n"
                f"terminal_tile_pid={tile['pid']}\n"
                "WALLPAPER_PROVEN=YES\n"
                "NORMAL_FRAMEBUFFER_RUNTIME_PROVEN=YES\n"
                "wallpaper_composition=normal DISPLAY_PRESENT\n"
                f"exact_wallpaper_margin={clean_margin}\n"
                f"wallpaper_margin_sha256={clean_hash}\n"
                f"terminal_pixels_distinct={different}/{sampled}\n"
                "BOOT_CONSOLE_OVERLAY_ABSENT=YES\n"
                "EXTRA_EARLY_PRESENT_ADDED=NO\n")
        finally:
            qmp("cont")

'''
    text = replace_once(text, helper_anchor, helper + helper_anchor,
                        "no-write and automatic desktop helpers")

    text = replace_once(
        text,
        "        trace_meta = capture_physical_trace()\n",
        "        qmp(\"query-status\")\n"
        "        prove_diagnostic_write_bypass()\n"
        "        capture_boot_console()\n",
        "replace artificial framebuffer trace capture")

    after_retained = '''        capture_retained_trace(trace_meta)\n        current = text()\n'''
    automatic = '''        automatic_match = wait(\n            lambda current: re.search(\n                r"M61 PHYSICAL: automatic terminal spawn pid=(\\d+)", current),\n            "automatic terminal spawn pid")\n        automatic_pid = int(automatic_match.group(1))\n        witness("boring-spawn: VFS executable source /bin/boring-terminal")\n        witness("boring-spawn: VFS executable source /bin/boring-shell")\n        automatic_frame = latest(1)\n        capture_auto_terminal_wallpaper(automatic_frame, automatic_pid)\n        current = text()\n'''
    text = replace_once(text, after_retained, automatic,
                        "normal runtime desktop proof without retained diagnostic trace")

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
