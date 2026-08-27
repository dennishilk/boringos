#!/usr/bin/env python3
import json
import runpy
from pathlib import Path
import sys


QMP_CONNECTION = runpy.run_path(str(Path(__file__).with_name("qmp-connection.py")))
receive_message = QMP_CONNECTION["receive_message"]
execute = QMP_CONNECTION["execute"]
connect = QMP_CONNECTION["connect"]


def key_event(code, down):
    return {"type": "key", "data": {"down": down,
            "key": {"type": "qcode", "data": code}}}


def button_event(button, down):
    return {"type": "btn", "data": {"down": down, "button": button}}


def rel_event(axis, value):
    return {"type": "rel", "data": {"axis": axis, "value": value}}


def main():
    if len(sys.argv) < 3:
        raise SystemExit("usage: qmp-input.py <socket> <command> [args]")
    socket_path = sys.argv[1]
    operation = sys.argv[2]

    with connect(socket_path) as stream:
        commands = execute(stream, "query-commands")
        if not any(item.get("name") == "input-send-event" for item in commands):
            raise RuntimeError("QMP input-send-event unavailable")

        if operation == "key":
            if len(sys.argv) != 4:
                raise RuntimeError("key requires qcode")
            code = sys.argv[3]
            events = [key_event(code, True), key_event(code, False)]
        elif operation == "super-q":
            events = [key_event("meta_l", True), key_event("q", True),
                      key_event("q", False), key_event("meta_l", False)]
        elif operation in ("super", "super-shift"):
            if len(sys.argv) != 4:
                raise RuntimeError("super chord requires qcode")
            code = sys.argv[3]
            events = [key_event("meta_l", True)]
            if operation == "super-shift":
                events.append(key_event("shift", True))
            events += [key_event(code, True), key_event(code, False)]
            if operation == "super-shift":
                events.append(key_event("shift", False))
            events.append(key_event("meta_l", False))
        elif operation == "super-enter":
            events = [key_event("meta_l", True), key_event("ret", True),
                      key_event("ret", False), key_event("meta_l", False)]
        elif operation == "move":
            if len(sys.argv) != 5:
                raise RuntimeError("move requires dx dy")
            dx = int(sys.argv[3], 10)
            dy = int(sys.argv[4], 10)
            events = [rel_event("x", dx), rel_event("y", dy)]
        elif operation == "button":
            if len(sys.argv) != 5 or sys.argv[4] not in ("down", "up"):
                raise RuntimeError("button requires name down|up")
            events = [button_event(sys.argv[3], sys.argv[4] == "down")]
        else:
            raise RuntimeError(f"unknown operation: {operation}")
        execute(stream, "input-send-event", {"events": events})


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as exc:
        print(f"qmp-input: {exc}", file=sys.stderr)
        raise SystemExit(1)
