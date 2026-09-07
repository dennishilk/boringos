#!/usr/bin/env python3
"""Capture a no-input temporal proof of the selected Limine M61 entry."""
import hashlib
import json
import re
import socket
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

BOOT_MARKER = 'BdsDxe: starting Boot0001'
KERNEL_PATTERN = re.compile(r'BoringKernel 0\.0\.\d+-dev')
CONNECT_TIMEOUT = 30.0
BOOT_DRIVE_TIMEOUT = 12.0
MENU_FIRST_VISIBLE_DELAY_SECONDS = 0.75
CAPTURE_OFFSETS_SECONDS = (0.0, 2.0, 4.0, 7.0)


def read_text(path):
    try:
        return path.read_text(errors='replace')
    except FileNotFoundError:
        return ''


def timestamp():
    return datetime.now(timezone.utc).isoformat(timespec='milliseconds')


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def receive_message(stream):
    while True:
        line = stream.readline()
        if not line:
            raise RuntimeError('QMP connection closed')
        message = json.loads(line.decode('utf-8'))
        if 'event' in message:
            continue
        return message


def execute(stream, command, arguments=None):
    payload = {'execute': command}
    if arguments is not None:
        payload['arguments'] = arguments
    stream.write((json.dumps(payload, separators=(',', ':')) + '\n').encode('utf-8'))
    stream.flush()
    while True:
        message = receive_message(stream)
        if 'QMP' in message:
            continue
        if 'error' in message:
            raise RuntimeError(f"QMP {command} failed: {message['error']}")
        if 'return' in message:
            return message['return']


def connect_qmp(path):
    deadline = time.monotonic() + CONNECT_TIMEOUT
    while time.monotonic() < deadline:
        client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        client.settimeout(2.0)
        try:
            client.connect(str(path))
            stream = client.makefile('rwb', buffering=0)
            greeting = receive_message(stream)
            if 'QMP' not in greeting:
                raise RuntimeError('missing QMP greeting')
            execute(stream, 'qmp_capabilities')
            return client, stream
        except (FileNotFoundError, ConnectionRefusedError, socket.timeout, RuntimeError):
            client.close()
            time.sleep(0.05)
    raise RuntimeError('secondary QMP socket did not become ready')


def wait_for_text(path, predicate, timeout, description):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        current = read_text(path)
        if predicate(current):
            return current
        time.sleep(0.05)
    raise RuntimeError(f'timed out waiting for {description}')


def record_line(log_path, text):
    with log_path.open('a') as record:
        record.write(f'{timestamp()} {text}\n')


def main():
    if len(sys.argv) != 3:
        raise RuntimeError('usage: m61-limine-boot-drive.py <qmp-socket> <serial-log>')
    qmp_path = Path(sys.argv[1])
    serial_path = Path(sys.argv[2])
    log_path = serial_path.with_name('limine-boot-drive.log')

    client = None
    stream = None
    try:
        client, stream = connect_qmp(qmp_path)
        wait_for_text(serial_path, lambda text: BOOT_MARKER in text,
                      CONNECT_TIMEOUT, 'OVMF Boot0001 start')
        log_path.write_text('')
        record_line(log_path, 'Boot0001 observed')
        record_line(log_path, 'no-input temporal proof: no keyboard, mouse, input-send-event, or send-key')

        time.sleep(MENU_FIRST_VISIBLE_DELAY_SECONDS)
        t0 = time.monotonic()
        first_kernel_seen = False
        for index, target_offset in enumerate(CAPTURE_OFFSETS_SECONDS):
            while True:
                elapsed = time.monotonic() - t0
                if elapsed >= target_offset:
                    break
                if not first_kernel_seen and KERNEL_PATTERN.search(read_text(serial_path)) is not None:
                    first_kernel_seen = True
                    record_line(log_path, f'first kernel witness: YES at T+{elapsed:.3f}s')
                time.sleep(min(0.02, target_offset - elapsed))

            elapsed = time.monotonic() - t0
            capture_path = serial_path.with_name(
                f'limine-noinput-t{int(target_offset)}.ppm').resolve()
            result = execute(stream, 'screendump', {'filename': str(capture_path)})
            digest = sha256(capture_path)
            record_line(log_path,
                        f'capture {index}: target=T+{target_offset:.0f}s actual=T+{elapsed:.3f}s '
                        f'file={capture_path.name} sha256={digest} '
                        f'QMP result={json.dumps(result, sort_keys=True)}')
            if not first_kernel_seen and KERNEL_PATTERN.search(read_text(serial_path)) is not None:
                first_kernel_seen = True
                record_line(log_path, f'first kernel witness: YES at T+{time.monotonic() - t0:.3f}s')

        if not first_kernel_seen:
            deadline = t0 + BOOT_DRIVE_TIMEOUT
            while time.monotonic() < deadline:
                if KERNEL_PATTERN.search(read_text(serial_path)) is not None:
                    first_kernel_seen = True
                    record_line(log_path, f'first kernel witness: YES at T+{time.monotonic() - t0:.3f}s')
                    break
                time.sleep(0.05)

        if not first_kernel_seen:
            record_line(log_path, f'first kernel witness: NO through T+{BOOT_DRIVE_TIMEOUT:.0f}s')
            raise RuntimeError('no-input timeout:5 did not reach BoringKernel serial witness')

        record_line(log_path, 'M61 Limine no-input temporal proof reached BoringKernel: PASS')
    except Exception as exc:
        record_line(log_path, f'FAIL: {exc}')
        raise
    finally:
        if stream is not None:
            stream.close()
        if client is not None:
            client.close()


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as exc:
        print(f'm61-limine-boot-drive: {exc}', file=sys.stderr)
        raise SystemExit(1)
