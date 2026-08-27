#!/usr/bin/env python3
"""M36 real QMP -> PS/2 -> WM -> terminal -> PTY -> shell visual acceptance."""
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
QMP = runpy.run_path(str(ROOT / "tests/qmp-input.py"))
WM_ORACLE = runpy.run_path(str(ROOT / "tests/validate-wm-screenshot.py"))
TERM_ORACLE = runpy.run_path(str(ROOT / "tests/validate-m36-terminal-screenshot.py"))
OUT = ROOT / "build/m36-desktop-reference"


def run():
    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)
    log_path = OUT / "serial.log"
    log_path.write_text("")
    with (OUT / "bundle.log").open("w") as bundle:
        subprocess.run(["sh", "tests/m36-bundle-test.sh"], cwd=ROOT,
                       stdout=bundle, stderr=subprocess.STDOUT, check=True)
    root_image = ROOT / "build/m36-bundle-test/boringos-root.img"
    with (OUT / "build.log").open("w") as build:
        subprocess.run(["make", "TEST_MODE=m36-desktop"], cwd=ROOT,
                       stdout=build, stderr=subprocess.STDOUT, check=True)
    qemu_log = (OUT / "qemu.log").open("w")
    vm = subprocess.Popen([
        os.environ.get("QEMU", "qemu-system-x86_64"), "-M", "q35", "-cpu",
        os.environ.get("QEMU_CPU", "qemu64,apic=off"), "-m", "128M",
        "-cdrom", str(ROOT / "build/boringos.iso"), "-boot", "d", "-vga", "std",
        "-drive", f"file={root_image},if=none,format=raw,id=boringdisk",
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
        def write(self, data): return vm.stdin.write(data)
        def flush(self): return vm.stdin.flush()
    pipes = QmpPipes()

    def log(): return log_path.read_text(errors="replace")
    def wait(predicate, description, timeout=60):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            text = log()
            if "M36 desktop FAILED" in text or "M36 terminal FAILED" in text or "Fatal exception" in text or "syscall fatal" in text:
                raise RuntimeError(f"guest failure while waiting for {description}\n{text[-9000:]}")
            if vm.poll() is not None:
                raise RuntimeError(f"QEMU exited ({vm.returncode}) while waiting for {description}")
            value = predicate(text)
            if value: return value
            time.sleep(0.04)
        raise RuntimeError(f"first missing witness: {description}\n{log()[-9000:]}")
    def witness(text, count=1): return wait(lambda output: output.count(text) >= count, f"{text} x{count}")
    def command(name, arguments=None):
        nonlocal qmp_ready
        if not qmp_ready:
            QMP["receive_message"](pipes)
            QMP["execute"](pipes, "qmp_capabilities")
            qmp_ready = True
        result = QMP["execute"](pipes, name, arguments)
        with (OUT / "qmp.log").open("a") as record:
            record.write(json.dumps({"command": name, "arguments": arguments, "result": result}) + "\n")
        return result
    def key(code, super_key=False):
        events = []
        if super_key: events.append(QMP["key_event"]("meta_l", True))
        events += [QMP["key_event"](code, True), QMP["key_event"](code, False)]
        if super_key: events.append(QMP["key_event"]("meta_l", False))
        command("input-send-event", {"events": events})
        time.sleep(0.025)
    def type_text(text):
        for ch in text:
            if not ("a" <= ch <= "z"):
                raise RuntimeError(f"unsupported test character {ch!r}")
            frame = WM_ORACLE["frames"](log())[-1]
            terminal_pid = next(t["pid"] for t in frame["tiles"] if t["token"] == frame["focus"])
            children = re.findall(rf"boring-spawn: parent pid {terminal_pid} child pid (\d+) task \d+ detached", log())
            if len(children) != 1:
                raise RuntimeError("focused terminal does not have exactly one shell")
            read_marker = f"fd-read: pid {children[0]} fd 0 bytes 1"
            before = log().count(read_marker)
            key(ch)
            witness(read_marker, before + 1)
    def latest_frame(count=None, after=0):
        def match(text):
            frames = WM_ORACLE["frames"](text)
            frames = [f for f in frames if f["frame"] > after and (count is None or f["count"] == count)]
            return frames[-1] if frames else None
        return wait(match, f"WM frame count={count} after={after}")
    def capture(name, frame, mode):
        match = re.search(r"boring-framebuffer: (\d+)x(\d+)x(?:24|32)", log())
        if not match: raise RuntimeError("missing framebuffer geometry")
        width, height = map(int, match.groups())
        command("stop")
        try:
            ppm = OUT / f"{name}.ppm"
            command("screendump", {"filename": str(ppm)})
            meta = dict(frame, width=width, height=height)
            (OUT / f"{name}.json").write_text(json.dumps(meta, indent=2) + "\n")
            decoded = TERM_ORACLE["validate"](ppm, meta, mode)
            (OUT / f"{name}.txt").write_text("\n".join(
                f"pid {pid}\n" + "\n".join(rows) for pid, rows in sorted(decoded.items())) + "\n")
        finally:
            command("cont")

    def settled_capture(name, frame, mode):
        # A returned QMP keyboard command only queues device events; it does not
        # mean the shell, parser and compositor have finished their work.
        deadline = time.monotonic() + 20
        while True:
            try:
                capture(name, frame, mode)
                return
            except ValueError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.1)

    try:
        witness("m36-desktop: display+wm scheduler tasks ready")
        witness("m36-desktop: real BoringFS root mounted")
        witness("display: M35 service and M31 input ready")
        witness("wm: boring.wm Ring3 policy ready; no pixel mappings")

        key("ret", super_key=True)
        witness("wm: Super+Return spawned /bin/boring-terminal")
        witness("boring-spawn: VFS executable source /bin/boring-terminal")
        witness("boring-terminal: Ring3 managed client + PTY + boring-shell ready")
        witness("boring-spawn: VFS executable source /bin/boring-shell")
        prompt_frame = latest_frame(1)
        time.sleep(0.25)
        settled_capture("prompt", prompt_frame, "prompt")

        type_text("boringfetch")
        key("ret")
        witness("boring-spawn: VFS executable source /bin/boringfetch")
        witness("exited status 0")
        time.sleep(0.35)
        fetch_frame = latest_frame(1, prompt_frame["frame"] - 1)
        settled_capture("boringfetch", fetch_frame, "fetch")

        key("ret", super_key=True)
        witness("wm: Super+Return spawned /bin/boring-terminal", 2)
        witness("boring-terminal: Ring3 managed client + PTY + boring-shell ready", 2)
        dual = latest_frame(2, fetch_frame["frame"])
        time.sleep(0.25)

        type_text("terminalb")
        time.sleep(0.2)
        settled_capture("dual-focused-b", dual, "dual-b")
        key("j", super_key=True)
        switched = latest_frame(2, dual["frame"])
        type_text("terminala")
        time.sleep(0.2)
        settled_capture("dual-focused-a", switched, "dual-a")

        key("q", super_key=True)
        witness("boring-terminal: CLOSE received")
        witness("boring-terminal: PTY master closed")
        witness("boring-terminal: graceful cleanup complete")
        single = latest_frame(1, switched["frame"])
        time.sleep(0.2)
        settled_capture("after-super-q", single, "single-after-close")

        key("q", super_key=True)
        witness("boring-terminal: CLOSE received", 2)
        witness("boring-terminal: graceful cleanup complete", 2)
        witness("M36 graphical terminal desktop acceptance passed.")
        (OUT / "SUCCESS.txt").write_text(
            "M36 graphical desktop SUCCESS\nReal QMP->PS/2->M31->WM->SPAWN->terminal->PTY->shell->boringfetch pixels; dual isolation; Super+Q cleanup.\n")
        print(f"M36 graphical desktop acceptance passed; evidence: {OUT}")
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
        try: vm.wait(timeout=5)
        except subprocess.TimeoutExpired:
            vm.kill(); vm.wait()
        qemu_log.close()
        vm.stdin.close(); vm.stdout.close()


if __name__ == "__main__": run()
