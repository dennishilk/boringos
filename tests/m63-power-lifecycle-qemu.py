#!/usr/bin/env python3
import hashlib
import json
import os
import re
import runpy
import select
import shutil
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/m63-system-power-lifecycle"
SOURCE = ROOT / "build/boringos-m61-usb-qemu.img"
CHECKER = ROOT / "build/boringfsck"
QMP = runpy.run_path(str(ROOT / "tests/qmp-input.py"))
SECTOR = 512
ROOT_FIRST_LBA = 133120
ROOT_SECTORS = 32768
IMAGE_SECTORS = 196608
ROOT_START = ROOT_FIRST_LBA * SECTOR
ROOT_LENGTH = ROOT_SECTORS * SECTOR
IMAGE_LENGTH = IMAGE_SECTORS * SECTOR

def wait_file(path, predicate, description, vm, timeout=150):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        text = path.read_text(errors="replace")
        result = predicate(text)
        if result:
            return result
        if vm.poll() is not None:
            raise RuntimeError(
                f"QEMU exited ({vm.returncode}) while waiting for {description}\n{text[-16000:]}")
        time.sleep(0.04)
    raise RuntimeError(f"timeout waiting for {description}\n{path.read_text(errors='replace')[-16000:]}")

def extract_root(image, destination):
    with image.open("rb") as source, destination.open("wb") as target:
        source.seek(ROOT_START)
        remaining = ROOT_LENGTH
        while remaining:
            chunk = source.read(min(1024 * 1024, remaining))
            if not chunk:
                raise RuntimeError("short M63 image while extracting BoringFS root")
            target.write(chunk)
            remaining -= len(chunk)

def verify_persisted(image, relative_path, expected, label):
    root = OUT / f"root-{label}.img"
    extract_root(image, root)
    checked = subprocess.check_output([str(CHECKER), str(root)])
    if b"Status: VALID" not in checked:
        raise RuntimeError(f"BoringFS invalid after {label}")
    actual = subprocess.check_output(
        [str(CHECKER), "--cat", "/persist/" + relative_path, str(root)])
    if actual != expected:
        raise RuntimeError(f"wrong persisted data after {label}: {actual!r}")

