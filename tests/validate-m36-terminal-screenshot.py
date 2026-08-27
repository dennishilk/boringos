#!/usr/bin/env python3
"""Decode the real M36 terminal bitmap font from a QEMU screendump."""
import ast
import json
import re
import runpy
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
parse_ppm = runpy.run_path(str(ROOT / "tests/validate-display-screenshot.py"))["parse_ppm"]
TERM_BG = bytes((0x1D, 0x20, 0x21))
TERM_FG = bytes((0xEB, 0xDB, 0xB2))
MARGIN_X, MARGIN_Y, CELL_W, CELL_H = 4, 4, 6, 8
MAX_COLS, MAX_ROWS = 128, 64


def glyphs():
    text = (ROOT / "user/boring-terminal/render.c").read_text()
    result = {}
    for literal, values in re.findall(r"case\s+('(?:\\.|[^'])+'):\s+GLYPH\(([^)]*)\);", text):
        try:
            ch = ast.literal_eval(literal)
        except (SyntaxError, ValueError):
            continue
        nums = tuple(int(item.strip(), 0) for item in values.split(','))
        if len(ch) == 1 and len(nums) == 7:
            result[nums] = ch
    return result


GLYPHS = glyphs()


def pixel(data, width, x, y):
    off = (y * width + x) * 3
    return data[off:off + 3]


def decode_tile(data, width, height, tile):
    x0 = tile["x"] + tile["border"]
    y0 = tile["y"] + tile["border"]
    view_w = tile["width"] - 2 * tile["border"]
    view_h = tile["height"] - 2 * tile["border"]
    cols = min(MAX_COLS, (view_w - 2 * MARGIN_X) // CELL_W)
    rows = min(MAX_ROWS, (view_h - 2 * MARGIN_Y) // CELL_H)
    decoded = []
    for row in range(rows):
        chars = []
        for col in range(cols):
            gx = x0 + MARGIN_X + col * CELL_W
            gy = y0 + MARGIN_Y + row * CELL_H
            bits = []
            valid = True
            for yy in range(7):
                value = 0
                for xx in range(5):
                    if gx + xx >= width or gy + yy >= height:
                        valid = False
                        break
                    rgb = pixel(data, width, gx + xx, gy + yy)
                    if rgb == TERM_FG:
                        value |= 1 << (4 - xx)
                    elif rgb != TERM_BG:
                        valid = False
                        break
                if not valid:
                    break
                bits.append(value)
            chars.append(GLYPHS.get(tuple(bits), "?") if valid else "?")
        decoded.append("".join(chars).rstrip())
    return decoded


def decode(ppm, metadata):
    width, height, data = parse_ppm(Path(ppm))
    if (width, height) != (metadata["width"], metadata["height"]):
        raise ValueError("framebuffer geometry differs from serial witness")
    return {tile["pid"]: decode_tile(data, width, height, tile)
            for tile in metadata["tiles"]}


def contains(rows, text):
    return any(text in row for row in rows)


def validate(ppm, metadata, mode):
    screens = decode(ppm, metadata)
    if mode == "prompt":
        if len(screens) != 1 or not any(contains(rows, "boring@boringos:/$") for rows in screens.values()):
            raise ValueError(f"graphical shell prompt missing: {screens}")
    elif mode == "fetch":
        if len(screens) != 1:
            raise ValueError("fetch proof requires exactly one terminal")
        rows = next(iter(screens.values()))
        for text in ("BoringOS", "Kernel: BoringKernel 0.0.36-dev", "Root FS: BoringFS"):
            if not contains(rows, text):
                raise ValueError(f"graphical boringfetch text missing: {text}: {rows}")
    elif mode == "dual-b":
        focus = metadata["focus"]
        focused = next((tile["pid"] for tile in metadata["tiles"] if tile["token"] == focus), None)
        if len(screens) != 2 or focused is None or not contains(screens[focused], "terminalb"):
            raise ValueError(f"focused terminal-b text missing: {screens}")
        if any(pid != focused and contains(rows, "terminalb") for pid, rows in screens.items()):
            raise ValueError("terminal-b input leaked into non-focused PTY")
    elif mode == "dual-a":
        focus = metadata["focus"]
        focused = next((tile["pid"] for tile in metadata["tiles"] if tile["token"] == focus), None)
        if len(screens) != 2 or focused is None or not contains(screens[focused], "terminala"):
            raise ValueError(f"focused terminal-a text missing: {screens}")
        other = [pid for pid in screens if pid != focused]
        if len(other) != 1 or not contains(screens[other[0]], "terminalb"):
            raise ValueError("other terminal did not retain its independent terminalb input")
        if contains(screens[focused], "terminalb") or contains(screens[other[0]], "terminala"):
            raise ValueError("cross-terminal input leak detected")
    elif mode == "single-after-close":
        if len(screens) != 1:
            raise ValueError("graceful close did not leave exactly one graphical terminal")
    else:
        raise ValueError(f"unknown validation mode: {mode}")
    print(f"M36 visual validator passed: {mode}; terminal pids={sorted(screens)}")
    return screens


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit("usage: validate-m36-terminal-screenshot.py frame.ppm frame.json mode")
    meta = json.loads(Path(sys.argv[2]).read_text())
    decoded = validate(Path(sys.argv[1]), meta, sys.argv[3])
    for pid, rows in sorted(decoded.items()):
        print(f"--- pid {pid} ---")
        for row in rows:
            if row:
                print(row)
