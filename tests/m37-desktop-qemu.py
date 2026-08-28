#!/usr/bin/env python3
"""M37 PID1 -> BoringFS display/WM -> terminal desktop real QEMU acceptance."""
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
OUT = ROOT / "build/m37-desktop-reference"
QMP = runpy.run_path(str(ROOT / "tests/qmp-input.py"))
WM_ORACLE = runpy.run_path(str(ROOT / "tests/validate-wm-screenshot.py"))
TERM_ORACLE = runpy.run_path(str(ROOT / "tests/validate-m36-terminal-screenshot.py"))


def run():
    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)
    log_path = OUT / "serial.log"
    log_path.write_text("")

    with (OUT / "bundle.log").open("w") as bundle:
        subprocess.run(["sh", "tests/m37-bundle-test.sh"], cwd=ROOT,
                       stdout=bundle, stderr=subprocess.STDOUT, check=True)
    for name in ("geometry.txt", "boringfsck.txt", "SHA256SUMS"):
        shutil.copyfile(ROOT / "build/m37-bundle-test" / name, OUT / name)
    root_image = ROOT / "build/m37-bundle-test/boringos-root.img"

    with (OUT / "build.log").open("w") as build:
        subprocess.run(["sh", "tests/m37-build.sh"], cwd=ROOT,
                       stdout=build, stderr=subprocess.STDOUT, check=True)
    iso = ROOT / "build/boringos.iso"
    (OUT / "image-hashes.json").write_text(json.dumps(dict(
        iso_sha256=hashlib.sha256(iso.read_bytes()).hexdigest(),
        root_sha256=hashlib.sha256(root_image.read_bytes()).hexdigest()),
        indent=2) + "\n")

    qemu_log = (OUT / "qemu.log").open("w")
    vm = subprocess.Popen([
        os.environ.get("QEMU", "qemu-system-x86_64"), "-M", "q35", "-cpu",
        os.environ.get("QEMU_CPU", "qemu64,apic=off"), "-m", "128M",
        "-cdrom", str(iso), "-boot", "d", "-vga", "std",
        "-drive", f"file={root_image},if=none,format=raw,id=boringdisk,readonly=on",
        "-device", "virtio-blk-pci,drive=boringdisk,disable-legacy=on",
        "-display", "none", "-serial", f"file:{log_path}", "-monitor", "none",
        "-qmp", "stdio", "-no-reboot", "-no-shutdown"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=qemu_log, bufsize=0)
    qmp_ready = False

    class QmpPipes:
        def readline(self):
            if not select.select([vm.stdout], [], [], 10)[0]:
                raise RuntimeError("QMP pipe response timeout")
            return vm.stdout.readline()

        def write(self, data):
            return vm.stdin.write(data)

        def flush(self):
            return vm.stdin.flush()

    pipes = QmpPipes()

    def log():
        return log_path.read_text(errors="replace")

    def wait(predicate, description, timeout=75):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            text = log()
            if ("M37 desktop FAILED" in text or "boring-init: FAILED" in text or
                    "Fatal exception" in text or "syscall fatal" in text or
                    "display: FAILED" in text or "wm: FAILED" in text or
                    "boring-terminal: FAILED" in text):
                raise RuntimeError(
                    f"guest failure while waiting for {description}\n{text[-10000:]}")
            if vm.poll() is not None:
                raise RuntimeError(
                    f"QEMU exited ({vm.returncode}) while waiting for {description}")
            value = predicate(text)
            if value:
                return value
            time.sleep(0.04)
        raise RuntimeError(f"first missing witness: {description}\n{log()[-10000:]}")

    def witness(text, count=1):
        return wait(lambda output: output.count(text) >= count,
                    f"{text} x{count}")

    def command(name, arguments=None):
        nonlocal qmp_ready
        if not qmp_ready:
            QMP["receive_message"](pipes)
            QMP["execute"](pipes, "qmp_capabilities")
            qmp_ready = True
        result = QMP["execute"](pipes, name, arguments)
        with (OUT / "qmp.log").open("a") as record:
            record.write(json.dumps({"command": name, "arguments": arguments,
                                     "result": result}) + "\n")
        return result

    def key(code, super_key=False):
        events = []
        if super_key:
            events.append(QMP["key_event"]("meta_l", True))
        events += [QMP["key_event"](code, True), QMP["key_event"](code, False)]
        if super_key:
            events.append(QMP["key_event"]("meta_l", False))
        command("input-send-event", {"events": events})
        time.sleep(0.025)

    def shell_for(terminal_pid):
        children = re.findall(
            rf"boring-spawn: parent pid {terminal_pid} child pid (\d+) task \d+ detached",
            log())
        if len(children) != 1:
            raise RuntimeError("terminal must own exactly one shell")
        return int(children[0])

    def type_text(text):
        for char in text:
            if not ("a" <= char <= "z"):
                raise RuntimeError(f"unsupported test character {char!r}")
            frame = WM_ORACLE["frames"](log())[-1]
            terminal_pid = next(
                tile["pid"] for tile in frame["tiles"]
                if tile["token"] == frame["focus"])
            shell_pid = shell_for(terminal_pid)
            marker = f"fd-read: pid {shell_pid} fd 0 bytes 1"
            before = log().count(marker)
            key(char)
            witness(marker, before + 1)

    def run_fetch(terminal_pid):
        shell_pid = shell_for(terminal_pid)
        type_text("boringfetch")
        key("ret")
        child = wait(lambda text: re.findall(
            rf"boring-spawn: parent pid {shell_pid} child pid (\d+) task \d+ foreground",
            text), f"foreground boringfetch from shell {shell_pid}")[-1]
        witness(f"boring-spawn: child pid {child} exited status 0")
        witness(f"boring-spawn: reaped child pid {child} task/process cleanup complete")

    def latest_frame(count=None, after=0):
        def match(text):
            frames = WM_ORACLE["frames"](text)
            frames = [frame for frame in frames
                      if frame["frame"] > after and
                      (count is None or frame["count"] == count)]
            return frames[-1] if frames else None
        return wait(match, f"WM frame count={count} after={after}")

    def capture(name, frame, mode):
        match = re.search(r"boring-framebuffer: (\d+)x(\d+)x(?:24|32)", log())
        if not match:
            raise RuntimeError("missing framebuffer geometry")
        width, height = map(int, match.groups())
        command("stop")
        try:
            ppm = OUT / f"{name}.ppm"
            command("screendump", {"filename": str(ppm)})
            meta = dict(frame, width=width, height=height)
            (OUT / f"{name}.json").write_text(json.dumps(meta, indent=2) + "\n")
            decoded = TERM_ORACLE["validate"](ppm, meta, mode)
            (OUT / f"{name}.txt").write_text("\n".join(
                f"pid {pid}\n" + "\n".join(rows)
                for pid, rows in sorted(decoded.items())) + "\n")
        finally:
            command("cont")

    def settled_capture(name, frame, mode):
        deadline = time.monotonic() + 20
        while True:
            try:
                capture(name, frame, mode)
                return
            except ValueError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.1)

    def startup_pids():
        text = log()
        display = re.findall(r"boring-init: desktop display spawned pid (\d+)", text)
        wm = re.findall(r"boring-init: desktop WM spawned pid (\d+)", text)
        if len(display) != 1 or len(wm) != 1:
            raise RuntimeError("missing unique PID1 desktop child identities")
        return int(display[0]), int(wm[0])

    def check_resources(frame):
        text = log()
        display_pid, wm_pid = startup_pids()
        cr3s = {int(pid): int(cr3, 16) for pid, cr3 in re.findall(
            r"boring-spawn: child pid (\d+) cr3 (0x[0-9a-fA-F]+)", text)}
        init_match = re.search(
            r"m37-desktop: enter CPL3 pid (\d+) name \S+ cr3 (0x[0-9a-fA-F]+)",
            text)
        if init_match is None:
            raise RuntimeError("missing PID1 CR3 witness")
        cr3s[int(init_match.group(1))] = int(init_match.group(2), 16)
        ptys = {int(pid): (int(slot), int(generation))
                for pid, slot, generation, _master, _slave in re.findall(
                    r"boring-pty: owner pid (\d+) slot (\d+) generation (\d+) master (\d+) slave (\d+)",
                    text)}
        buffers = {int(pid): (int(obj), int(size))
                   for pid, obj, size in re.findall(
                       r"m36-buffer: owner pid (\d+) object (\d+) bytes (\d+)", text)}
        terminals = []
        for tile in frame["tiles"]:
            pid = tile["pid"]
            terminals.append(dict(pid=pid, shell_pid=shell_for(pid),
                                  cr3=cr3s[pid], pty=ptys[pid],
                                  buffer=buffers[pid], surface=tile["surface"],
                                  token=tile["token"]))
        live_pids = [1, display_pid, wm_pid] + [
            pid for terminal in terminals
            for pid in (terminal["pid"], terminal["shell_pid"])]
        if len(live_pids) != 7 or len(set(live_pids)) != 7:
            raise RuntimeError("M37 desktop process identities overlap")
        if len({cr3s[pid] for pid in live_pids}) != 7:
            raise RuntimeError("M37 desktop processes share an address space")
        if len({terminal["pty"] for terminal in terminals}) != 2:
            raise RuntimeError("M37 terminals share a PTY")
        if len({terminal["buffer"][0] for terminal in terminals}) != 2 or any(
                terminal["buffer"][1] != 800 * 600 * 4 for terminal in terminals):
            raise RuntimeError("M37 terminal shared buffers are not independent")
        if display_pid not in buffers:
            raise RuntimeError("display composition buffer witness missing")
        if buffers[display_pid][0] in {
                terminal["buffer"][0] for terminal in terminals}:
            raise RuntimeError("terminal aliases display composition buffer")
        if "boring-launch:" in text:
            raise RuntimeError("M37 desktop startup used legacy LAUNCH")
        if f"boring-spawn: parent pid 1 child pid {display_pid}" not in text or \
           f"boring-spawn: parent pid 1 child pid {wm_pid}" not in text:
            raise RuntimeError("display/WM were not PID1 SPAWN children")
        (OUT / "resources.json").write_text(json.dumps(dict(
            init_pid=1, display_pid=display_pid, wm_pid=wm_pid,
            terminals=terminals, live_pids=live_pids,
            cr3={str(pid): cr3s[pid] for pid in live_pids}), indent=2) + "\n")

    try:
        witness("m37-desktop: real BoringFS root mounted")
        witness("m37-desktop: PID 1 scheduler task ready; desktop children must come from BoringFS")
        witness("boring-spawn: VFS executable source /bin/boring-display")
        witness("boring-init: boring.display service online")
        witness("boring-spawn: VFS executable source /bin/boringwm")
        witness("display: M35 service and M31 input ready")
        witness("wm: boring.wm Ring3 policy ready; no pixel mappings")
        display_pid, wm_pid = startup_pids()
        if display_pid != 2 or wm_pid != 3:
            raise RuntimeError(
                f"unexpected deterministic startup PIDs: display={display_pid} wm={wm_pid}")

        key("ret", super_key=True)
        witness("wm: Super+Return spawned /bin/boring-terminal")
        witness("boring-spawn: VFS executable source /bin/boring-terminal")
        witness("boring-terminal: Ring3 managed client + PTY + boring-shell ready")
        witness("boring-spawn: VFS executable source /bin/boring-shell")
        prompt = latest_frame(1)
        settled_capture("prompt", prompt, "prompt")
        terminal_a = prompt["tiles"][0]["pid"]

        run_fetch(terminal_a)
        fetch_frame = latest_frame(1, prompt["frame"] - 1)
        settled_capture("boringfetch", fetch_frame, "fetch")

        key("ret", super_key=True)
        witness("wm: Super+Return spawned /bin/boring-terminal", 2)
        witness("boring-terminal: Ring3 managed client + PTY + boring-shell ready", 2)
        dual = latest_frame(2, fetch_frame["frame"])
        settled_capture("dual-ready", dual, "dual-ready")
        check_resources(dual)

        key("j", super_key=True)
        switched = latest_frame(2, dual["frame"])
        type_text("focuscheck")
        settled_capture("dual-focus-isolation", switched, "dual-a")

        key("q", super_key=True)
        witness("boring-terminal: CLOSE received")
        witness("boring-terminal: graceful cleanup complete")
        single = latest_frame(1, switched["frame"])
        settled_capture("after-first-close", single, "single-after-close")

        key("q", super_key=True)
        witness("boring-terminal: CLOSE received", 2)
        witness("boring-terminal: graceful cleanup complete", 2)
        witness("wm: session empty; clean exit")
        witness("boring-init: desktop WM exited status 0")
        witness("display: session drained; exiting with claims")
        witness("boring-init: desktop session drained")
        witness("m37-desktop: IPC/input/framebuffer/M32/PTY desktop resources drained")
        witness("m37-desktop: all spawned desktop tasks/processes reaped; PID 1 remains")
        witness("M37 native desktop session startup acceptance passed.")
        (OUT / "SUCCESS.txt").write_text(
            "M37 native desktop session startup SUCCESS\n"
            "PID1 SPAWN from persistent BoringFS -> display -> WM -> Super+Return terminal -> PTY shell -> boringfetch; dual focus/input; complete desktop child drain with PID1 remaining.\n")
        print(f"M37 desktop startup acceptance passed; evidence: {OUT}")
    except Exception as exc:
        if vm.poll() is None:
            try:
                command("stop")
                (OUT / "registers.txt").write_text(command(
                    "human-monitor-command", {"command-line": "info registers"}))
                command("screendump", {"filename": str(OUT / "failure.ppm")})
            except Exception as diagnostic_error:
                (OUT / "diagnostic-error.txt").write_text(str(diagnostic_error))
        (OUT / "FAILURE.txt").write_text(str(exc) + "\n")
        raise
    finally:
        vm.terminate()
        try:
            vm.wait(timeout=5)
        except subprocess.TimeoutExpired:
            vm.kill()
            vm.wait()
        qemu_log.close()
        vm.stdin.close()
        vm.stdout.close()


if __name__ == "__main__":
    run()
