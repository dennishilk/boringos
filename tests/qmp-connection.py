#!/usr/bin/env python3
"""QMP transport for the same acceptance commands over Unix sockets or FIFOs."""
from contextlib import contextmanager, ExitStack
import fcntl
import json
from pathlib import Path
import select
import socket
import stat
import time


def receive_message(stream):
    while True:
        line = stream.readline()
        if not line:
            raise RuntimeError("QMP connection closed")
        message = json.loads(line.decode("utf-8"))
        if "return" in message or "error" in message or "QMP" in message:
            return message


def execute(stream, command, arguments=None):
    payload = {"execute": command}
    if arguments is not None:
        payload["arguments"] = arguments
    stream.write((json.dumps(payload, separators=(",", ":")) + "\n").encode("utf-8"))
    stream.flush()
    while True:
        message = receive_message(stream)
        if "QMP" in message:
            continue
        if "error" in message:
            raise RuntimeError(f"QMP {command} failed: {message['error']}")
        return message.get("return")


class PipeStream:
    def __init__(self, reader, writer):
        self.reader, self.writer = reader, writer

    def readline(self):
        if not select.select([self.reader], [], [], 10)[0]:
            raise RuntimeError("QMP pipe response timeout")
        return self.reader.readline()

    def write(self, data): return self.writer.write(data)
    def flush(self): return self.writer.flush()


@contextmanager
def connect(path):
    path = str(path)
    pipe_in, pipe_out = Path(path + ".in"), Path(path + ".out")
    ready = Path(path + ".ready")
    with ExitStack() as stack:
        if pipe_in.exists() and pipe_out.exists():
            if not all(stat.S_ISFIFO(p.stat().st_mode) for p in (pipe_in, pipe_out)):
                raise RuntimeError("QMP pipe endpoints must be FIFOs")
            lock = stack.enter_context(open(path + ".lock", "a"))
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            # O_RDWR avoids open-time deadlocks; only the indicated direction
            # is used. One QEMU pipe session persists across helper processes.
            reader = stack.enter_context(pipe_out.open("r+b", buffering=0))
            writer = stack.enter_context(pipe_in.open("r+b", buffering=0))
            stream = PipeStream(reader, writer)
            fresh = not ready.exists()
        else:
            client = stack.enter_context(socket.socket(socket.AF_UNIX, socket.SOCK_STREAM))
            client.settimeout(10)
            for _ in range(200):
                try:
                    client.connect(path)
                    break
                except (FileNotFoundError, ConnectionRefusedError):
                    time.sleep(0.05)
            else:
                raise RuntimeError("QMP socket did not become ready")
            stream = stack.enter_context(client.makefile("rwb", buffering=0))
            fresh = True
        if fresh:
            if "QMP" not in receive_message(stream):
                raise RuntimeError("missing QMP greeting")
            execute(stream, "qmp_capabilities")
            if isinstance(stream, PipeStream):
                ready.write_text("QMP capabilities negotiated\n")
        yield stream
