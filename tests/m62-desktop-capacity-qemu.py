#!/usr/bin/env python3
"""M62 real-desktop capacity/churn acceptance on a disposable copy of the M61 USB image."""
import json
import os
import re
import select
import shutil
import subprocess
import time
from pathlib import Path
import runpy

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/m62-desktop-capacity"
SOURCE_IMAGE = ROOT / "build/boringos-m61-usb-qemu.img"
STRESS_IMAGE = OUT / "boringos-m62-stress.img"
QMP = runpy.run_path(str(ROOT / "tests/qmp-input.py"))
EXPECTED_BYTES = 100663296


def wait(predicate, description, serial, vm, timeout=120):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        text = serial.read_text(errors="replace")
        if any(marker in text for marker in (
                "Fatal exception", "syscall fatal", "boring-init: FAILED",
                "display: FAILED", "wm: FAILED", "boring-terminal: FAILED",
                "boring-edit FAILED", "boring-files FAILED")):
            raise RuntimeError(f"guest failure while waiting for {description}\n{text[-12000:]}")
        result = predicate(text)
        if result:
            return result
        if vm.poll() is not None:
            raise RuntimeError(f"QEMU exited while waiting for {description}")
        time.sleep(0.05)
    raise RuntimeError(f"timeout waiting for {description}\n{serial.read_text(errors='replace')[-12000:]}")


def frames(text):
    return [(int(a), int(b), int(c)) for a, b, c in
            re.findall(r"wm: frame=(\d+) count=(\d+) focus=(\d+)", text)]


def latest_count(text):
    found = frames(text)
    return found[-1][1] if found else None


def live_regular_processes(text):
    spawned = set(int(pid) for pid in re.findall(
        r"boring-spawn: parent pid \d+ child pid (\d+) task \d+", text))
    reaped = set(int(pid) for pid in re.findall(
        r"boring-spawn: reaped child pid (\d+) task/process cleanup complete", text))
    # boring-init is pid 1 and is not itself created through BORING_SYS_SPAWN.
    return 1 + len(spawned - reaped)


