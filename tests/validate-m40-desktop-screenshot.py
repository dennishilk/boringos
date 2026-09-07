#!/usr/bin/env python3
"""M40 full-frame oracle: three real clients using the existing bitmap font."""
import ast
import hashlib
import json
import re
import runpy
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
parse_ppm = runpy.run_path(str(ROOT / "tests/validate-display-screenshot.py"))["parse_ppm"]
WM_ORACLE = runpy.run_path(str(ROOT / "tests/validate-wm-screenshot.py"))
expected_layout = WM_ORACLE["expected_layout"]
desktop_background = WM_ORACLE["desktop_background"]
TERM_BG = bytes((0x1D, 0x20, 0x21))
TERM_FG = bytes((0xEB, 0xDB, 0xB2))
TERM_CURSOR = bytes((0x83, 0xA5, 0x98))
MARGIN_X, MARGIN_Y, CELL_W, CELL_H = 4, 4, 6, 8
MAX_COLS, MAX_ROWS = 160, 96


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


def decode_tile(data, expected, width, height, tile, pointer):
    x0 = tile["x"] + tile["border"]
    y0 = tile["y"] + tile["border"]
    view_w = tile["width"] - 2 * tile["border"]
    view_h = tile["height"] - 2 * tile["border"]
    cols = min(MAX_COLS, (view_w - 2 * MARGIN_X) // CELL_W)
    rows = min(MAX_ROWS, (view_h - 2 * MARGIN_Y) // CELL_H)
    decoded = []
    cursors = []
    for row in range(rows):
        chars = []
        for col in range(cols):
            gx = x0 + MARGIN_X + col * CELL_W
            gy = y0 + MARGIN_Y + row * CELL_H
            bits = []
            masks = []
            for yy in range(7):
                value = 0
                mask = 0
                for xx in range(5):
                    if gx + xx >= width or gy + yy >= height:
                        raise ValueError("glyph escapes framebuffer")
                    if (gx + xx, gy + yy) in pointer:
                        continue
                    mask |= 1 << (4 - xx)
                    rgb = pixel(data, width, gx + xx, gy + yy)
                    if rgb == TERM_FG:
                        value |= 1 << (4 - xx)
                    elif rgb != TERM_BG:
                        raise ValueError(f"invalid glyph color at {gx + xx},{gy + yy}")
                bits.append(value)
                masks.append(mask)
            if all(mask == 31 for mask in masks):
                shape = tuple(bits)
                candidates = [(shape, GLYPHS[shape])] if shape in GLYPHS else []
            else:
                candidates = [(shape, ch) for shape, ch in GLYPHS.items()
                              if all((a & m) == b for a, b, m in zip(shape, bits, masks))]
            if not candidates:
                raise ValueError(f"invalid glyph shape at cell {col},{row} pid {tile['pid']}")
            shape, ch = candidates[0]
            chars.append(ch)
            for yy, value in enumerate(shape):
                for xx in range(5):
                    if value & (1 << (4 - xx)):
                        off = ((gy + yy) * width + gx + xx) * 3
                        expected[off:off + 3] = TERM_FG
            if pixel(data, width, gx, gy + 7) == TERM_CURSOR:
                cursors.append((col, row))
                off = ((gy + 7) * width + gx) * 3
                expected[off:off + 15] = TERM_CURSOR * 5
        decoded.append("".join(chars).rstrip())
    if len(cursors) != 1:
        raise ValueError(f"expected one visible terminal cursor, got {cursors}")
    return decoded


def decode(ppm, metadata):
    width, height, data = parse_ppm(Path(ppm))
    if (width, height) != (800, 600) or (width, height) != (metadata["width"], metadata["height"]):
        raise ValueError("framebuffer is not the requested exact 800x600 mode")
    tiles = metadata["tiles"]
    if metadata["count"] != len(tiles) or not 1 <= len(tiles) <= 3:
        raise ValueError("invalid terminal count")
    for field in ("pid", "surface", "token"):
        if len({t[field] for t in tiles}) != len(tiles) or any(t[field] <= 0 for t in tiles):
            raise ValueError(f"terminal {field} is not independent")
    if sum(t["token"] == metadata["focus"] for t in tiles) != 1:
        raise ValueError("focus is not unique")
    # This keyboard-only path never moves the M31 pointer from the center.
    pointer = {}
    cx, cy = width // 2, height // 2
    for row in range(12):
        for col in range(row // 2 + 1):
            pointer[cx + col, cy + row] = bytes((0xFF, 0xEE, 0xF5)) if col == 0 else bytes((0x30, 0xEE, 0xF5))
    expected = desktop_background(width, height)
    screens = {}
    for tile, rect in zip(tiles, expected_layout(width, height, len(tiles))):
        if tuple(tile[k] for k in ("x", "y", "width", "height", "border")) != rect:
            raise ValueError("terminal geometry differs from the independent M35 layout oracle")
        x, y, w, h, border = rect
        color = TERM_FG if tile["token"] == metadata["focus"] else bytes((0x3C, 0x38, 0x36))
        for row in range(h):
            off = ((y + row) * width + x) * 3
            expected[off:off + w * 3] = color * w if row < border or row >= h - border else (
                color * border + TERM_BG * (w - 2 * border) + color * border)
        screens[tile["pid"]] = decode_tile(data, expected, width, height, tile, pointer)
    for (x, y), color in pointer.items():
        off = (y * width + x) * 3
        expected[off:off + 3] = color
    if data != expected:
        first = next(i for i, (a, b) in enumerate(zip(data, expected)) if a != b)
        index = first // 3
        raise ValueError(f"first pixel divergence at {index % width},{index // width}")
    return screens
