#!/usr/bin/env python3
import re
import sys
from collections import Counter
from pathlib import Path

PALETTE = {
    "background": (0x08, 0x0C, 0x10),
    "panel": (0x0E, 0x15, 0x1B),
    "secondary": (0x11, 0x1B, 0x22),
    "grid": (0x18, 0x28, 0x31),
    "cyan": (0x3A, 0xCD, 0xDC),
    "text": (0xE8, 0xEF, 0xF2),
    "success": (0x72, 0xD6, 0x8A),
    "amber": (0xE4, 0xB8, 0x68),
}


def die(message: str) -> None:
    print(f"framebuffer-screenshot: FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def ppm_tokens(data: bytes):
    index = 0
    length = len(data)
    while index < length:
        while index < length and chr(data[index]).isspace():
            index += 1
        if index >= length:
            return
        if data[index] == ord("#"):
            while index < length and data[index] not in (10, 13):
                index += 1
            continue
        start = index
        while index < length and not chr(data[index]).isspace():
            index += 1
        yield data[start:index], index


def parse_ppm(path: Path):
    data = path.read_bytes()
    tokens = ppm_tokens(data)
    try:
        magic, _ = next(tokens)
        width_token, _ = next(tokens)
        height_token, _ = next(tokens)
        max_token, header_end = next(tokens)
    except StopIteration:
        die("truncated PPM header")
    if magic != b"P6":
        die(f"unexpected PPM magic {magic!r}")
    try:
        width = int(width_token)
        height = int(height_token)
        maximum = int(max_token)
    except ValueError:
        die("non-numeric PPM geometry")
    if width <= 0 or height <= 0 or maximum != 255:
        die("invalid PPM geometry/max value")
    pixel_start = header_end
    if pixel_start >= len(data):
        die("missing PPM pixel separator")
    if data[pixel_start:pixel_start + 2] == b"\r\n":
        pixel_start += 2
    elif data[pixel_start] in b" \t\r\n":
        pixel_start += 1
    else:
        die("missing PPM pixel separator")
    pixels = data[pixel_start:]
    expected = width * height * 3
    if len(pixels) != expected:
        die(f"pixel byte count {len(pixels)} != expected {expected}")
    return width, height, pixels


def pixel_at(pixels: bytes, width: int, x: int, y: int):
    offset = (y * width + x) * 3
    return tuple(pixels[offset:offset + 3])


def main() -> int:
    if len(sys.argv) != 3:
        die("usage: validate-framebuffer-screenshot.py <image.ppm> <serial.log>")
    image_path = Path(sys.argv[1])
    serial_path = Path(sys.argv[2])
    if not image_path.is_file():
        die("screenshot is missing")
    if not serial_path.is_file():
        die("serial log is missing")

    width, height, pixels = parse_ppm(image_path)
    if width < 800 or height < 600:
        die(f"framebuffer is smaller than M30 reference floor: {width}x{height}")
    if not any(pixels):
        die("screenshot is all black")

    serial = serial_path.read_text(encoding="utf-8", errors="replace")
    match = re.search(r"^boring-framebuffer: ([0-9]+)x([0-9]+)x([0-9]+)$", serial, re.MULTILINE)
    if not match:
        die("serial framebuffer geometry marker is missing")
    serial_width, serial_height, serial_bpp = map(int, match.groups())
    if (width, height) != (serial_width, serial_height):
        die(f"PPM {width}x{height} != guest {serial_width}x{serial_height}")
    if serial_bpp not in (24, 32):
        die(f"unexpected guest bpp {serial_bpp}")

    colors = Counter(tuple(pixels[i:i + 3]) for i in range(0, len(pixels), 3))
    minimums = {
        "background": max(1000, width * height // 20),
        "panel": max(1000, width * height // 12),
        "secondary": 1500,
        "cyan": 500,
        "text": 250,
        "success": 80,
    }
    for name, minimum in minimums.items():
        count = colors[PALETTE[name]]
        if count < minimum:
            die(f"palette color {name} count {count} < {minimum}")

    if pixel_at(pixels, width, 8, 8) != PALETTE["background"]:
        die("background sample does not match BoringOS palette")
    if pixel_at(pixels, width, 30, 90) != PALETTE["panel"]:
        die("main panel sample does not match BoringOS palette")
    if pixel_at(pixels, width, width // 2, 63) != PALETTE["cyan"]:
        die("top cyan accent sample is missing")
    if pixel_at(pixels, width, 52, 108) != PALETTE["cyan"]:
        die("BoringOS mark sample is missing")

    required_serial = (
        "boring-framebuffer: rgb validated",
        "boring-graphics: primitives online",
        "boring-graphics: pixel font online",
        "boring-graphics: dashboard rendered",
        "boring-shell ready.",
    )
    for marker in required_serial:
        if marker not in serial:
            die(f"missing serial marker: {marker}")

    print(
        "Framebuffer screenshot validated: "
        f"{width}x{height}x{serial_bpp}; "
        + ", ".join(f"{name}={colors[color]}" for name, color in PALETTE.items())
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