def main():
    if not SOURCE_IMAGE.is_file() or SOURCE_IMAGE.stat().st_size != EXPECTED_BYTES:
        raise RuntimeError("fresh M61 physical USB image missing or wrong size")
    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)
    shutil.copyfile(SOURCE_IMAGE, STRESS_IMAGE)

    code = Path(os.environ.get("OVMF_CODE", "/usr/share/OVMF/OVMF_CODE_4M.fd"))
    variables = Path(os.environ.get("OVMF_VARS", "/usr/share/OVMF/OVMF_VARS_4M.fd"))
    if not code.is_file() or not variables.is_file():
        raise RuntimeError("OVMF firmware unavailable")
    vars_copy = OUT / "OVMF_VARS.fd"
    shutil.copyfile(variables, vars_copy)
    serial = OUT / "serial.log"
    serial.write_text("")
    qemu_log_path = OUT / "qemu.log"
    qemu_log = qemu_log_path.open("w")
    command = [
        os.environ.get("QEMU", "qemu-system-x86_64"),
        "-M", "q35,i8042=off", "-cpu", os.environ.get("QEMU_CPU", "qemu64,apic=off"),
        "-m", "512M",
        "-drive", f"if=pflash,format=raw,unit=0,readonly=on,file={code}",
        "-drive", f"if=pflash,format=raw,unit=1,file={vars_copy}",
        "-drive", f"if=none,id=m62usb,format=raw,file={STRESS_IMAGE},cache=writeback",
        "-device", "qemu-xhci,id=xhci,p3=0",
        "-device", "usb-storage,bus=xhci.0,port=1,drive=m62usb,removable=on,bootindex=1",
        "-device", "usb-kbd,bus=xhci.0,port=2,id=m62kbd",
        "-device", "usb-tablet,bus=xhci.0,port=3,id=m62tablet",
        "-boot", "menu=off,strict=on", "-vga", "std", "-display", "none",
        "-serial", f"file:{serial}", "-monitor", "none", "-qmp", "stdio",
        "-trace", "enable=usb_xhci_slot_address",
        "-trace", "enable=usb_xhci_xfer_start",
        "-trace", "enable=usb_xhci_xfer_success",
        "-no-reboot", "-no-shutdown",
    ]
    (OUT / "qemu-command.json").write_text(json.dumps(command, indent=2) + "\n")
    vm = subprocess.Popen(command, cwd=ROOT, stdin=subprocess.PIPE,
                          stdout=subprocess.PIPE, stderr=qemu_log, bufsize=0)
    qmp_ready = False

    class Pipes:
        def readline(self):
            if not select.select([vm.stdout], [], [], 10)[0]:
                raise RuntimeError("QMP response timeout")
            return vm.stdout.readline()
        def write(self, data):
            return vm.stdin.write(data)
        def flush(self):
            return vm.stdin.flush()

    pipes = Pipes()

    def qmp(name, arguments=None):
        nonlocal qmp_ready
        if not qmp_ready:
            QMP["receive_message"](pipes)
            QMP["execute"](pipes, "qmp_capabilities")
            qmp_ready = True
        return QMP["execute"](pipes, name, arguments)

    def chord(code):
        events = [QMP["key_event"]("meta_l", True),
                  QMP["key_event"](code, True), QMP["key_event"](code, False),
                  QMP["key_event"]("meta_l", False)]
        qmp("input-send-event", {"events": events})
        time.sleep(0.08)

    try:
        wait(lambda text: latest_count(text) == 0,
             "persistent empty desktop", serial, vm)
        initial = serial.read_text(errors="replace")
        if "M61 TRACE [26+] DESKTOP PRESENT" not in initial:
            raise RuntimeError("real M61 desktop present witness missing")

        launches = ["ret", "ret", "ret", "e", "e", "e", "f", "f"]
        for target, code_key in enumerate(launches, start=1):
            chord(code_key)
            wait(lambda text, wanted=target: latest_count(text) == wanted,
                 f"WM client count {target}", serial, vm)

        text = serial.read_text(errors="replace")
        if text.count("boring-terminal: Ring3 managed client + PTY + boring-shell ready") < 3:
            raise RuntimeError("three complete terminal/shell pairs not ready")
        if text.count("boring-spawn: VFS executable source /bin/boring-shell") < 3:
            raise RuntimeError("three boring-shell child processes not proven")
        live_peak = live_regular_processes(text)
        if live_peak <= 8:
            raise RuntimeError(f"desktop live-process peak did not exceed 8: {live_peak}")

        before_frame = frames(text)[-1][0]
        for wanted in range(7, -1, -1):
            chord("q")
            wait(lambda current, count=wanted: latest_count(current) == count,
                 f"close to WM count {wanted}", serial, vm)

        empty_text = serial.read_text(errors="replace")
        forbidden = (
            "wm: session empty; clean exit",
            "display: session drained; exiting with claims",
            "boring-init: desktop session state DRAINING",
        )
        if any(item in empty_text for item in forbidden):
            raise RuntimeError("persistent empty desktop entered historical drain path")

        for target, code_key in enumerate(("ret", "e", "f"), start=1):
            chord(code_key)
            wait(lambda text, wanted=target: latest_count(text) == wanted,
                 f"replacement WM client count {target}", serial, vm)

        final = serial.read_text(errors="replace")
        if frames(final)[-1][0] <= before_frame:
            raise RuntimeError("WM frame progression stopped during churn")
        proof = (
            "REAL_M61_STYLE_DESKTOP_STARTED=YES\n"
            "DESKTOP_LIVE_PROCESS_COUNT_EXCEEDED_8=YES\n"
            f"DESKTOP_LIVE_PROCESS_PEAK={live_peak}\n"
            "THREE_TERMINAL_SHELL_PAIRS=YES\n"
            "DESKTOP_CLIENT_PEAK=8\n"
            "TILE_FOCUS_FRAME_PROGRESS=YES\n"
            "CLOSED_ALL_MANAGED_WINDOWS=YES\n"
            "EMPTY_DESKTOP_PERSISTED=YES\n"
            "TERMINAL_EDIT_FILES_RELAUNCHED=YES\n"
            "DESKTOP_CAPACITY_REUSE=YES\n"
        )
        (OUT / "proof.txt").write_text(proof)
        print(proof, end="")
    finally:
        if vm.poll() is None:
            vm.terminate()
            try:
                vm.wait(timeout=5)
            except subprocess.TimeoutExpired:
                vm.kill()
                vm.wait()
        qemu_log.close()
        STRESS_IMAGE.unlink(missing_ok=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"m62-desktop-capacity: {exc}")
        raise SystemExit(1)
