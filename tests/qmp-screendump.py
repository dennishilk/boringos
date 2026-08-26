#!/usr/bin/env python3
import json
import socket
import sys
import time
from pathlib import Path


def die(message: str) -> None:
    print(f"qmp-screendump: {message}", file=sys.stderr)
    raise SystemExit(1)


def read_message(stream):
    while True:
        line = stream.readline()
        if not line:
            die("QMP connection closed")
        try:
            message = json.loads(line.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            continue
        return message


def read_return(stream):
    while True:
        message = read_message(stream)
        if "error" in message:
            die(f"QMP error: {message['error']}")
        if "return" in message:
            return message["return"]


def command(sock, stream, execute, arguments=None):
    payload = {"execute": execute}
    if arguments is not None:
        payload["arguments"] = arguments
    sock.sendall((json.dumps(payload, separators=(",", ":")) + "\r\n").encode("utf-8"))
    return read_return(stream)


def main() -> int:
    if len(sys.argv) != 3:
        die("usage: qmp-screendump.py <qmp-socket> <output.ppm>")

    socket_path = sys.argv[1]
    output = Path(sys.argv[2]).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)

    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    for _ in range(200):
        try:
            sock.connect(socket_path)
            break
        except (FileNotFoundError, ConnectionRefusedError):
            time.sleep(0.05)
    else:
        die("QMP socket did not become ready")

    with sock:
        stream = sock.makefile("rb")
        greeting = read_message(stream)
        if "QMP" not in greeting:
            die("missing QMP greeting")
        command(sock, stream, "qmp_capabilities")
        command(sock, stream, "screendump", {"filename": str(output)})

    if not output.is_file() or output.stat().st_size == 0:
        die("screendump did not create a non-empty PPM")
    print(f"QMP screendump saved: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
