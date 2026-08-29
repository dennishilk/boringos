#!/usr/bin/env python3
"""Boot the exact M59 hybrid image as read-only USB mass storage under OVMF."""
import os
import shutil
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
IMAGE = BUILD / "boringos-m59-cthulhu-smoke.img"
SERIAL = BUILD / "m59-usb-image-ovmf.log"
QEMU_LOG = BUILD / "m59-usb-image-ovmf-qemu.log"
VARS_COPY = BUILD / "m59-usb-image-ovmf-vars.fd"


def firmware_files():
    code = Path(os.environ.get("OVMF_CODE", "/usr/share/OVMF/OVMF_CODE_4M.fd"))
    variables = Path(os.environ.get("OVMF_VARS", "/usr/share/OVMF/OVMF_VARS_4M.fd"))
    if not code.is_file() or not variables.is_file():
        raise RuntimeError("OVMF_CODE_4M.fd / OVMF_VARS_4M.fd unavailable")
    return code, variables


def require(text, marker):
    if marker not in text:
        raise RuntimeError(f"missing M59 USB-image witness: {marker!r}")


def run():
    if not IMAGE.is_file():
        raise RuntimeError(f"missing image: {IMAGE}")
    code, variables = firmware_files()
    shutil.copyfile(variables, VARS_COPY)
    SERIAL.write_text("")
    command = [
        os.environ.get("QEMU", "qemu-system-x86_64"),
        "-M", "q35,i8042=off", "-cpu", "qemu64,apic=off", "-m", "512M",
        "-drive", f"if=pflash,format=raw,unit=0,readonly=on,file={code}",
        "-drive", f"if=pflash,format=raw,unit=1,file={VARS_COPY}",
        "-drive", f"if=none,id=bootstick,format=raw,readonly=on,file={IMAGE}",
        "-device", "qemu-xhci,id=xhci",
        "-device", "usb-storage,drive=bootstick,bus=xhci.0,removable=on",
        "-device", "usb-kbd,bus=xhci.0",
        "-device", "usb-tablet,bus=xhci.0",
        "-boot", "menu=off,strict=on",
        "-vga", "std", "-display", "none",
        "-serial", f"file:{SERIAL}", "-monitor", "none",
        "-no-reboot", "-no-shutdown",
    ]
    with QEMU_LOG.open("w") as qemu_log:
        vm = subprocess.Popen(command, cwd=ROOT, stdout=qemu_log,
                              stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 90.0
            text = ""
            while time.monotonic() < deadline:
                text = SERIAL.read_text(errors="replace")
                if "PHYSICAL SMOKE READY" in text:
                    break
                if "M59 PHYSICAL SMOKE FAILED:" in text:
                    raise RuntimeError("guest reported M59 physical-smoke failure")
                if vm.poll() is not None:
                    raise RuntimeError("QEMU exited before PHYSICAL SMOKE READY")
                time.sleep(0.1)
            else:
                raise RuntimeError("timeout before PHYSICAL SMOKE READY")
        finally:
            if vm.poll() is None:
                vm.terminate()
                try:
                    vm.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    vm.kill()
                    vm.wait()

    for marker in (
        "Kernel: BoringKernel 0.0.59-dev",
        "Arch: x86_64",
        "PCI enumeration: COMPLETE",
        "Framebuffer: READY",
        "xHCI: READY",
        "USB addressed devices: 3",
        "USB addressing truncated: NO",
        "USB descriptors: READY",
        "USB keyboard: DETECTED",
        "USB pointer: DETECTED",
        "USB HID transport: DETECTION ONLY (mixed non-HID devices present; M59 does not own storage)",
        "Storage writes: DISABLED",
        "PHYSICAL SMOKE READY",
        "M59 PHYSICAL SMOKE HARNESS: bounded diagnostic completion",
    ):
        require(text, marker)
    if "VirtIO block:" in text or "AHCI:" in text:
        raise RuntimeError("M59 USB artifact unexpectedly entered a root-storage backend")
    print("M59 UEFI USB IMAGE BOOT PASSED")


if __name__ == "__main__":
    run()
