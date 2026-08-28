#!/usr/bin/env python3
"""Verify guest-read SMBIOS identity across two real firmware configurations."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/m45-smbios-reference"


def run_vm(name, machine, smbios):
    serial = OUT / f"{name}.serial.log"
    cmd = [os.environ.get("QEMU", "qemu-system-x86_64"), "-M", machine,
           "-cpu", "qemu64,apic=off", "-m", "128M",
           "-cdrom", str(ROOT / "build/boringos.iso"), "-boot", "d",
           "-display", "none", "-serial", "file:" + str(serial),
           "-monitor", "none", "-no-reboot", "-no-shutdown"]
    for value in smbios:
        cmd.extend(("-smbios", value))
    with (OUT / f"{name}.qemu.log").open("w") as qlog:
        vm = subprocess.Popen(cmd, cwd=ROOT, stdout=qlog,
                              stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 60
            while time.monotonic() < deadline:
                text = serial.read_text(errors="replace") if serial.exists() else ""
                if "BoringKernel process/address-space test passed." in text:
                    break
                if vm.poll() is not None:
                    raise RuntimeError(f"{name}: QEMU exited early")
                time.sleep(0.1)
            else:
                raise RuntimeError(f"{name}: normal boot completion timeout")
        finally:
            if vm.poll() is None:
                vm.terminate()
                try:
                    vm.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    vm.kill()
                    vm.wait()

    entry = re.search(
        r"^smbios: entry=(32|64) version=(\d+)\.(\d+) "
        r"structures=(\d+) table_bytes=(\d+)$", text, re.M)
    assert entry is not None
    fields = dict(re.findall(
        r"^smbios: (firmware_vendor|firmware_version|system_manufacturer|"
        r"system_product|board_manufacturer|board_product)=(.*)$", text, re.M))
    assert len(fields) == 6
    memory = re.search(
        r"^smbios: memory_slots=(\d+) memory_present=(\d+) "
        r"memory_bytes=(\d+) memory_info_available=(\d+) "
        r"memory_size_complete=(\d+)$", text, re.M)
    assert memory is not None
    slots, present, memory_bytes, available, complete = map(
        int, memory.groups())
    assert int(entry.group(4)) > 0 and int(entry.group(5)) > 0
    assert slots >= present >= 1
    assert memory_bytes == 128 * 1024 * 1024
    assert available == complete == 1
    assert "smbios: bounded platform identity complete" in text
    return {
        "scenario": name,
        "machine": machine,
        "smbios_arguments": smbios,
        "entry_bits": int(entry.group(1)),
        "version": f"{entry.group(2)}.{entry.group(3)}",
        "structures": int(entry.group(4)),
        "table_bytes": int(entry.group(5)),
        "identity": fields,
        "memory": {
            "slots": slots,
            "present": present,
            "bytes": memory_bytes,
            "size_complete": bool(complete),
        },
        "normal_boot_complete": True,
    }


def run():
    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)
    with (OUT / "build.log").open("w") as log:
        subprocess.run(["make", "TEST_MODE=normal", "all",
                        "smbios-host-test"], cwd=ROOT, stdout=log,
                       stderr=subprocess.STDOUT, check=True)

    default = run_vm("default-q35", "q35", ())
    varied_args = (
        "type=0,vendor=BoringFirmware45,version=BFW-45",
        "type=1,manufacturer=BoringSystems45,product=Platform-45",
        "type=2,manufacturer=BoringBoards45,product=Board-45",
    )
    varied = run_vm("varied-pc-smbios2",
                    "pc,smbios-entry-point-type=32", varied_args)
    expected = {
        "firmware_vendor": "BoringFirmware45",
        "firmware_version": "BFW-45",
        "system_manufacturer": "BoringSystems45",
        "system_product": "Platform-45",
        "board_manufacturer": "BoringBoards45",
        "board_product": "Board-45",
    }
    assert varied["identity"] == expected
    assert default["entry_bits"] == 64 and varied["entry_bits"] == 32
    assert default["identity"]["firmware_vendor"] != expected["firmware_vendor"]
    assert default["identity"]["system_product"] != expected["system_product"]
    assert default["machine"] != varied["machine"]

    (OUT / "guest-platform-identity.json").write_text(
        json.dumps((default, varied), indent=2) + "\n")
    manifest = {
        str(path.relative_to(OUT)): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in sorted(OUT.iterdir()) if path.is_file()
    }
    manifest["../boringos.iso"] = hashlib.sha256(
        (ROOT / "build/boringos.iso").read_bytes()).hexdigest()
    (OUT / "SHA256SUMS.json").write_text(
        json.dumps(manifest, indent=2) + "\n")
    (OUT / "SUCCESS.txt").write_text(
        "M45 actual SMBIOS identity SUCCESS: default q35 and varied pc "
        "firmware tables, bounded parser, complete normal boots.\n")
    print("M45 actual guest SMBIOS platform identity SUCCESS")


if __name__ == "__main__":
    run()
