#!/usr/bin/env python3
"""Prove the diagnostic pre-IDT fault path halts instead of resetting."""
import json
import os
import runpy
import select
import shutil
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/m61-early-fault"
IMAGE = ROOT / "build/boringos-m61-usb-qemu.img"
QMP = runpy.run_path(str(ROOT / "tests/qmp-input.py"))


def firmware_files():
    code = Path(os.environ.get("OVMF_CODE",
                               "/usr/share/OVMF/OVMF_CODE_4M.fd"))
    variables = Path(os.environ.get("OVMF_VARS",
                                    "/usr/share/OVMF/OVMF_VARS_4M.fd"))
    if not code.is_file() or not variables.is_file():
        raise RuntimeError(
            "OVMF_CODE_4M.fd / OVMF_VARS_4M.fd unavailable")
    return code, variables


def run():
    if not IMAGE.is_file():
        raise RuntimeError("controlled early-fault USB test twin is missing")
    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)
    serial = OUT / "serial.log"
    serial.write_text("")
    qemu_log_path = OUT / "qemu.log"
    code, variables = firmware_files()
    vars_copy = OUT / "OVMF_VARS.fd"
    shutil.copyfile(variables, vars_copy)
    command = [
        os.environ.get("QEMU", "qemu-system-x86_64"),
        "-M", "q35,i8042=off",
        "-cpu", os.environ.get("QEMU_CPU", "qemu64,apic=off"),
        "-m", "512M",
        "-drive", f"if=pflash,format=raw,unit=0,readonly=on,file={code}",
        "-drive", f"if=pflash,format=raw,unit=1,file={vars_copy}",
        "-drive", f"if=none,id=m61usb,format=raw,readonly=on,file={IMAGE}",
        "-device", "qemu-xhci,id=xhci,p3=0",
        "-device", "usb-storage,bus=xhci.0,port=1,drive=m61usb,removable=on,bootindex=1",
        "-device", "usb-kbd,bus=xhci.0,port=2,id=m61kbd",
        "-device", "usb-tablet,bus=xhci.0,port=3,id=m61tablet",
        "-boot", "menu=off,strict=on",
        "-vga", "std", "-display", "none", "-serial", f"file:{serial}",
        "-monitor", "none", "-qmp", "stdio",
        "-no-reboot", "-no-shutdown",
    ]
    (OUT / "qemu-command.json").write_text(
        json.dumps(command, indent=2) + "\n")
    if any(item in command for item in ("-cdrom", "-kernel")):
        raise RuntimeError("early-fault topology contains an alternate boot path")

    with qemu_log_path.open("w") as qemu_log:
        vm = subprocess.Popen(
            command, cwd=ROOT, stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=qemu_log, bufsize=0)
        qmp_ready = False

        class Pipes:
            def readline(self):
                if not select.select([vm.stdout], [], [], 10)[0]:
                    raise RuntimeError("QMP pipe response timeout")
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
            result = QMP["execute"](pipes, name, arguments)
            with (OUT / "qmp.log").open("a") as record:
                record.write(json.dumps({
                    "command": name,
                    "arguments": arguments,
                    "result": result,
                }) + "\n")
            return result

        def wait_for(marker, timeout=120):
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                current = serial.read_text(errors="replace")
                if marker in current:
                    return current
                if vm.poll() is not None:
                    raise RuntimeError(
                        f"QEMU exited ({vm.returncode}) before {marker}\n"
                        f"{current[-8000:]}")
                time.sleep(0.04)
            raise RuntimeError(
                f"missing early-fault witness: {marker}\n"
                f"{serial.read_text(errors='replace')[-8000:]}")

        try:
            wait_for("M61 CONTROLLED EARLY FAULT ARMED vector=6")
            wait_for(
                "M61 EARLY FAULT CONTAINED vector=6 "
                "framebuffer_write_active=no")
            first_status = qmp("query-status")
            time.sleep(1.5)
            if vm.poll() is not None:
                raise RuntimeError(
                    "QEMU exited after the contained early fault")
            second_status = qmp("query-status")
            current = serial.read_text(errors="replace")
            if (first_status.get("status") != "running" or
                    second_status.get("status") != "running"):
                raise RuntimeError(
                    "contained guest is not stably halted in running VM: "
                    f"{first_status!r} -> {second_status!r}")
            if (current.count(
                    "M61 CONTROLLED EARLY FAULT ARMED vector=6") != 1 or
                    current.count(
                        "M61 EARLY FAULT CONTAINED vector=6 "
                        "framebuffer_write_active=no") != 1):
                raise RuntimeError(
                    "controlled fault marker repeated; reset is possible")
            if ("BoringOS booting..." in current or
                    "M61 FRAMEBUFFER COUNT=" in current):
                raise RuntimeError(
                    "guest progressed beyond the controlled pre-IDT halt")
            (OUT / "proof.txt").write_text(
                "EARLY_FAULT_CONTAINMENT_PROVEN=YES\n"
                "fault_vector=6\n"
                "fault_point=before framebuffer access and exception_init\n"
                "framebuffer_write_active=no\n"
                f"qmp_status_first={first_status['status']}\n"
                f"qmp_status_second={second_status['status']}\n"
                "qemu_process_alive_after_1_5_seconds=yes\n"
                "guest_reset_observed=no\n")
            print("M61 controlled early-fault containment passed")
        finally:
            if vm.poll() is None:
                vm.terminate()
                try:
                    vm.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    vm.kill()
                    vm.wait()


if __name__ == "__main__":
    try:
        run()
    except (OSError, RuntimeError, subprocess.SubprocessError) as exc:
        OUT.mkdir(parents=True, exist_ok=True)
        (OUT / "FAILURE.txt").write_text(str(exc) + "\n")
        print(f"m61-early-fault-qemu: {exc}")
        raise SystemExit(1)
