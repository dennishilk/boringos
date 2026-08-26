#!/usr/bin/env python3
import re
import sys
from pathlib import Path

BACKGROUND = (0x0B, 0x11, 0x18)
CLIENT_A = (0xD8, 0x78, 0x30)
CLIENT_A_UPDATED = (0x40, 0xE0, 0x40)
CLIENT_B = (0x28, 0x68, 0xB8)
CURSOR_EDGE = (0xFF, 0xEE, 0xF5)
CURSOR_FILL = (0x30, 0xEE, 0xF5)


def die(message: str) -> None:
    print(f"display-screenshot: {message}", file=sys.stderr)
    raise SystemExit(1)


def read_token(data: bytes, offset: int) -> tuple[bytes, int]:
    length = len(data)
    while offset < length:
        byte = data[offset]
        if byte in b" \t\r\n":
            offset += 1
            continue
        if byte == ord("#"):
            newline = data.find(b"\n", offset)
            if newline < 0:
                die("unterminated PPM comment")
            offset = newline + 1
            continue
        break
    start = offset
    while offset < length and data[offset] not in b" \t\r\n#":
        offset += 1
    if start == offset:
        die("missing PPM header token")
    return data[start:offset], offset


def parse_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    magic, offset = read_token(data, 0)
    width_token, offset = read_token(data, offset)
    height_token, offset = read_token(data, offset)
    max_token, offset = read_token(data, offset)
    if magic != b"P6":
        die(f"expected binary P6 PPM, got {magic!r}")
    try:
        width = int(width_token, 10)
        height = int(height_token, 10)
        maximum = int(max_token, 10)
    except ValueError as exc:
        die(f"invalid numeric PPM header: {exc}")
    if width <= 0 or height <= 0 or maximum != 255:
        die(f"invalid PPM geometry/maxval: {width}x{height} max={maximum}")
    if offset >= len(data) or data[offset] not in b" \t\r\n":
        die("PPM header is not terminated by whitespace")
    if data[offset:offset + 2] == b"\r\n":
        offset += 2
    else:
        offset += 1
    pixels = data[offset:]
    expected = width * height * 3
    if len(pixels) != expected:
        die(f"PPM payload size mismatch: got {len(pixels)}, expected {expected}")
    return width, height, pixels


def pixel(pixels: bytes, width: int, height: int, x: int, y: int) -> tuple[int, int, int]:
    if x < 0 or y < 0 or x >= width or y >= height:
        die(f"sample outside framebuffer: ({x},{y}) for {width}x{height}")
    offset = (y * width + x) * 3
    return tuple(pixels[offset:offset + 3])


def require_pixel(pixels: bytes, width: int, height: int,
                  x: int, y: int, expected: tuple[int, int, int], name: str) -> None:
    actual = pixel(pixels, width, height, x, y)
    if actual != expected:
        die(f"{name} mismatch at ({x},{y}): got {actual}, expected {expected}")


def main() -> int:
    if len(sys.argv) != 3:
        die("usage: validate-display-screenshot.py <reference.ppm> <serial.log>")
    ppm_path = Path(sys.argv[1])
    log_path = Path(sys.argv[2])
    if not ppm_path.is_file() or not log_path.is_file():
        die("reference PPM or serial log is missing")

    log = log_path.read_text(encoding="utf-8", errors="replace")
    geometry = re.search(r"^boring-framebuffer: ([0-9]+)x([0-9]+)x(?:24|32)$", log, re.MULTILINE)
    if geometry is None:
        die("serial framebuffer geometry witness is missing")
    serial_width = int(geometry.group(1), 10)
    serial_height = int(geometry.group(2), 10)

    width, height, pixels = parse_ppm(ppm_path)
    if (width, height) != (serial_width, serial_height):
        die(f"PPM geometry {width}x{height} != serial geometry {serial_width}x{serial_height}")
    if width <= 260 or height <= 220:
        die(f"framebuffer too small for deterministic M34 witness: {width}x{height}")

    require_pixel(pixels, width, height, 10, 10, BACKGROUND, "dark background")
    require_pixel(pixels, width, height, 80, 80, CLIENT_A_UPDATED,
                  "Client A same-backing update")
    require_pixel(pixels, width, height, 81, 80, CLIENT_A,
                  "Client A retained surface content")
    require_pixel(pixels, width, height, 100, 100, CLIENT_A,
                  "Client A surface interior")
    require_pixel(pixels, width, height, 180, 140, CLIENT_B,
                  "Client B surface origin")
    require_pixel(pixels, width, height, 200, 160, CLIENT_B,
                  "Client B surface interior")
    require_pixel(pixels, width, height, 40, 30, CURSOR_EDGE,
                  "software cursor anchor")
    require_pixel(pixels, width, height, 41, 34, CURSOR_FILL,
                  "software cursor fill")

    counts = {
        BACKGROUND: 0,
        CLIENT_A: 0,
        CLIENT_A_UPDATED: 0,
        CLIENT_B: 0,
        CURSOR_EDGE: 0,
        CURSOR_FILL: 0,
    }
    for offset in range(0, len(pixels), 3):
        value = tuple(pixels[offset:offset + 3])
        if value in counts:
            counts[value] += 1

    if counts[BACKGROUND] < (width * height) // 2:
        die("dark desktop does not dominate framebuffer")
    if counts[CLIENT_A] < 4000:
        die(f"Client A region incomplete: only {counts[CLIENT_A]} expected-color pixels")
    if counts[CLIENT_A_UPDATED] < 1:
        die("live Client A updated pixel is absent")
    if counts[CLIENT_B] < 6300:
        die(f"Client B region incomplete: only {counts[CLIENT_B]} expected-color pixels")
    if counts[CURSOR_EDGE] < 10 or counts[CURSOR_FILL] < 20:
        die("software cursor shape is incomplete")

    required = [
        "boring-display: live shared-buffer COMMIT passed",
        "boring-display: deterministic stacking passed",
        "boring-display: cursor clipped top-left and presented",
        "boring-display: cursor clipped bottom-right and presented",
        "boring-display: visual witness ready cursor=40,30 surfaces=2",
        "boring-display: framebuffer present witness complete",
    ]
    for witness in required:
        if witness not in log:
            die(f"serial witness missing: {witness}")
    if "display-client-a: exiting without destroy" in log:
        die("Client A exited before the stable visual witness was captured")

    print(f"M34 display framebuffer reference validated: {width}x{height}")
    print(f"background pixels: {counts[BACKGROUND]}")
    print(f"Client A pixels: {counts[CLIENT_A]} updated={counts[CLIENT_A_UPDATED]}")
    print(f"Client B pixels: {counts[CLIENT_B]}")
    print(f"cursor pixels: edge={counts[CURSOR_EDGE]} fill={counts[CURSOR_FILL]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
