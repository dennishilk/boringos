#!/usr/bin/env python3
"""Independent full-frame oracle for real QEMU screendumps; never paints evidence."""
import hashlib
import json
import re
import runpy
import sys
from pathlib import Path

parse_ppm = runpy.run_path(str(Path(__file__).with_name("validate-display-screenshot.py")))["parse_ppm"]
GLYPHS = (
    (14, 17, 16, 16, 16, 17, 14), (16, 16, 16, 16, 16, 16, 31),
    (31, 4, 4, 4, 4, 4, 31), (31, 16, 16, 30, 16, 16, 31),
    (17, 25, 25, 21, 19, 19, 17), (31, 4, 4, 4, 4, 4, 4), (0,) * 7,
)
LETTERS = ((14, 17, 17, 31, 17, 17, 17), (30, 17, 17, 30, 17, 17, 30), GLYPHS[0])
BODY = (0x32382A, 0x26363C, 0x3E2E34)
ACCENT = (0xB8BB26, 0x83A598, 0xD3869B)


def frames(log):
    result = []
    current = None
    # Serial files are sampled while QEMU is still writing. An unterminated
    # final line is incomplete input, not a malformed completed witness.
    for line in log.splitlines(keepends=True):
        if not line.endswith("\n"):
            break
        line = line.rstrip("\r\n")
        match = re.search(r"wm: frame=(\d+) count=(\d+) focus=(\d+)$", line)
        if match:
            current = dict(zip(("frame", "count", "focus"), map(int, match.groups())))
            current["tiles"] = []
        elif current and "wm: tile " in line:
            values = list(map(int, line.split("wm: tile ", 1)[1].split()))
            if len(values) != 8:
                raise ValueError("malformed tile witness")
            current["tiles"].append(dict(zip(("token", "surface", "x", "y", "width", "height", "border", "pid"), values)))
        elif current and line.endswith("wm: frame ready"):
            if len(current["tiles"]) != current["count"]:
                raise ValueError("incomplete frame witness")
            result.append(current)
            current = None
    return result


def expected_layout(width, height, count):
    if not (width >= 100 and height >= 100 and 1 <= count <= 6):
        raise ValueError("unsupported acceptance geometry")
    available_width, available_height = width - 8, height - 8
    if count == 1:
        return [(4, 4, available_width, available_height, 3)]
    master = (available_width - 8) * 3 // 5
    result = [(4, 4, master, available_height, 3)]
    stack_count = count - 1
    quotient, remainder = divmod(available_height - (stack_count - 1) * 8, stack_count)
    y = 4
    for i in range(stack_count):
        h = quotient + (i < remainder)
        result.append((4 + master + 8, y, available_width - master - 8, h, 3))
        y += h + 8
    if y - 8 != height - 4:
        raise ValueError("stack did not exhaust available height")
    return result


def rgb(color):
    return bytes((color >> 16, (color >> 8) & 255, color & 255))


def client_pixel(client, x, y):
    color = BODY[client]
    if 4 <= y < 6:
        color = ACCENT[client]
    if 16 <= x < 112 and 20 <= y < 34:
        glyph, column = divmod(x - 16, 12)
        column //= 2
        row = (y - 20) // 2
        bits = LETTERS[client][row] if glyph == 7 else GLYPHS[glyph][row]
        if column < 5 and bits & (1 << (4 - column)):
            color = 0xEBDBB2
    return rgb(color)


def validate(ppm, metadata):
    width, height, actual = parse_ppm(Path(ppm))
    if (width, height) != (metadata["width"], metadata["height"]):
        raise ValueError("framebuffer does not match serial dimensions")
    tiles = metadata["tiles"]
    expected_rects = expected_layout(width, height, len(tiles))
    expected = bytearray(rgb(0x282828) * (width * height))
    focused = 0
    for tile, rect in zip(tiles, expected_rects):
        if tuple(tile[k] for k in ("x", "y", "width", "height", "border")) != rect:
            raise ValueError(f"layout witness differs from independent oracle: {tile}")
        x, y, w, h, border = rect
        client = tile["pid"] - 3
        if client not in range(3):
            raise ValueError("tile is not one of the three independent acceptance processes")
        is_focus = tile["token"] == metadata["focus"]
        focused += is_focus
        color = rgb(0xEBDBB2 if is_focus else 0x3C3836)
        for row in range(h):
            for col in range(w):
                value = color
                if border <= row < h - border and border <= col < w - border:
                    value = client_pixel(client, col - border, row - border)
                offset = ((y + row) * width + x + col) * 3
                expected[offset:offset + 3] = value
    if focused != 1:
        raise ValueError("focus is not unique")
    cx, cy = metadata["cursor"]
    for row in range(12):
        for col in range(row // 2 + 1):
            if cx + col < width and cy + row < height:
                offset = ((cy + row) * width + cx + col) * 3
                expected[offset:offset + 3] = rgb(0xFFEEF5 if col == 0 else 0x30EEF5)
    if actual != expected:
        first = next(i for i, (a, b) in enumerate(zip(actual, expected)) if a != b)
        pixel = first // 3
        raise ValueError(f"first pixel divergence at {pixel % width},{pixel // width}: "
                         f"actual={list(actual[pixel * 3:pixel * 3 + 3])}, "
                         f"expected={list(expected[pixel * 3:pixel * 3 + 3])}")
    print(f"M35 full-frame validator passed: {Path(ppm).name} {width}x{height}, "
          f"{len(tiles)} real client tiles, {width * height} exact pixels, "
          f"sha256={hashlib.sha256(Path(ppm).read_bytes()).hexdigest()}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: validate-wm-screenshot.py frame.ppm frame.json")
    validate(sys.argv[1], json.loads(Path(sys.argv[2]).read_text()))
