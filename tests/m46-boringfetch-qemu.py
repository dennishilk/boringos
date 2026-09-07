#!/usr/bin/env python3
"""M46 real Ring 3 hardware fetch plus the unchanged three-client desktop."""
import json
import re
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/m46-boringfetch-reference"


def value(serial, name):
    match = re.search(rf"^{re.escape(name)}=(.*)$", serial, re.MULTILINE)
    if match is None:
        raise RuntimeError(f"missing real boot inventory value: {name}")
    return match.group(1)


def require_visible(screen, expected):
    if expected not in screen:
        raise RuntimeError(f"real value did not reach framebuffer text: {expected}")


def run():
    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)

    subprocess.run(["make", "boringfetch-host-test", "boringfetch-audit"],
                   cwd=ROOT, check=True)
    subprocess.run(["make", "clean"], cwd=ROOT, check=True)
    OUT.mkdir(parents=True, exist_ok=True)
    subprocess.run(["python3", "tests/m36-desktop-qemu.py", "--mode", "normal"],
                   cwd=ROOT, check=True)
    subprocess.run(["python3", "tests/m42-client-qemu.py"], cwd=ROOT,
                   check=True)

    m36 = ROOT / "build/m36-desktop-reference"
    m42 = ROOT / "build/m42-client-reference"
    screen = (m36 / "boringfetch.txt").read_text()
    serial = (m36 / "serial.log").read_text(errors="replace")

    vendor = value(serial, "cpu-inventory: vendor")
    brand = value(serial, "cpu-inventory: brand")
    family = value(serial, "cpu-inventory: family")
    model = value(serial, "cpu-inventory: model")
    stepping = value(serial, "cpu-inventory: stepping")
    require_visible(screen, f"CPU: {vendor} {brand}")
    require_visible(screen,
                    f"CPU ID: family {family} model {model} stepping {stepping}")

    system_manufacturer = value(serial, "smbios: system_manufacturer")
    system_product = value(serial, "smbios: system_product")
    require_visible(screen,
                    f"Machine: {system_manufacturer} {system_product}")
    firmware_vendor = value(serial, "smbios: firmware_vendor")
    firmware_version = value(serial, "smbios: firmware_version")
    require_visible(screen, f"Firmware: {firmware_vendor} {firmware_version}")
    board_manufacturer = value(serial, "smbios: board_manufacturer")
    board_product = value(serial, "smbios: board_product")
    if board_manufacturer == "unavailable" and board_product == "unavailable":
        if "Board:" in screen:
            raise RuntimeError("unavailable SMBIOS board was invented")
    else:
        require_visible(screen,
                        f"Board: {board_manufacturer} {board_product}")

    summary = re.search(
        r"^pci-inventory: stored=(\d+) total=(\d+) .* complete=(\d+)$",
        serial, re.MULTILINE)
    if summary is None:
        raise RuntimeError("missing bounded PCI inventory summary")
    stored, total, complete = map(int, summary.groups())
    if complete != 1 or stored != total:
        raise RuntimeError("focused QEMU PCI inventory is not complete")
    require_visible(screen, f"PCI: {total} devices")
    entries = re.findall(
        r"^pci-inventory: ([0-9A-F]{2}:[0-9A-F]{2}\.[0-7]) "
        r"id=([0-9A-F]{4}:[0-9A-F]{4}) class=([0-9A-F]{2}:[0-9A-F]{2}) "
        r"prog_if=([0-9A-F]{2})", serial, re.MULTILINE)
    if len(entries) != total:
        raise RuntimeError("PCI entry/summary mismatch")
    for bdf, identity, class_pair, prog_if in entries[:8]:
        require_visible(screen,
                        f"PCI {bdf} {identity} class {class_pair}:{prog_if}")

    memory = re.search(
        r"^smbios: memory_slots=(\d+) memory_present=(\d+) "
        r"memory_bytes=(\d+) .* memory_size_complete=(\d+)$",
        serial, re.MULTILINE)
    if memory is None:
        raise RuntimeError("missing real SMBIOS memory facts")
    slots, present, memory_bytes, memory_complete = map(int, memory.groups())
    if memory_complete != 1:
        raise RuntimeError("focused QEMU SMBIOS memory size is incomplete")
    require_visible(
        screen,
        f"SMBIOS Memory: {memory_bytes // (1024 * 1024)} MiB "
        f"({present}/{slots} devices)")

    framebuffer = re.search(r"^boring-framebuffer: (\d+)x(\d+)x(\d+)$",
                            serial, re.MULTILINE)
    pitch = re.search(r"^boring-framebuffer: pitch (\d+)$", serial,
                      re.MULTILINE)
    if framebuffer is None or pitch is None:
        raise RuntimeError("missing real framebuffer facts")
    width, height, bpp = framebuffer.groups()
    require_visible(screen,
                    f"Display: {width}x{height}x{bpp} pitch {pitch.group(1)}")

    root = ROOT / "build/m36-bundle-test/boringos-root.img"
    root_bytes = root.stat().st_size
    storage_size = (f"{root_bytes // (1024 * 1024)} MiB"
                    if root_bytes >= 1024 * 1024 else
                    f"{root_bytes // 1024} KiB")
    require_visible(screen, f"Storage: vblk0 {storage_size} 1AF4:1042")

    if "M42 real three-client acceptance" not in (m42 / "SUCCESS.txt").read_text():
        raise RuntimeError("BoringEdit/BoringFiles/shortcut regression missing")
    if "boring-spawn: VFS executable source /bin/boringfetch" not in serial:
        raise RuntimeError("graphical command did not execute the Ring 3 ELF")

    for source, destination in (
            (m36 / "boringfetch.ppm", OUT / "boringfetch.ppm"),
            (m36 / "boringfetch.txt", OUT / "boringfetch.txt"),
            (m36 / "serial.log", OUT / "serial.log"),
            (m42 / "normal/three-shortcut-apps.ppm",
             OUT / "three-shortcut-apps.ppm"),
            (m42 / "normal/three-shortcut-apps.txt",
             OUT / "three-shortcut-apps.txt")):
        shutil.copyfile(source, destination)
    (OUT / "facts.json").write_text(json.dumps({
        "cpu": {"vendor": vendor, "brand": brand, "family": family,
                "model": model, "stepping": stepping},
        "pci_devices": total,
        "framebuffer": {"width": int(width), "height": int(height),
                        "bpp": int(bpp), "pitch": int(pitch.group(1))},
        "storage_bytes": root_bytes,
        "ring3": True,
        "shortcuts_and_three_clients": True,
    }, indent=2) + "\n")
    (OUT / "SUCCESS.txt").write_text(
        "M46 real Ring3 boringfetch hardware + graphical three-client regression SUCCESS\n")
    print("M46 real Ring3 hardware framebuffer and three-client acceptance SUCCESS")


if __name__ == "__main__":
    run()
