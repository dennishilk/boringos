#!/usr/bin/env python3
"""M57 writable AHCI BoringFS root, USB-only desktop, and reboot acceptance."""
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

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "build/m57-ahci-persistent-root-reference"
QMP = runpy.run_path(str(ROOT / "tests/qmp-input.py"))
WM = runpy.run_path(str(ROOT / "tests/validate-wm-screenshot.py"))
TERM = runpy.run_path(str(ROOT / "tests/validate-m36-terminal-screenshot.py"))


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_session(name, root_image, expect_existing):
    out = OUT / name
    out.mkdir(parents=True)
    serial = out / "serial.log"
    serial.write_text("")
    iso = ROOT / "build/boringos.iso"
    qemu_log = (out / "qemu.log").open("w")
    vm = subprocess.Popen([
        os.environ.get("QEMU", "qemu-system-x86_64"),
        "-M", "q35,i8042=off", "-cpu", os.environ.get("QEMU_CPU", "qemu64,apic=off"),
        "-m", "256M", "-cdrom", str(iso), "-boot", "d", "-vga", "std",
        "-drive", f"file={root_image},format=raw,if=ide,index=0,media=disk",
        "-device", "qemu-xhci,id=xhci", "-device", "usb-kbd,bus=xhci.0",
        "-device", "usb-tablet,bus=xhci.0", "-display", "none",
        "-serial", f"file:{serial}", "-monitor", "none", "-qmp", "stdio",
        "-no-reboot", "-no-shutdown"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=qemu_log, bufsize=0)
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

    def text():
        return serial.read_text(errors="replace")

    def wait(predicate, description, timeout=90):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            current = text()
            if any(marker in current for marker in (
                    "M37 desktop FAILED", "boring-init: FAILED", "Fatal exception",
                    "syscall fatal", "display: FAILED", "wm: FAILED",
                    "boring-terminal: FAILED")):
                raise RuntimeError(f"guest failure while waiting for {description}\n{current[-12000:]}")
            if vm.poll() is not None:
                raise RuntimeError(f"QEMU exited ({vm.returncode}) while waiting for {description}")
            result = predicate(current)
            if result:
                return result
            time.sleep(0.04)
        raise RuntimeError(f"first missing witness: {description}\n{text()[-12000:]}")

    def witness(marker, count=1):
        return wait(lambda current: current.count(marker) >= count, f"{marker} x{count}")

    def qmp(command, arguments=None):
        nonlocal qmp_ready
        if not qmp_ready:
            QMP["receive_message"](pipes)
            QMP["execute"](pipes, "qmp_capabilities")
            qmp_ready = True
        result = QMP["execute"](pipes, command, arguments)
        with (out / "qmp.log").open("a") as record:
            record.write(json.dumps({"command": command, "arguments": arguments,
                                     "result": result}) + "\n")
        return result

    def key(code, super_key=False):
        events = []
        if super_key:
            events.append(QMP["key_event"]("meta_l", True))
        events.extend((QMP["key_event"](code, True), QMP["key_event"](code, False)))
        if super_key:
            events.append(QMP["key_event"]("meta_l", False))
        qmp("input-send-event", {"events": events})
        time.sleep(0.025)

    def frames():
        return WM["frames"](text())

    def latest(count=None, after=0):
        return wait(lambda _current: next((frame for frame in reversed(frames())
                                           if frame["frame"] > after and
                                           (count is None or frame["count"] == count)), None),
                    f"WM frame count={count} after={after}")

    def shell_for(terminal_pid):
        matches = re.findall(
            rf"boring-spawn: parent pid {terminal_pid} child pid (\d+) task \d+ detached",
            text())
        if len(matches) != 1:
            raise RuntimeError(f"terminal {terminal_pid} does not own exactly one shell")
        return int(matches[0])

    def type_text(value):
        codes = {" ": "spc"}
        for char in value:
            frame = frames()[-1]
            terminal_pid = next(tile["pid"] for tile in frame["tiles"]
                                if tile["token"] == frame["focus"])
            marker = f"fd-read: pid {shell_for(terminal_pid)} fd 0 bytes 1"
            before = text().count(marker)
            key(codes.get(char, char))
            witness(marker, before + 1)

    def command(value):
        type_text(value)
        key("ret")
        return frames()[-1]

    def cat_persist():
        source_count = text().count("boring-spawn: VFS executable source /bin/cat")
        child_count = len(re.findall(
            r"boring-spawn: parent pid \d+ child pid (\d+) task \d+ foreground", text()))
        command_frame = command("cat persist")
        witness("boring-spawn: VFS executable source /bin/cat", source_count + 1)
        child = wait(lambda current: (
            matches[-1] if len(matches := re.findall(
                r"boring-spawn: parent pid \d+ child pid (\d+) task \d+ foreground",
                current)) > child_count else None), "new persisted cat child")
        witness(f"boring-spawn: child pid {child} exited status 0")
        return command_frame

    def capture_persisted(frame):
        geometry = re.search(r"boring-framebuffer: (\d+)x(\d+)x(?:24|32)", text())
        if geometry is None:
            raise RuntimeError("missing framebuffer geometry")
        width, height = map(int, geometry.groups())
        qmp("stop")
        try:
            ppm = out / "persisted-cat.ppm"
            qmp("screendump", {"filename": str(ppm)})
            meta = dict(frame, width=width, height=height)
            (out / "persisted-cat.json").write_text(json.dumps(meta, indent=2) + "\n")
            TERM["decode"].__globals__["MAX_COLS"] = 160
            TERM["decode"].__globals__["MAX_ROWS"] = 96
            screens = TERM["decode"](ppm, meta)
            rows = next(iter(screens.values()))
            if not any(row.strip() == "survived" for row in rows):
                raise ValueError(f"persisted bytes absent from terminal pixels: {rows}")
            (out / "persisted-cat.txt").write_text("\n".join(rows) + "\n")
        finally:
            qmp("cont")

    def settled_capture_persisted(frame):
        deadline = time.monotonic() + 20
        while True:
            try:
                capture_persisted(frame)
                return
            except ValueError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.1)

    try:
        witness("m57-desktop: writable BoringFS root mounted through AHCI sata0")
        witness("m54-desktop: q35 i8042-free xHCI USB keyboard/tablet path online")
        witness("boring-spawn: VFS executable source /bin/boring-display")
        witness("boring-spawn: VFS executable source /bin/boringwm")
        key("ret", super_key=True)
        witness("wm: Super+Return spawned /bin/boring-terminal")
        witness("boring-spawn: VFS executable source /bin/boring-terminal")
        witness("boring-spawn: VFS executable source /bin/boring-shell")
        latest(1)

        if expect_existing:
            cat_frame = cat_persist()
            settled_capture_persisted(cat_frame)
            # Closeout requires a completed write/flush. This identical rewrite
            # happens only after the reboot read and pixel proof above.
            command("write persist survived")
        else:
            command("touch persist")
            command("write persist survived")
            cat_frame = cat_persist()
            settled_capture_persisted(cat_frame)

        key("q", super_key=True)
        witness("boring-terminal: graceful cleanup complete")
        witness("boring-init: desktop session drained")
        witness("m37-desktop: IPC/input/framebuffer/M32/PTY desktop resources drained")
        witness("m37-desktop: all spawned desktop tasks/processes reaped; PID 1 remains")
        witness("m57-desktop: AHCI writes/flushes=")
        witness("M57 writable AHCI persistent-root desktop passed.")
        witness("M54 USB-only graphical desktop acceptance passed.")
        (out / "SUCCESS.txt").write_text(f"M57 {name} AHCI persistent-root desktop SUCCESS\n")
    except Exception as exc:
        if vm.poll() is None:
            try:
                qmp("stop")
                qmp("screendump", {"filename": str(out / "failure.ppm")})
            except Exception as diagnostic_error:
                (out / "diagnostic-error.txt").write_text(str(diagnostic_error))
        (out / "FAILURE.txt").write_text(str(exc) + "\n")
        raise
    finally:
        if vm.poll() is None:
            vm.terminate()
            try:
                vm.wait(timeout=5)
            except subprocess.TimeoutExpired:
                vm.kill()
                vm.wait()
        qemu_log.close()


