#!/usr/bin/env python3
"""M39 real editor input, framebuffer, independent cat and persisted BoringFS acceptance."""
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
OUT = ROOT / "build/m39-edit-reference"
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
        "-drive", f"file={root_image},if=none,format=raw,id=boringdisk,readonly=off",
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
            fatal = ("M38 desktop FAILED", "boring-edit FAILED", "boring-init: FAILED",
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

    def key(code, super_key=False, ctrl=False):
        events = []
        if ctrl:
            events.append(QMP["key_event"]("ctrl", True))
        if super_key:
            events.append(QMP["key_event"]("meta_l", True))
        events.extend((QMP["key_event"](code, True),
                       QMP["key_event"](code, False)))
        if super_key:
            events.append(QMP["key_event"]("meta_l", False))
        if ctrl:
            events.append(QMP["key_event"]("ctrl", False))
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

    def type_text(value, editor=False):
        codes = {" ": "spc", "/": "slash", "-": "minus", ".": "dot", "\n": "ret"}
        for char in value:
            frame = frames()[-1]
            terminal_pid = next(tile["pid"] for tile in frame["tiles"]
                                if tile["token"] == frame["focus"])
            marker = "boring-edit: key/redraw" if editor else (
                f"fd-read: pid {shell_for(terminal_pid)} fd 0 bytes 1")
            before = text().count(marker)
            key(codes.get(char, char))
            witness(marker, before + 1)

    def command(value):
        type_text(value)
        key("ret")

    def launch_editor(value):
        prior = frames()[-1]["frame"]
        count = text().count("boring-edit: Ring3 managed client ready")
        command(value)
        witness("boring-edit: Ring3 managed client ready", count + 1)
        return latest(2, after=prior)

    def close_editor():
        prior = frames()[-1]["frame"]
        count = text().count("boring-edit: graceful cleanup complete")
        key("q", super_key=True)
        witness("boring-edit: graceful cleanup complete", count + 1)
        latest(1, after=prior)

    def independent_cat(path):
        before = text().count("boring-spawn: VFS executable source /bin/cat")
        child_count = len(re.findall(r"boring-spawn: parent pid \d+ child pid (\d+) task \d+ foreground", text()))
        command("cat " + path)
        witness("boring-spawn: VFS executable source /bin/cat", before + 1)
        child = wait(lambda current: (
            matches[-1] if len(matches := re.findall(
                r"boring-spawn: parent pid \d+ child pid (\d+) task \d+ foreground", current)) > child_count
            else None), "new cat child")
        witness(f"boring-spawn: child pid {child} exited status 0")
        witness(f"boring-spawn: reaped child pid {child} task/process cleanup complete")
        witness(f"fd-read: pid {child} fd 3 bytes")
        return int(child)

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
            # Same unmodified font and full-frame oracle, at real renderer bounds.
            TERM["decode"].__globals__["MAX_COLS"] = 160
            TERM["decode"].__globals__["MAX_ROWS"] = 96
            decoded = TERM["decode"](ppm, meta)
            mode(decoded, meta)
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
        # Create the initial file in the guest, independently of the editor.
        command("touch /note.txt")
        command("write /note.txt seed")
        edit_frame = launch_editor("boring-edit /note.txt")
        witness("boring-edit: loaded real BoringFS document")
        type_text(" hello\nboringx", editor=True)
        key("backspace")
        key("s", ctrl=True)
        witness("boring-edit: saved document")

        def assert_editor(screens, meta):
            pid = next(t["pid"] for t in meta["tiles"] if t["token"] == meta["focus"])
            rows = screens[pid]
            if not ("BoringEdit" in rows[0] and rows[1] == "seed" and rows[2] == " hello" and
                    rows[3] == "boring" and "Saved to BoringFS" in rows[-2]):
                raise ValueError(f"actual editor pixels do not show saved document: {rows}")
            other = next(value for key_pid, value in screens.items() if key_pid != pid)
            if any(row == "boring" for row in other):
                raise ValueError("editor input leaked into terminal")

        settled_capture("editor-saved", latest(2), assert_editor)
        close_editor()
        cat_pid = independent_cat("/note.txt")

        def assert_cat(screens, _meta):
            rows = next(iter(screens.values()))
            if "seed" not in rows or not any(row.strip() == "hello" for row in rows) or not any("boring" in row for row in rows):
                raise ValueError(f"independent cat pixels missing: {rows}")

        settled_capture("independent-cat", latest(1), assert_cat)
        launch_editor("boring-edit /note.txt")
        witness("boring-edit: loaded real BoringFS document", 2)
        for _ in range(6):
            key("backspace")
        type_text("os", editor=True)
        key("s", ctrl=True)
        witness("boring-edit: saved document", 2)
        close_editor()
        independent_cat("/note.txt")

        # No-path invocation really starts empty and creates an empty file.
        launch_editor("boring-edit")
        key("s", ctrl=True)
        witness("boring-edit: saved document", 3)
        close_editor()

        # Failed save keeps the buffer and dirty-close guard; explicit discard exits.
        launch_editor("boring-edit /missing/note.txt")
        type_text("keep", editor=True)
        key("s", ctrl=True)
        witness("boring-edit: save failed; buffer retained")
        key("q", super_key=True)
        witness("boring-edit: CLOSE received", 4)

        def assert_retained(screens, meta):
            pid = next(t["pid"] for t in meta["tiles"] if t["token"] == meta["focus"])
            rows = screens[pid]
            if rows[1] != "keep" or not any("UNSAVED" in row for row in rows):
                raise ValueError("failed save lost buffer or dirty guard")

        settled_capture("failed-save-retained", latest(2), assert_retained)
        prior = frames()[-1]["frame"]
        key("q", ctrl=True)
        witness("boring-edit: graceful cleanup complete", 4)
        latest(1, after=prior)
        key("q", super_key=True)
        witness("boring-terminal: graceful cleanup complete")
        witness("boring-init: desktop session drained")

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
            "independent_cat_pid": cat_pid,
            "process_task_snapshot": values,
            "success": True,
        }, indent=2) + "\n")
        (out / "SUCCESS.txt").write_text(
            f"M39 {name} real QEMU editor acceptance SUCCESS\n")
        print(f"M39 {name} acceptance passed")
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
        subprocess.run(["sh", "tests/m39-build.sh"], cwd=ROOT,
                       stdout=log, stderr=subprocess.STDOUT, check=True)

    bundle = ROOT / "build/m39-bundle"
    run_scenario("normal", bundle / "boringos-root.img")
    # QEMU is fully stopped: independently decode the real modified block image.
    image = bundle / "boringos-root.img"
    check = subprocess.check_output([str(ROOT / "build/boringfsck"), str(image)])
    (OUT / "boringfsck-after.txt").write_bytes(check)
    if b"Status: VALID" not in check:
        raise RuntimeError("persisted image invalid")
    for path, expected in (("/note.txt", b"seed\n hello\nos"), ("/untitled.txt", b"")):
        actual = subprocess.check_output([str(ROOT / "build/boringfsck"), "--cat", path, str(image)])
        if actual != expected:
            raise RuntimeError(f"persisted exact bytes mismatch: {path}: {actual!r}")
        (OUT / (Path(path).name + ".exact-bytes")).write_bytes(actual)

    manifest = {}
    for path in sorted(OUT.rglob("*")):
        if path.is_file():
            manifest[str(path.relative_to(OUT))] = sha256(path)
    (OUT / "SHA256SUMS.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (OUT / "SUCCESS.txt").write_text(
        "M39 native BoringEdit SUCCESS\n"
        "Real input + editor framebuffer + independent cat + exact persisted bytes + PID1-only drain.\n")
    print(f"M39 editor acceptance passed; evidence: {OUT}")


if __name__ == "__main__":
    run()
