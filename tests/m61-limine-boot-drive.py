#!/usr/bin/env python3
"""Drive the selected Limine entry in the headless M61 QEMU acceptance only."""
import json
import re
import socket
import sys
import time
from pathlib import Path

BOOT_MARKER = 'BdsDxe: starting Boot0001'
KERNEL_PATTERN = re.compile(r'BoringKernel 0\.0\.\d+-dev')
TEMP_KBD_SLOT_PATTERN = re.compile(
    r'usb_xhci_slot_address\s+slotid \d+, port (?:4|\S*\.4)(?:\s|$)')
CONNECT_TIMEOUT = 30.0
BOOT_DRIVE_TIMEOUT = 12.0
KEY_HOLD_MILLISECONDS = 120
ENTER_INTERVAL = 0.4


def read_text(path):
    try:
        return path.read_text(errors='replace')
    except FileNotFoundError:
        return ''


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


def main():
    if len(sys.argv) != 3:
        raise RuntimeError('usage: m61-limine-boot-drive.py <qmp-socket> <serial-log>')
    qmp_path = Path(sys.argv[1])
    serial_path = Path(sys.argv[2])
    trace_path = serial_path.with_name('qemu.log')
    log_path = serial_path.with_name('limine-boot-drive.log')

    client = None
    stream = None
    keyboard_attached = True
    attempts = 0
    try:
        client, stream = connect_qmp(qmp_path)
        wait_for_text(trace_path, lambda text: TEMP_KBD_SLOT_PATTERN.search(text) is not None,
                      CONNECT_TIMEOUT, 'firmware-addressed temporary Limine USB keyboard')
        wait_for_text(serial_path, lambda text: BOOT_MARKER in text,
                      CONNECT_TIMEOUT, 'OVMF Boot0001 start')
        with log_path.open('w') as record:
            record.write('temporary Limine USB keyboard pre-attached on xHCI port 4\n')
            record.write('firmware addressed temporary Limine USB keyboard before Boot0001 drive\n')
            record.write('OVMF Boot0001 reached; driving selected Limine entry explicitly\n')

        deadline = time.monotonic() + BOOT_DRIVE_TIMEOUT
        while time.monotonic() < deadline:
            current = read_text(serial_path)
            if KERNEL_PATTERN.search(current):
                break
            execute(stream, 'send-key', {
                'keys': [{'type': 'qcode', 'data': 'ret'}],
                'hold-time': KEY_HOLD_MILLISECONDS,
            })
            attempts += 1
            with log_path.open('a') as record:
                record.write(
                    f'explicit Limine Return attempt {attempts}: QMP send-key hold '
                    f'{KEY_HOLD_MILLISECONDS}ms\n')
            time.sleep(ENTER_INTERVAL)
        else:
            raise RuntimeError('explicit Limine Return did not reach BoringKernel serial witness')

        execute(stream, 'device_del', {'id': 'm61liminekbd'})
        keyboard_attached = False
        time.sleep(0.1)
        with log_path.open('a') as record:
            record.write(f'BoringKernel serial witness reached after {attempts} Return attempt(s)\n')
            record.write('temporary Limine USB keyboard removed before runtime HID acceptance\n')
            record.write('M61 Limine explicit-entry QEMU boot drive: PASS\n')
    except Exception as exc:
        with log_path.open('a') as record:
            record.write(f'FAIL: {exc}\n')
        raise
    finally:
        if keyboard_attached and stream is not None:
            try:
                execute(stream, 'device_del', {'id': 'm61liminekbd'})
            except Exception:
                pass
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