def run():
    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)
    with (OUT / "build.log").open("w") as log:
        subprocess.run(["sh", "tests/m57-build.sh"], cwd=ROOT,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    root_image = ROOT / "build/m57-bundle/boringos-root.img"
    initial_hash = sha256(root_image)
    run_session("first-boot", root_image, False)
    after_first = sha256(root_image)
    if initial_hash == after_first:
        raise RuntimeError("first boot did not mutate the AHCI root image")

    checker = str(ROOT / "build/boringfsck")
    checked = subprocess.check_output([checker, str(root_image)])
    (OUT / "boringfsck-after-first.txt").write_bytes(checked)
    if b"Status: VALID" not in checked:
        raise RuntimeError("host BoringFS validation failed after first boot")
    persisted = subprocess.check_output([checker, "--cat", "/persist", str(root_image)])
    (OUT / "persist.exact-bytes").write_bytes(persisted)
    if persisted != b"survived\n":
        raise RuntimeError(f"wrong persisted bytes: {persisted!r}")

    run_session("second-boot", root_image, True)
    after_second = sha256(root_image)
    if after_first != after_second:
        raise RuntimeError("identical second-boot rewrite changed the root image")
    checked = subprocess.check_output([checker, str(root_image)])
    (OUT / "boringfsck-after-second.txt").write_bytes(checked)
    if b"Status: VALID" not in checked:
        raise RuntimeError("host BoringFS validation failed after reboot")
    (OUT / "image-hashes.json").write_text(json.dumps({
        "iso_sha256": sha256(ROOT / "build/boringos.iso"),
        "root_initial_sha256": initial_hash,
        "root_after_first_sha256": after_first,
        "root_after_second_sha256": after_second,
    }, indent=2) + "\n")
    (OUT / "SUCCESS.txt").write_text(
        "M57 writable AHCI persistent-root USB-only desktop SUCCESS\n"
        "Userland mutation, host fsck/exact bytes, reboot read pixels, and clean drain passed.\n")
    print(f"M57 AHCI persistent-root desktop passed; evidence: {OUT}")


if __name__ == "__main__":
    run()