class Session:
    def __init__(self, name, image):
        self.name = name
        self.image = image
        self.out = OUT / name
        self.out.mkdir(parents=True, exist_ok=True)
        self.serial = self.out / "serial.log"
        self.serial.write_text("")
        self.qemu_log_path = self.out / "qemu.log"
        self.qemu_log = self.qemu_log_path.open("w")
        code = Path(os.environ.get("OVMF_CODE", "/usr/share/OVMF/OVMF_CODE_4M.fd"))
        variables = Path(os.environ.get("OVMF_VARS", "/usr/share/OVMF/OVMF_VARS_4M.fd"))
        if not code.is_file() or not variables.is_file():
            raise RuntimeError("OVMF firmware unavailable")
        vars_copy = self.out / "OVMF_VARS.fd"
        shutil.copyfile(variables, vars_copy)
        self.command = [
            os.environ.get("QEMU", "qemu-system-x86_64"),
            "-M", "q35,i8042=off",
            "-cpu", os.environ.get("QEMU_CPU", "qemu64,apic=off"),
            "-m", "512M",
            "-drive", f"if=pflash,format=raw,unit=0,readonly=on,file={code}",
            "-drive", f"if=pflash,format=raw,unit=1,file={vars_copy}",
            "-drive", f"if=none,id=m63usb,format=raw,file={image},cache=writeback",
            "-device", "qemu-xhci,id=xhci,p3=0",
            "-device", "usb-storage,bus=xhci.0,port=1,drive=m63usb,removable=on,bootindex=1",
            "-device", "usb-kbd,bus=xhci.0,port=2,id=m63kbd",
            "-device", "usb-tablet,bus=xhci.0,port=3,id=m63tablet",
            "-boot", "menu=off,strict=on",
            "-vga", "std", "-display", "none",
            "-serial", f"file:{self.serial}",
            "-monitor", "none", "-qmp", "stdio",
        ]
        (self.out / "qemu-command.json").write_text(json.dumps(self.command, indent=2) + "\n")
        if "-no-reboot" in self.command or "-no-shutdown" in self.command:
            raise RuntimeError("M63 real power acceptance accidentally suppresses hardware transitions")
        self.vm = subprocess.Popen(
            self.command, cwd=ROOT, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=self.qemu_log, bufsize=0)
        self.qmp_ready = False

    class Pipes:
        def __init__(self, vm):
            self.vm = vm
        def readline(self):
            if not select.select([self.vm.stdout], [], [], 10)[0]:
                raise RuntimeError("QMP response timeout")
            return self.vm.stdout.readline()
        def write(self, data):
            return self.vm.stdin.write(data)
        def flush(self):
            return self.vm.stdin.flush()

    def text(self):
        return self.serial.read_text(errors="replace")

    def qmp(self, name, arguments=None):
        pipes = self.Pipes(self.vm)
        if not self.qmp_ready:
            QMP["receive_message"](pipes)
            QMP["execute"](pipes, "qmp_capabilities")
            self.qmp_ready = True
        return QMP["execute"](pipes, name, arguments)

    def wait(self, predicate, description, timeout=150):
        return wait_file(self.serial, predicate, description, self.vm, timeout)

    def boot_desktop(self, generation=1):
        self.wait(lambda t: t.count("BoringOS booting...") >= generation,
                  f"boot generation {generation}")
        self.wait(lambda t: t.count("M61 TRACE [26+] DESKTOP PRESENT") >= generation,
                  f"desktop generation {generation}")

    def open_shell(self):
        before = self.text().count(
            "boring-terminal: Ring3 managed client + PTY + boring-shell ready")
        events = [
            QMP["key_event"]("meta_l", True),
            QMP["key_event"]("ret", True),
            QMP["key_event"]("ret", False),
            QMP["key_event"]("meta_l", False),
        ]
        self.qmp("input-send-event", {"events": events})
        self.wait(lambda t: t.count(
            "boring-terminal: Ring3 managed client + PTY + boring-shell ready") > before,
            "terminal/shell ready")
        matches = re.findall(
            r"boring-spawn: parent pid \d+ child pid (\d+) task \d+ detached",
            self.text())
        if not matches:
            raise RuntimeError("unable to resolve shell pid")
        return int(matches[-1])

    def type_text(self, shell_pid, value):
        keymap = {" ": "spc", "-": "minus", ".": "dot", "/": "slash"}
        marker = f"fd-read: pid {shell_pid} fd 0 bytes 1"
        for character in value:
            code = keymap.get(character, character.lower())
            before = self.text().count(marker)
            self.qmp("input-send-event", {"events": [
                QMP["key_event"](code, True), QMP["key_event"](code, False)
            ]})
            self.wait(lambda t, wanted=before + 1: t.count(marker) >= wanted,
                      f"shell input {character!r}")

    def submit(self, shell_pid, expect_prompt):
        read_marker = f"fd-read: pid {shell_pid} fd 0 bytes 1"
        write_pattern = re.compile(rf"fd-write: pid {shell_pid} fd 1 bytes \d+")
        before_read = self.text().count(read_marker)
        before_write = len(write_pattern.findall(self.text()))
        self.qmp("input-send-event", {"events": [
            QMP["key_event"]("ret", True), QMP["key_event"]("ret", False)
        ]})
        self.wait(lambda t: t.count(read_marker) >= before_read + 1,
                  "shell Return input")
        if expect_prompt:
            self.wait(lambda t: len(write_pattern.findall(t)) >= before_write + 2,
                      "shell regenerated prompt")

    def command_line(self, shell_pid, command):
        self.type_text(shell_pid, command)
        self.submit(shell_pid, True)

    def close(self):
        if self.vm.poll() is None:
            self.vm.terminate()
            try:
                self.vm.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.vm.kill()
                self.vm.wait()
        self.qemu_log.close()

def create_persistent_file(session, shell_pid, filename, payload):
    session.command_line(shell_pid, "mkdir persist")
    session.command_line(shell_pid, "cd persist")
    session.command_line(shell_pid, "touch " + filename)
    session.command_line(shell_pid, "write " + filename + " " + payload)

