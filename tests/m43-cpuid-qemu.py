#!/usr/bin/env python3
"""Read real guest CPUID diagnostics; no host model substitutes for a guest."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/m43-cpuid-reference"


def run():
    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)
    with (OUT / "build.log").open("w") as log:
        subprocess.run(["make", "TEST_MODE=normal", "all", "cpu-inventory-host-test"],
                       cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    results = []
    for name, cpu, smp in (("default", "qemu64,apic=off", "1"),
                           ("varied", "qemu64,apic=off,vendor=BoringCPU123,family=15,model=42,stepping=7", "1"),
                           ("smp-inventory-only", "qemu64,apic=off", "4")):
        log = OUT / (name + ".serial.log")
        cmd = [os.environ.get("QEMU", "qemu-system-x86_64"), "-M", "q35", "-cpu", cpu,
               "-smp", smp, "-m", "128M", "-cdrom", str(ROOT / "build/boringos.iso"),
               "-boot", "d", "-display", "none", "-serial", "file:" + str(log),
               "-monitor", "none", "-no-reboot", "-no-shutdown"]
        with (OUT / (name + ".qemu.log")).open("w") as qlog:
            vm = subprocess.Popen(cmd, cwd=ROOT, stdout=qlog, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 30
                while time.monotonic() < deadline:
                    text = log.read_text(errors="replace") if log.exists() else ""
                    witness = ("cpu-inventory: boot CPU CPUID collection complete; advertised features only"
                               if name == "smp-inventory-only" else "BoringKernel process/address-space test passed.")
                    if witness in text:
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
        fields = dict(line.removeprefix("cpu-inventory: ").split("=", 1)
                      for line in text.splitlines() if line.startswith("cpu-inventory: ") and "=" in line)
        assert len(fields["vendor"]) == 12 and fields["brand"].strip() != "unavailable"
        values = {k: int(v, 0) for k, v in fields.items() if k not in ("vendor", "brand")}
        assert all(values[k] == 1 for k in ("leaf1_valid", "ext1_valid", "brand_valid"))
        sig = values["signature"]
        base = (sig >> 8) & 15
        assert values["family"] == base + (((sig >> 20) & 255) if base == 15 else 0)
        assert values["model"] == ((sig >> 4) & 15) + ((((sig >> 16) & 15) << 4) if base in (6, 15) else 0)
        assert values["stepping"] == (sig & 15)
        assert values["logical_per_package_max"] == (((values["leaf1_ebx"] >> 16) & 255) if values["leaf1_edx"] & (1 << 28) else 1)
        assert values["ext1_edx"] & (1 << 29)  # Actual long-mode advertisement.
        if name == "varied":
            assert fields["vendor"] == "BoringCPU123"
            assert (values["family"], values["model"], values["stepping"]) == (15, 42, 7)
        if name == "smp-inventory-only":
            assert values["logical_per_package_max"] == 4
        results.append({"scenario": name, "cpu_argument": cpu, "smp_argument": smp,
                        "normal_boot_required": name != "smp-inventory-only", "inventory": fields})
    assert results[0]["inventory"]["vendor"] != results[1]["inventory"]["vendor"]
    (OUT / "guest-inventory.json").write_text(json.dumps(results, indent=2) + "\n")
    manifest = {str(p.relative_to(OUT)): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in sorted(OUT.iterdir()) if p.is_file()}
    manifest["../boringos.iso"] = hashlib.sha256((ROOT / "build/boringos.iso").read_bytes()).hexdigest()
    (OUT / "SHA256SUMS.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (OUT / "SUCCESS.txt").write_text("M43 CPUID inventory SUCCESS: two single-CPU normal boots, plus four-CPU inventory only (no SMP runtime claim).\n")
    print("M43 actual guest CPUID inventory SUCCESS")


if __name__ == "__main__":
    run()
