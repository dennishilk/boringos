#!/usr/bin/env python3
"""Real five-process desktop acceptance with QMP -> PS/2 -> M31 input."""
import json
import os
import re
import runpy
import select
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
QMP = runpy.run_path(str(ROOT / "tests/qmp-input.py"))
ORACLE = runpy.run_path(str(ROOT / "tests/validate-wm-screenshot.py"))


def run(mode):
    out = ROOT / "build" / ("m35-wm-death" if mode == "m35-wm-death" else "m35-wm-reference")
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True, exist_ok=True)
    log_path = out / "serial.log"
    log_path.write_text("")
    with (out / "build.log").open("w") as build:
        subprocess.run(["make", f"TEST_MODE={mode}"], cwd=ROOT, stdout=build, stderr=subprocess.STDOUT, check=True)
    qemu_log = (out / "qemu.log").open("w")
    vm = subprocess.Popen([os.environ.get("QEMU", "qemu-system-x86_64"), "-M", "q35", "-cpu",
                           os.environ.get("QEMU_CPU", "qemu64,apic=off"), "-m", "128M", "-cdrom",
                           str(ROOT / "build/boringos.iso"), "-boot", "d", "-vga", "std", "-display", "none",
                           "-serial", f"file:{log_path}", "-monitor", "none", "-qmp", "stdio",
                           "-no-reboot", "-no-shutdown"],
                          stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=qemu_log, bufsize=0)
    cursor = None
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

    def wait(predicate, description):
        end = time.monotonic() + 60
        while time.monotonic() < end:
            text = log()
            if "FAILED" in text or "Fatal exception" in text or "syscall fatal" in text:
                raise RuntimeError(f"guest failure while waiting for {description}\n{text[-7000:]}")
            if vm.poll() is not None:
                raise RuntimeError(f"QEMU exited ({vm.returncode}): {qemu_log.name}")
            value = predicate(text)
            if value:
                return value
            time.sleep(0.04)
        raise RuntimeError(f"first missing witness: {description}\n{log()[-7000:]}")

    def witness(text):
        return wait(lambda output: text in output, text)

    def command(name, arguments=None):
        nonlocal qmp_ready
        if not qmp_ready:
            QMP["receive_message"](pipes)
            QMP["execute"](pipes, "qmp_capabilities")
            qmp_ready = True
        result = QMP["execute"](pipes, name, arguments)
        with (out / "qmp.log").open("a") as record:
            record.write(json.dumps({"command": name, "arguments": arguments, "result": result}) + "\n")
        return result

    def chord(key, shift=False, super_key=True):
        events = []
        modifiers = (["meta_l"] if super_key else []) + (["shift"] if shift else [])
        for code in modifiers:
            events.append(QMP["key_event"](code, True))
        events.extend((QMP["key_event"](key, True), QMP["key_event"](key, False)))
        for code in reversed(modifiers):
            events.append(QMP["key_event"](code, False))
        command("input-send-event", {"events": events})

    def move(dx, dy):
        nonlocal cursor
        command("input-send-event", {"events": [QMP["rel_event"]("x", dx), QMP["rel_event"]("y", dy)]})
        cursor = (max(0, min(width - 1, cursor[0] + dx)), max(0, min(height - 1, cursor[1] + dy)))
        time.sleep(0.08)

    def state(order, focus, after=0):
        def match(text):
            completed = ORACLE["frames"](text)
            if not completed:
                return None
            frame = completed[-1]
            focused = [t["pid"] for t in frame["tiles"] if t["token"] == frame["focus"]]
            if frame["frame"] > after and [t["pid"] for t in frame["tiles"]] == order and focused == [focus]:
                return frame
            return None
        return wait(match, f"frame order={order} focus={focus} after={after}")

    def capture(name, frame):
        command("stop")
        try:
            ppm = out / f"{name}.ppm"
            command("screendump", {"filename": str(ppm)})
            metadata = dict(frame, width=width, height=height, cursor=cursor)
            (out / f"{name}.json").write_text(json.dumps(metadata, indent=2) + "\n")
            ORACLE["validate"](ppm, metadata)
        finally:
            command("cont")

    try:
        witness("wm-test: five distinct processes ready")
        witness("wm: boring.wm Ring3 policy ready; no pixel mappings")
        for client in "abc":
            witness(f"wm-client-{client}: managed ready")
        witness("wm-client-a: malformed requests rejected")
        witness("wm-client-c: foreign client token rejected")
        match = re.search(r"boring-framebuffer: (\d+)x(\d+)x(?:24|32)", log())
        if not match:
            raise RuntimeError("missing geometry witness")
        width, height = map(int, match.groups())
        cursor = (width // 2, height // 2)
        initial = state([3, 4, 5], 5)
        if mode == "m35-wm-death":
            chord("ret")
            witness("wm: dedicated negative acceptance exits WM")
            witness("display: manager disconnected; display survives")
            for client in "abc":
                witness(f"wm-client-{client}: WM gone; app and display survived")
        else:
            capture("initial-layout", initial)
            chord("j"); focused = state([3, 4, 5], 3, initial["frame"])
            capture("focus-changed", focused)
            chord("j"); next_focus = state([3, 4, 5], 4, focused["frame"])
            chord("k"); back = state([3, 4, 5], 3, next_focus["frame"])
            chord("j", shift=True); reordered = state([4, 3, 5], 3, back["frame"])
            capture("reordered", reordered)
            # Real relative mouse packets, never oversized PS/2 deltas.
            while cursor[0] != 40 or cursor[1] != 30:
                move(max(-127, min(127, 40 - cursor[0])), max(-127, min(127, 30 - cursor[1])))
            mouse_focus = state([4, 3, 5], 4, reordered["frame"])
            chord("right"); before_close = state([4, 3, 5], 3, mouse_focus["frame"])
            chord("q"); witness("wm-client-a: graceful close received")
            closed = state([4, 5], 5, before_close["frame"])
            capture("after-close", closed)
            chord("x", super_key=False); witness("wm-client-c: unsolicited exit with live resources")
            exited = state([4], 4, closed["frame"])
            capture("after-exit", exited)
            chord("ret"); witness("wm: terminal unavailable; no configured launcher")
            chord("q"); witness("wm-client-b: graceful close received")
        witness("wm-test: IPC/input/framebuffer/M32 resources reclaimed")
        witness("M35 native BoringWM acceptance passed.")
        text = log()
        entries = re.findall(r"^wm-test: enter CPL3 pid (\d+) cr3 (\S+)$", text, re.M)
        if len(entries) != 5 or {int(p) for p, _ in entries} != set(range(1, 6)) or len({cr3 for _, cr3 in entries}) != 5:
            raise RuntimeError("five distinct real CPL3 process/address-space witnesses missing")
        (out / "SUCCESS.txt").write_text(f"{mode}: SUCCESS\nFive distinct CPL3 processes/address spaces; real M33/M32/M34 path.\n")
        print(f"{mode}: real QEMU acceptance passed; evidence: {out}")
    except Exception as exc:
        (out / "FAILURE.txt").write_text(str(exc) + "\n")
        raise
    finally:
        vm.terminate()
        try:
            vm.wait(timeout=5)
        except subprocess.TimeoutExpired:
            vm.kill(); vm.wait()
        qemu_log.close()
        vm.stdin.close()
        vm.stdout.close()


if __name__ == "__main__":
    run(sys.argv[1] if len(sys.argv) > 1 else "m35-wm")
