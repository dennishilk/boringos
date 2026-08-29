#!/usr/bin/env python3
import json
import socket
import sys
import time


def recv_message(sock):
    data = b""
    while b"\r\n" not in data:
        chunk = sock.recv(4096)
        if not chunk:
            raise RuntimeError("QMP socket closed")
        data += chunk
    line, _, rest = data.partition(b"\r\n")
    # QMP replies for this tiny helper are small. If an asynchronous event was
    # coalesced in the same recv, subsequent command handling simply reads the
    # next reply from the socket.
    if rest:
        # The commands below do not depend on asynchronous event contents.
        pass
    return json.loads(line.decode("utf-8"))


def send_command(sock, payload):
    sock.sendall((json.dumps(payload, separators=(",", ":")) + "\r\n").encode("utf-8"))
    while True:
        reply = recv_message(sock)
        if "return" in reply:
            return
        if "error" in reply:
            raise RuntimeError(f"QMP command failed: {reply['error']}")


def connect(path):
    deadline = time.monotonic() + 5.0
    while True:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            sock.connect(path)
            return sock
        except OSError:
            sock.close()
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.05)


def key_event(down):
    return {
        "execute": "input-send-event",
        "arguments": {
            "events": [
                {
                    "type": "key",
                    "data": {
                        "down": down,
                        "key": {"type": "qcode", "data": "a"},
                    },
                }
            ]
        },
    }


def tablet_event():
    return {
        "execute": "input-send-event",
        "arguments": {
            "events": [
                {"type": "abs", "data": {"axis": "x", "value": 12345}},
                {"type": "abs", "data": {"axis": "y", "value": 23456}},
            ]
        },
    }


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: m52-qmp-input.py <qmp-socket>")
    with connect(sys.argv[1]) as sock:
        greeting = recv_message(sock)
        if "QMP" not in greeting:
            raise RuntimeError("missing QMP greeting")
        send_command(sock, {"execute": "qmp_capabilities"})
        send_command(sock, key_event(True))
        time.sleep(0.12)
        send_command(sock, tablet_event())
        time.sleep(0.12)
        send_command(sock, key_event(False))
    print("m52-qmp-input: PASS")


if __name__ == "__main__":
    main()
