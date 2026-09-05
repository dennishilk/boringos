#!/usr/bin/env python3
import json
import socket
import sys
import time

def recv_message(stream):
    line = stream.readline()
    if not line:
        raise RuntimeError("QMP socket closed")
    return json.loads(line.decode("utf-8"))

def send_command(stream, payload):
    stream.write((json.dumps(payload, separators=(",", ":")) + "\r\n").encode("utf-8"))
    stream.flush()
    while True:
        reply = recv_message(stream)
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

def rel(axis, value):
    return {"type":"rel","data":{"axis":axis,"value":value}}

def button(name, down):
    return {"type":"btn","data":{"down":down,"button":name}}

def inject(stream, events):
    send_command(stream, {"execute":"input-send-event","arguments":{"events":events}})
    time.sleep(0.12)

def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: m65-qmp-mouse.py <qmp-socket>")
    with connect(sys.argv[1]) as sock:
        stream = sock.makefile("rwb", buffering=0)
        if "QMP" not in recv_message(stream):
            raise RuntimeError("missing QMP greeting")
        send_command(stream, {"execute":"qmp_capabilities"})
        inject(stream, [rel("x", 23), rel("y", -17)])
        inject(stream, [button("left", True)])
        inject(stream, [button("left", False)])
    print("m65-qmp-mouse: PASS")

if __name__ == "__main__":
    main()