def require_order(text, markers, label):
    positions = [text.find(marker) for marker in markers]
    if any(position < 0 for position in positions) or positions != sorted(positions):
        raise RuntimeError(f"{label} witness order invalid: {positions!r}")

def reboot_test(image):
    session = Session("reboot", image)
    try:
        session.boot_desktop(1)
        shell_pid = session.open_shell()
        create_persistent_file(session, shell_pid, "m63-reboot.txt", "reboot-safe")
        session.type_text(shell_pid, "reboot")
        session.submit(shell_pid, False)
        session.wait(lambda t: "M63_REBOOT_HW_ENTER" in t, "reboot hardware entry")
        session.boot_desktop(2)
        text = session.text()
        require_order(text, (
            "M63_REBOOT_REQUESTED",
            "M63_NEW_SPAWN_GATE_ACTIVE",
            "M63_STORAGE_SYNC_BEGIN",
            "M63_STORAGE_SYNC_OK",
            "M63_ACPI_TABLES_OK",
            "M63_REBOOT_HW_ENTER",
        ), "reboot")
        if text.count("BoringOS booting...") < 2:
            raise RuntimeError("process respawn was mistaken for machine reset")
    finally:
        session.close()
    verify_persisted(image, "m63-reboot.txt", b"reboot-safe\n", "reboot")

def shutdown_test(image):
    session = Session("shutdown", image)
    try:
        session.boot_desktop(1)
        shell_pid = session.open_shell()
        create_persistent_file(session, shell_pid, "m63-shutdown.txt", "shutdown-safe")
        session.type_text(shell_pid, "shutdown")
        session.submit(shell_pid, False)
        session.wait(lambda t: "M63_POWEROFF_HW_ENTER" in t, "poweroff hardware entry")
        deadline = time.monotonic() + 30
        while (session.vm.poll() is None) and (time.monotonic() < deadline):
            time.sleep(0.05)
        if session.vm.poll() is None:
            raise RuntimeError("guest ACPI S5 did not power QEMU off")
        text = session.text()
        require_order(text, (
            "M63_SHUTDOWN_REQUESTED",
            "M63_NEW_SPAWN_GATE_ACTIVE",
            "M63_STORAGE_SYNC_BEGIN",
            "M63_STORAGE_SYNC_OK",
            "M63_ACPI_TABLES_OK",
            "M63_POWEROFF_HW_ENTER",
        ), "shutdown")
    finally:
        session.close()
    verify_persisted(image, "m63-shutdown.txt", b"shutdown-safe\n", "shutdown")

    cold = Session("shutdown-cold-boot", image)
    try:
        cold.boot_desktop(1)
    finally:
        cold.close()
    verify_persisted(image, "m63-shutdown.txt", b"shutdown-safe\n", "shutdown-cold-boot")

def main():
    if not SOURCE.is_file() or SOURCE.stat().st_size != IMAGE_LENGTH:
        raise RuntimeError("fresh M61-style M63 QEMU twin missing or wrong size")
    if not CHECKER.is_file():
        raise RuntimeError("boringfsck missing")
    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)
    reboot_image = OUT / "m63-reboot.img"
    shutdown_image = OUT / "m63-shutdown.img"
    shutil.copyfile(SOURCE, reboot_image)
    shutil.copyfile(SOURCE, shutdown_image)

    reboot_test(reboot_image)
    shutdown_test(shutdown_image)

    proof = (
        "QEMU_REAL_REBOOT=YES\n"
        "QEMU_SECOND_BOOT_OBSERVED=YES\n"
        "QEMU_REAL_POWEROFF=YES\n"
        "PERSISTENCE_ACROSS_REBOOT=YES\n"
        "PERSISTENCE_ACROSS_SHUTDOWN=YES\n"
        "PRODUCTION_QEMU_MAGIC_PORT=NO\n"
        "POWER_TEST_IMAGES_PUBLISHABLE=NO\n"
    )
    (OUT / "proof.txt").write_text(proof)
    print(proof, end="")
    reboot_image.unlink(missing_ok=True)
    shutdown_image.unlink(missing_ok=True)

if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError, ValueError) as exc:
        print("m63-power-lifecycle:", exc)
        raise SystemExit(1)
