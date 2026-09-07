#!/usr/bin/env python3
"""M38 PID1 bounded desktop-session supervision real QEMU acceptance."""
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
OUT = ROOT / "build/m38-desktop-reference"
QMP = runpy.run_path(str(ROOT / "tests/qmp-input.py"))
WM = runpy.run_path(str(ROOT / "tests/validate-wm-screenshot.py"))
TERM = runpy.run_path(str(ROOT / "tests/validate-m36-terminal-screenshot.py"))


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_scenario(name, root_image):
    out = OUT / name
    out.mkdir(parents=True, exist_ok=True)
    serial = out / "serial.log"
    serial.write_text("")
    iso = ROOT / "build/boringos.iso"
    (out / "image-hashes.json").write_text(json.dumps({
        "scenario": name,
        "iso_sha256": sha256(iso),
        "root_sha256": sha256(root_image),
    }, indent=2) + "\n")

    qemu_log = (out / "qemu.log").open("w")
    vm = subprocess.Popen([
        os.environ.get("QEMU", "qemu-system-x86_64"),
        "-M", "q35", "-cpu", os.environ.get("QEMU_CPU", "qemu64,apic=off"),
        "-m", "128M", "-cdrom", str(iso), "-boot", "d", "-vga", "std",
        "-drive", f"file={root_image},if=none,format=raw,id=boringdisk,readonly=on",
        "-device", "virtio-blk-pci,drive=boringdisk,disable-legacy=on",
        "-display", "none", "-serial", f"file:{serial}", "-monitor", "none",
        "-qmp", "stdio", "-no-reboot", "-no-shutdown"],
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
            fatal = ("M38 desktop FAILED", "boring-init: FAILED",
                     "Fatal exception", "syscall fatal")
            if any(marker in current for marker in fatal):
                raise RuntimeError(
                    f"guest failure while waiting for {description}\n{current[-12000:]}")
            if name == "normal" and any(marker in current for marker in (
                    "display: FAILED", "wm: FAILED", "M36 terminal FAILED")):
                raise RuntimeError(
                    f"unexpected component failure while waiting for {description}\n"
                    f"{current[-12000:]}")
            if vm.poll() is not None:
                raise RuntimeError(
                    f"QEMU exited ({vm.returncode}) while waiting for {description}")
            result = predicate(current)
            if result:
                return result
            time.sleep(0.04)
        raise RuntimeError(f"first missing witness: {description}\n{text()[-12000:]}")

    def witness(marker, count=1):
        return wait(lambda current: current.count(marker) >= count,
                    f"{marker} x{count}")

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
        events.extend((QMP["key_event"](code, True),
                       QMP["key_event"](code, False)))
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

    def startup_pids():
        current = text()
        display = re.findall(r"boring-init: desktop display spawned pid (\d+)", current)
        wm = re.findall(r"boring-init: desktop WM spawned pid (\d+)", current)
        if len(display) != 1 or len(wm) != 1:
            raise RuntimeError("missing unique PID1 desktop child identities")
        display_pid, wm_pid = int(display[0]), int(wm[0])
        if display_pid <= 1 or wm_pid <= 1 or display_pid == wm_pid:
            raise RuntimeError("invalid central child PID identities")
        return display_pid, wm_pid

    def shell_for(terminal_pid):
        children = re.findall(
            rf"boring-spawn: parent pid {terminal_pid} child pid (\d+) task \d+ detached",
            text())
        if len(children) != 1:
            raise RuntimeError(f"terminal {terminal_pid} does not own exactly one shell")
        return int(children[0])

    def type_text(value):
        for char in value:
            if not ("a" <= char <= "z"):
                raise RuntimeError(f"unsupported test character {char!r}")
            frame = frames()[-1]
            terminal_pid = next(tile["pid"] for tile in frame["tiles"]
                                if tile["token"] == frame["focus"])
            shell_pid = shell_for(terminal_pid)
            marker = f"fd-read: pid {shell_pid} fd 0 bytes 1"
            before = text().count(marker)
            key(char)
            witness(marker, before + 1)

    def run_fetch(terminal_pid):
        shell_pid = shell_for(terminal_pid)
        type_text("boringfetch")
        key("ret")
        child = wait(lambda current: re.findall(
            rf"boring-spawn: parent pid {shell_pid} child pid (\d+) task \d+ foreground",
            current), f"foreground boringfetch from shell {shell_pid}")[-1]
        witness(f"boring-spawn: child pid {child} exited status 0")
        witness(f"boring-spawn: reaped child pid {child} task/process cleanup complete")

    def capture(label, frame, mode):
        geometry = re.search(r"boring-framebuffer: (\d+)x(\d+)x(?:24|32)", text())
        if geometry is None:
            raise RuntimeError("missing framebuffer geometry")
        width, height = map(int, geometry.groups())
        qmp("stop")
        try:
            ppm = out / f"{label}.ppm"
            qmp("screendump", {"filename": str(ppm)})
            meta = dict(frame, width=width, height=height)
            (out / f"{label}.json").write_text(json.dumps(meta, indent=2) + "\n")
            decoded = TERM["validate"](ppm, meta, mode)
            (out / f"{label}.txt").write_text("\n".join(
                f"pid {pid}\n" + "\n".join(rows)
                for pid, rows in sorted(decoded.items())) + "\n")
        finally:
            qmp("cont")

    def settled_capture(label, frame, mode):
        deadline = time.monotonic() + 20
        while True:
            try:
                capture(label, frame, mode)
                return
            except ValueError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.1)

    def check_central_reaps(display_pid, wm_pid):
        current = text()
        for pid in (display_pid, wm_pid):
            marker = f"boring-spawn: reaped child pid {pid} task/process cleanup complete"
            if current.count(marker) != 1:
                raise RuntimeError(f"central child {pid} was not reaped exactly once")
        display_exit = current.find("boring-init: desktop display exited status ")
        wm_exit = current.find("boring-init: desktop WM exited status ")
        if min(display_exit, wm_exit) < 0:
            raise RuntimeError("missing central child exit witness")
        expected_first = wm_exit < display_exit if name != "display-first" else display_exit < wm_exit
        if not expected_first:
            raise RuntimeError(f"wrong central child reap order for {name}")

    try:
        witness("m38-desktop: real BoringFS root mounted")
        witness("m38-desktop: PID 1 scheduler task ready; desktop children come from BoringFS")
        witness("boring-init: desktop session state STARTING")
        witness("boring-spawn: VFS executable source /bin/boring-display")
        witness("boring-spawn: VFS executable source /bin/boringwm")
        witness("boring-init: desktop session state RUNNING")
        witness("display: M35 service and M31 input ready")
        witness("wm: boring.wm Ring3 policy ready; no pixel mappings")
        display_pid, wm_pid = startup_pids()
        if (f"boring-spawn: parent pid 1 child pid {display_pid}" not in text() or
                f"boring-spawn: parent pid 1 child pid {wm_pid}" not in text()):
            raise RuntimeError("central desktop processes are not direct PID1 children")

        key("ret", super_key=True)
        witness("wm: Super+Return spawned /bin/boring-terminal")
        witness("boring-spawn: VFS executable source /bin/boring-terminal")
        witness("boring-terminal: Ring3 managed client + PTY + boring-shell ready")
        witness("boring-spawn: VFS executable source /bin/boring-shell")
        prompt = latest(1)
        terminal_a = prompt["tiles"][0]["pid"]
        run_fetch(terminal_a)
        fetch_frame = latest(1, prompt["frame"] - 1)
        settled_capture("ready", fetch_frame, "fetch")

        if name == "normal":
            key("ret", super_key=True)
            witness("wm: Super+Return spawned /bin/boring-terminal", 2)
            witness("boring-terminal: Ring3 managed client + PTY + boring-shell ready", 2)
            dual = latest(2, fetch_frame["frame"])
            type_text("terminalb")
            key("j", super_key=True)
            switched = latest(2, dual["frame"])
            type_text("terminala")
            settled_capture("dual-focused", switched, "dual-a")
            key("q", super_key=True)
            witness("boring-terminal: CLOSE received")
            witness("boring-terminal: graceful cleanup complete")
            latest(1, switched["frame"])
            key("q", super_key=True)
            witness("boring-terminal: CLOSE received", 2)
            witness("boring-terminal: graceful cleanup complete", 2)
            witness("wm: session empty; clean exit")
            witness("boring-init: desktop WM exited status 0")
            witness("boring-init: desktop session state DRAINING")
            witness("display: session drained; exiting with claims")
            witness("boring-init: desktop display exited status 0")
            witness("boring-init: desktop session state DRAINED")
            witness("boring-init: desktop session drained")
            if "desktop session failed:" in text():
                raise RuntimeError("normal scenario was incorrectly classified FAILED")
        elif name == "wm-first":
            key("f12")
            witness("wm: M38 test-only unexpected Ring3 exit")
            witness(f"boring-spawn: child pid {wm_pid} exited status 72")
            witness("boring-init: desktop WM exited status 72")
            witness("boring-init: desktop session failed: WM exited status 72")
            witness("boring-init: desktop session state FAILED")
            witness("boring-init: desktop session state DRAINING")
            witness("display: manager disconnected; display survives")
            witness("display: session drained; exiting with claims")
            witness("boring-init: desktop display exited status 0")
            witness("boring-init: desktop session state DRAINED")
            witness("boring-init: desktop failed session drained")
        elif name == "display-first":
            key("f11")
            witness("display: M38 test-only unexpected Ring3 exit")
            witness(f"boring-spawn: child pid {display_pid} exited status 73")
            witness("boring-init: desktop display exited status 73")
            witness("boring-init: desktop session failed: display exited status 73")
            witness("boring-init: desktop session state FAILED")
            witness("boring-init: desktop session state DRAINING")
            witness(f"boring-spawn: child pid {wm_pid} exited status 90")
            witness("boring-init: desktop WM exited status 90")
            witness("boring-init: desktop session state DRAINED")
            witness("boring-init: desktop failed session drained")
        else:
            raise RuntimeError(f"unknown scenario {name}")

        witness("m38-desktop: IPC/input/framebuffer/M32/PTY resources drained")
        witness("m38-desktop: all desktop tasks/processes reaped; PID 1 remains")
        witness("M38 native desktop session supervision resource acceptance passed.")
        check_central_reaps(display_pid, wm_pid)

        current = text()
        if "m38-desktop: drain ipc services/connections/queued/attachments=0/0/0/0 memory_objects=0 input/fb=0/0" not in current:
            raise RuntimeError("M38 IPC/memory/input/framebuffer drain counters are not zero")
        if "m38-desktop: drain pty pairs/refs/waiters/bytes=0/0/0/0" not in current:
            raise RuntimeError("M38 PTY drain counters are not zero")
        process_line = re.search(
            r"m38-desktop: drain proc active/created/finished/current=(\d+)/(\d+)/(\d+)/(\d+) "
            r"task active/created/finished/current/pid/resume/fault=(\d+)/(\d+)/(\d+)/(\d+)/(\d+)/(\d+)/(\d+)",
            current)
        if process_line is None:
            raise RuntimeError("missing M38 process/task drain snapshot")
        values = tuple(map(int, process_line.groups()))
        proc_active, proc_created, proc_finished, current_pid = values[:4]
        task_active, task_created, task_finished, _task_id, task_pid, resume, fault = values[4:]
        if (proc_active, current_pid, task_active, task_pid, resume, fault) != (1, 1, 1, 1, 0, 0):
            raise RuntimeError(f"bad final PID1 process/task state: {values}")
        if proc_created != proc_finished + 1 or task_created != task_finished + 1:
            raise RuntimeError(f"inconsistent created/finished accounting: {values}")

        (out / "scenario.json").write_text(json.dumps({
            "scenario": name,
            "display_pid": display_pid,
            "wm_pid": wm_pid,
            "central_reaps_exactly_once": True,
            "process_task_snapshot": values,
            "success": True,
        }, indent=2) + "\n")
        (out / "SUCCESS.txt").write_text(
            f"M38 {name} real QEMU supervision acceptance SUCCESS\n")
        print(f"M38 {name} acceptance passed")
    except Exception as exc:
        if vm.poll() is None:
            try:
                qmp("stop")
                (out / "registers.txt").write_text(qmp(
                    "human-monitor-command", {"command-line": "info registers"}))
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
        subprocess.run(["sh", "tests/m38-build.sh"], cwd=ROOT,
                       stdout=log, stderr=subprocess.STDOUT, check=True)

    for scenario in ("normal", "wm-first", "display-first"):
        bundle = ROOT / f"build/m38-bundle-{scenario}"
        scenario_out = OUT / scenario
        scenario_out.mkdir(parents=True, exist_ok=True)
        for evidence in ("geometry.txt", "boringfsck.txt", "SHA256SUMS"):
            shutil.copyfile(bundle / evidence, scenario_out / evidence)
        run_scenario(scenario, bundle / "boringos-root.img")

    manifest = {}
    for path in sorted(OUT.rglob("*")):
        if path.is_file():
            manifest[str(path.relative_to(OUT))] = sha256(path)
    (OUT / "SHA256SUMS.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (OUT / "SUCCESS.txt").write_text(
        "M38 native desktop session supervision SUCCESS\n"
        "normal + unexpected WM-first + unexpected display-first real QEMU sessions drained to PID1 only.\n")
    print(f"M38 supervision acceptance passed; evidence: {OUT}")


if __name__ == "__main__":
    run()
