#!/usr/bin/env python3
"""M47 OVMF proof and controlled demonstrations of the first HW gaps."""
import hashlib
import json
import os
import re
import shutil
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/m47-ovmf-readiness"
WORK = ROOT / "build/m47-ovmf-work"


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def firmware_files():
    code = Path(os.environ.get("OVMF_CODE", "/usr/share/OVMF/OVMF_CODE_4M.fd"))
    variables = Path(os.environ.get("OVMF_VARS", "/usr/share/OVMF/OVMF_VARS_4M.fd"))
    if not code.is_file() or not variables.is_file():
        raise RuntimeError("OVMF_CODE_4M.fd / OVMF_VARS_4M.fd unavailable")
    return code, variables


def run_vm(name, iso, marker, *, memory="128M", machine="q35",
           root_mode=None):
    code, variables = firmware_files()
    serial = OUT / f"{name}.serial.log"
    qemu_log = OUT / f"{name}.qemu.log"
    vars_copy = WORK / f"{name}.vars.fd"
    shutil.copyfile(variables, vars_copy)
    serial.write_text("")
    command = [
        os.environ.get("QEMU", "qemu-system-x86_64"),
        "-M", machine, "-cpu", "qemu64,apic=off", "-m", memory,
        "-drive", f"if=pflash,format=raw,unit=0,readonly=on,file={code}",
        "-drive", f"if=pflash,format=raw,unit=1,file={vars_copy}",
        "-cdrom", str(iso), "-boot", "d", "-vga", "std",
        "-display", "none", "-serial", f"file:{serial}",
        "-monitor", "none", "-no-reboot", "-no-shutdown",
    ]
    if root_mode is not None:
        source = ROOT / "build/m40-bundle/boringos-root.img"
        root = WORK / f"{name}.root.img"
        shutil.copyfile(source, root)
        command.extend(("-drive", f"file={root},if=none,format=raw,id=rootdisk"))
        if root_mode in ("virtio", "xhci"):
            command.extend(("-device",
                            "virtio-blk-pci,drive=rootdisk,disable-legacy=on"))
        elif root_mode == "ahci":
            command.extend(("-device", "ide-hd,drive=rootdisk,bus=ide.0"))
        else:
            raise RuntimeError(f"unknown root mode: {root_mode}")
    if root_mode == "xhci":
        command.extend(("-device", "qemu-xhci,id=xhci",
                        "-device", "usb-kbd,bus=xhci.0",
                        "-device", "usb-mouse,bus=xhci.0"))

    with qemu_log.open("w") as log:
        vm = subprocess.Popen(command, cwd=ROOT, stdout=log,
                              stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 90
            while time.monotonic() < deadline:
                text = serial.read_text(errors="replace")
                if marker in text:
                    break
                if vm.poll() is not None:
                    raise RuntimeError(f"{name}: QEMU exited before {marker!r}")
                time.sleep(0.1)
            else:
                raise RuntimeError(f"{name}: timeout before {marker!r}")
        finally:
            if vm.poll() is None:
                vm.terminate()
                try:
                    vm.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    vm.kill()
                    vm.wait()
    return text, {
        "scenario": name,
        "machine": machine,
        "memory": memory,
        "root_mode": root_mode,
        "expected_marker": marker,
        "iso_sha256": sha256(iso),
        "ovmf_code_sha256": sha256(code),
        "ovmf_vars_template_sha256": sha256(variables),
    }


def source_audit():
    boot = (ROOT / "kernel/include/boring/boot_protocol.h").read_text()
    entry = (ROOT / "kernel/core/entry.c").read_text()
    pci = (ROOT / "kernel/arch/x86_64/pci.c").read_text()
    irq = (ROOT / "kernel/arch/x86_64/irq.c").read_text()
    timer = (ROOT / "kernel/arch/x86_64/timer.c").read_text()
    input_source = (ROOT / "kernel/arch/x86_64/i8042.c").read_text()
    makefile = (ROOT / "Makefile").read_text()

    assert "RSDP_REQUEST" not in boot and "rsdp" not in entry.lower()
    assert "PCI_CONFIG_ADDRESS_PORT 0x0cf8U" in pci
    assert "PIC_MASTER_COMMAND = 0x20" in irq
    assert "PIT_CHANNEL0_DATA = 0x40" in timer
    assert "I8042_DATA_PORT 0x60U" in input_source
    lowered = makefile.lower()
    assert "kernel/drivers/xhci" not in lowered
    assert "kernel/drivers/nvme" not in lowered
    assert "kernel/core/ahci_block.c" in lowered
    assert "kernel/drivers/ahci_block.c" in lowered
    return {
        "acpi_rsdp_request": False,
        "pci_config": "segment 0 CF8/CFC, first 256 bytes",
        "interrupts": "8259 PIC + PIT channel 0",
        "input_driver": "i8042 PS/2 only",
        "storage_driver": "VirtIO block + bounded read-only AHCI SATA",
    }


def run():
    if OUT.exists():
        shutil.rmtree(OUT)
    if WORK.exists():
        shutil.rmtree(WORK)
    subprocess.run(["make", "clean"], cwd=ROOT, check=True)
    OUT.mkdir(parents=True)
    WORK.mkdir(parents=True)

    with (OUT / "normal-build.log").open("w") as log:
        subprocess.run(["make", "TEST_MODE=normal", "all",
                        "pmm-readiness-host-test"], cwd=ROOT, stdout=log,
                       stderr=subprocess.STDOUT, check=True)
    normal_iso = WORK / "normal.iso"
    shutil.copyfile(ROOT / "build/boringos.iso", normal_iso)
    high, high_meta = run_vm(
        "ovmf-high-memory", normal_iso,
        "BoringKernel process/address-space test passed.", memory="5G")
    for witness in (
            "firmware_vendor=Ubuntu distribution of EDK II",
            "boring-framebuffer: detected",
            "pci-inventory: stored=",
            "smbios: bounded platform identity complete",
            "Memory map capped: yes", "PMM: online",
            "Controller: 8259 PIC", "Timer source: PIT IRQ0"):
        if witness not in high:
            raise RuntimeError(f"high-memory OVMF witness missing: {witness}")
    usable = re.search(r"^Usable memory: (\d+) bytes$", high, re.MULTILINE)
    if usable is None or int(usable.group(1)) != 4 * 1024 * 1024 * 1024:
        raise RuntimeError("PMM did not report its exact safe 4 GiB cap")

    with (OUT / "desktop-build.log").open("w") as log:
        subprocess.run(["sh", "tests/m40-build.sh"], cwd=ROOT, stdout=log,
                       stderr=subprocess.STDOUT, check=True)
    desktop_iso = ROOT / "build/boringos.iso"
    legacy, legacy_meta = run_vm(
        "ovmf-legacy-assisted-desktop", desktop_iso,
        "boring-init: desktop session state RUNNING", root_mode="virtio")
    for witness in (
            "firmware_vendor=Ubuntu distribution of EDK II",
            "m38-desktop: real PS/2 keyboard and mouse path online",
            "m38-desktop: real BoringFS root mounted",
            "boring-framebuffer: detected"):
        if witness not in legacy:
            raise RuntimeError(f"legacy-assisted OVMF witness missing: {witness}")

    xhci, xhci_meta = run_vm(
        "ovmf-xhci-only-input", desktop_iso,
        "M38 desktop FAILED: input hardware", machine="q35,i8042=off",
        root_mode="xhci")
    if ("class=0C:03 prog_if=30" not in xhci or
            "real PS/2 keyboard and mouse path online" in xhci):
        raise RuntimeError("xHCI-only boundary was not isolated honestly")

    ahci, ahci_meta = run_vm(
        "ovmf-ahci-only-root", desktop_iso,
        "M38 desktop FAILED: persistent root", root_mode="ahci")
    if ("id=8086:2922 class=01:06" not in ahci or
            "real PS/2 keyboard and mouse path online" not in ahci):
        raise RuntimeError("AHCI-only root boundary was not isolated honestly")

    evidence = {
        "proof_level": "REAL-HARDWARE-READY CANDIDATE; NOT PHYSICALLY VERIFIED",
        "firmware": "OVMF/UEFI QEMU only",
        "source_audit": source_audit(),
        "scenarios": [high_meta, legacy_meta, xhci_meta, ahci_meta],
        "managed_memory_cap_bytes": 4 * 1024 * 1024 * 1024,
        "first_interactive_blocker": "xHCI/USB HID input is not implemented",
        "independent_storage_blocker": "writable AHCI/NVMe root storage is not implemented",
        "m48_selection": "smallest correct xHCI/USB HID foundation",
    }
    (OUT / "readiness-evidence.json").write_text(
        json.dumps(evidence, indent=2) + "\n")
    manifest = {
        str(path.relative_to(OUT)): sha256(path)
        for path in sorted(OUT.iterdir()) if path.is_file()
    }
    (OUT / "SHA256SUMS.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (OUT / "SUCCESS.txt").write_text(
        "M47 OVMF readiness and controlled xHCI/AHCI boundary proof SUCCESS\n"
        "Candidate only; no physical-machine verification claim.\n")
    print("M47 OVMF readiness matrix evidence SUCCESS; not physically verified")


if __name__ == "__main__":
    run()
