#!/usr/bin/env python3
"""Retain the full real VirtIO I/O acceptance; correlate its PCI BDF/identity."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/m44-pci-reference"


def run():
    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)
    subprocess.run(["make", "pci-inventory-host-test"], cwd=ROOT, check=True)
    log = OUT / "virtio-acceptance.log"
    with log.open("w") as output:
        subprocess.run(["sh", "tests/virtio-block-qemu.sh"], cwd=ROOT,
                       stdout=output, stderr=subprocess.STDOUT, check=True)
    text = log.read_text()
    pattern = r"^pci-inventory: ([0-9A-F]{2}:[0-9A-F]{2}\.[0-7]) id=([0-9A-F]{4}):([0-9A-F]{4}) class=([0-9A-F]{2}):([0-9A-F]{2}) prog_if=([0-9A-F]{2}) revision=([0-9A-F]{2}) header=([0-9A-F]{2})$"
    devices = [dict(zip(("bdf", "vendor", "device", "class", "subclass", "prog_if", "revision", "header"), values))
               for values in re.findall(pattern, text, re.M)]
    summary = re.search(r"^pci-inventory: stored=(\d+) total=(\d+) config_reads=(\d+) truncated=(\d+) complete=(\d+)$", text, re.M)
    assert summary is not None and len(devices) >= 4
    stored, total, reads, truncated, complete = map(int, summary.groups())
    assert stored == total == len(devices) and truncated == 0 and complete == 1
    assert 8192 <= reads <= 196608
    assert len({d["bdf"] for d in devices}) == len(devices)
    driver_bdf = re.search(r"^  BDF: ([0-9A-F]{2}:[0-9A-F]{2}\.[0-7])$", text, re.M).group(1)
    matches = [d for d in devices if d["bdf"] == driver_bdf]
    assert len(matches) == 1 and matches[0]["vendor"] == "1AF4" and matches[0]["device"] == "1042"
    assert matches[0]["class"] == "01" and matches[0]["subclass"] == "00"
    # Actual chipset multifunction devices, not just a host fixture.
    secondary = [d for d in devices if not d["bdf"].endswith(".0")]
    assert secondary
    for d in secondary:
        zero = next(x for x in devices if x["bdf"] == d["bdf"][:-1] + "0")
        assert int(zero["header"], 16) & 0x80
    for witness in ("BoringKernel VirtIO block test passed.", "  persisted-write: PASS",
                    "  left-neighbor: PASS", "  right-neighbor: PASS",
                    "BoringKernel VirtIO block QEMU verification passed."):
        assert witness in text
    (OUT / "guest-pci-inventory.json").write_text(json.dumps({"devices": devices,
        "stored": stored, "total": total, "config_reads": reads, "driver_bdf": driver_bdf,
        "virtio_real_io_and_persistence": True}, indent=2) + "\n")
    manifest = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in OUT.iterdir() if p.is_file()}
    manifest["../boringos.iso"] = hashlib.sha256((ROOT / "build/boringos.iso").read_bytes()).hexdigest()
    (OUT / "SHA256SUMS.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (OUT / "SUCCESS.txt").write_text("M44 actual PCI inventory, multifunction and real VirtIO identity/I/O/persistence correlation SUCCESS\n")
    print("M44 actual PCI inventory and VirtIO correlation SUCCESS")


if __name__ == "__main__":
    run()
