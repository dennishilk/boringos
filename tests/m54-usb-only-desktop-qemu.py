#!/usr/bin/env python3
"""M54 i8042-free xHCI USB-only full graphical desktop acceptance."""
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
HUB_MOUSE = os.environ.get("M66_USB_HUB_MOUSE", "0") == "1"
TYPEMATIC = os.environ.get("M67_TYPEMATIC", "0") == "1"
OUT = ROOT / ("build/m66-usb-hub-desktop-reference"
              if HUB_MOUSE else "build/m54-usb-only-desktop-reference")
QMP = runpy.run_path(str(ROOT / "tests/qmp-input.py"))
WM = runpy.run_path(str(ROOT / "tests/validate-wm-screenshot.py"))
TERM = runpy.run_path(str(ROOT / "tests/validate-m36-terminal-screenshot.py"))


def run():
    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)
    serial = OUT / "serial.log"
    serial.write_text("")

    with (OUT / "build.log").open("w") as out:
        subprocess.run(["sh", "tests/m54-build.sh"], cwd=ROOT,
                       stdout=out, stderr=subprocess.STDOUT, check=True)
    bundle_env = os.environ.copy()
    bundle_env["M37_BUNDLE_EXTRA_USER_CPPFLAGS"] = \
        ("-DBORING_M54_USB_ONLY_DESKTOP=1 -DBORING_M66_USB_HUB_MOUSE=1"
         if HUB_MOUSE else "-DBORING_M54_USB_ONLY_DESKTOP=1")
    with (OUT / "bundle.log").open("w") as out:
        subprocess.run(["sh", "tests/m37-bundle-test.sh"], cwd=ROOT,
                       env=bundle_env, stdout=out, stderr=subprocess.STDOUT,
                       check=True)
    for name in ("geometry.txt", "boringfsck.txt", "SHA256SUMS"):
        shutil.copyfile(ROOT / "build/m37-bundle-test" / name, OUT / name)
    root_image = ROOT / "build/m37-bundle-test/boringos-root.img"
    iso = ROOT / "build/boringos.iso"
    (OUT / "image-hashes.json").write_text(json.dumps({
        "iso_sha256": hashlib.sha256(iso.read_bytes()).hexdigest(),
        "root_sha256": hashlib.sha256(root_image.read_bytes()).hexdigest(),
    }, indent=2) + "\n")

    qemu_log = (OUT / "qemu.log").open("w")
    usb_devices = ([
        "-device", "qemu-xhci,id=xhci,p3=0",
        "-device", "usb-hub,id=hub,bus=xhci.0,port=1",
        "-device", "usb-mouse,bus=xhci.0,port=1.1",
        "-device", "usb-kbd,bus=xhci.0,port=2",
    ] if HUB_MOUSE else [
        "-device", "qemu-xhci,id=xhci",
        "-device", "usb-kbd,bus=xhci.0",
        "-device", "usb-tablet,bus=xhci.0",
    ])
    vm = subprocess.Popen([
        os.environ.get("QEMU", "qemu-system-x86_64"),
        "-M", "q35,i8042=off", "-cpu", os.environ.get("QEMU_CPU", "qemu64,apic=off"),
        "-m", "256M", "-cdrom", str(iso), "-boot", "d", "-vga", "std",
        "-drive", f"file={root_image},if=none,format=raw,id=boringdisk,readonly=on",
        "-device", "virtio-blk-pci,drive=boringdisk,disable-legacy=on",
    ] + usb_devices + [
        "-trace", "enable=usb_xhci_xfer_*",
        "-trace", "enable=usb_xhci_queue_event",
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

    def wait(predicate, description, timeout=75):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            current = text()
            if any(marker in current for marker in (
                    "M37 desktop FAILED", "boring-init: FAILED",
                    "Fatal exception", "syscall fatal", "display: FAILED",
                    "wm: FAILED", "boring-terminal: FAILED")):
                raise RuntimeError(
                    f"guest failure while waiting for {description}\n{current[-10000:]}")
            if vm.poll() is not None:
                raise RuntimeError(
                    f"QEMU exited ({vm.returncode}) while waiting for {description}")
            result = predicate(current)
            if result:
                return result
            time.sleep(0.04)
        raise RuntimeError(f"first missing witness: {description}\n{text()[-10000:]}")

    def witness(marker, count=1):
        return wait(lambda current: current.count(marker) >= count,
                    f"{marker} x{count}")

    def qmp(name, arguments=None):
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
        events.extend((QMP["key_event"](code, True),
                       QMP["key_event"](code, False)))
        if super_key:
            events.append(QMP["key_event"]("meta_l", False))
        qmp("input-send-event", {"events": events})
        time.sleep(0.025)

    def usb_inject(events):
        qmp("input-send-event", {"events": events})
        time.sleep(0.12)

    def usb_abs(axis, value):
        return {"type": "abs", "data": {"axis": axis, "value": value}}

    def usb_rel(axis, value):
        return {"type": "rel", "data": {"axis": axis, "value": value}}

    def usb_button(name, down):
        return {"type": "btn", "data": {"down": down, "button": name}}

    def frames():
        return WM["frames"](text())

    def latest(count=None, after=0):
        return wait(lambda _current: next((frame for frame in reversed(frames())
                                           if frame["frame"] > after and
                                           (count is None or frame["count"] == count)), None),
                    f"WM frame count={count} after={after}")

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

    def capture(name, frame, mode):
        geometry = re.search(r"boring-framebuffer: (\d+)x(\d+)x(?:24|32)", text())
        if geometry is None:
            raise RuntimeError("missing framebuffer geometry")
        width, height = map(int, geometry.groups())
        qmp("stop")
        try:
            ppm = OUT / f"{name}.ppm"
            qmp("screendump", {"filename": str(ppm)})
            cursor_x = width // 2 + 23 if HUB_MOUSE else width - 1
            cursor_y = height // 2 - 17 if HUB_MOUSE else height - 1
            meta = dict(frame, width=width, height=height,
                        cursor_x=cursor_x, cursor_y=cursor_y)
            (OUT / f"{name}.json").write_text(json.dumps(meta, indent=2) + "\n")
            decoded = TERM["validate"](ppm, meta, mode)
            (OUT / f"{name}.txt").write_text("\n".join(
                f"pid {pid}\n" + "\n".join(rows)
                for pid, rows in sorted(decoded.items())) + "\n")
        finally:
            qmp("cont")

    def decode_terminal(name, frame):
        geometry = re.search(r"boring-framebuffer: (\d+)x(\d+)x(?:24|32)", text())
        if geometry is None:
            raise RuntimeError("missing framebuffer geometry")
        width, height = map(int, geometry.groups())
        qmp("stop")
        try:
            ppm = OUT / f"{name}.ppm"
            qmp("screendump", {"filename": str(ppm)})
            cursor_x = width // 2 + 23 if HUB_MOUSE else width - 1
            cursor_y = height // 2 - 17 if HUB_MOUSE else height - 1
            meta = dict(frame, width=width, height=height,
                        cursor_x=cursor_x, cursor_y=cursor_y)
            return TERM["decode"](ppm, meta)
        finally:
            qmp("cont")

    def prompt_suffix(rows):
        marker = "boring@boringos:/$ "
        matches = [row.split(marker, 1)[1] for row in rows if marker in row]
        if len(matches) != 1:
            raise RuntimeError(f"expected one prompt row, got {matches!r}")
        return matches[0]

    def wait_prompt_suffix(name, frame, pid, predicate, description,
                           timeout=2.0):
        deadline = time.monotonic() + timeout
        last = None
        attempt = 0
        while time.monotonic() < deadline:
            rows = decode_terminal(f"{name}-{attempt}", frame)[pid]
            last = prompt_suffix(rows)
            if predicate(last):
                return last
            attempt += 1
            time.sleep(0.04)
        raise RuntimeError(f"timeout waiting for {description}: last={last!r}")

    def wait_m67_prompt_suffix(name, frame, pid, expected_suffix,
                                timeout=2.0):
        marker = "boring@boringos:/$"
        deadline = time.monotonic() + timeout
        last_matches = None
        attempt = 0
        while time.monotonic() < deadline:
            rows = decode_terminal(f"{name}-{attempt}", frame)[pid]
            matches = []
            for row in rows:
                if marker not in row:
                    continue
                tail = row.split(marker, 1)[1]
                if tail == "":
                    matches.append("")
                elif tail.startswith(" "):
                    matches.append(tail[1:])
                else:
                    matches.append(tail)
            last_matches = matches
            if len(matches) > 1:
                raise RuntimeError(
                    "M67 cleanup saw multiple prompt rows: "
                    f"expected={expected_suffix!r} matches={matches!r}")
            if len(matches) == 1 and matches[0] == expected_suffix:
                return matches[0]
            attempt += 1
            time.sleep(0.04)
        raise RuntimeError(
            "M67 cleanup prompt did not settle: "
            f"expected={expected_suffix!r} last_matches={last_matches!r}")

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
        current = text()
        display = re.findall(r"boring-init: desktop display spawned pid (\d+)", current)
        wm = re.findall(r"boring-init: desktop WM spawned pid (\d+)", current)
        if len(display) != 1 or len(wm) != 1:
            raise RuntimeError("missing unique PID1 desktop child identities")
        return int(display[0]), int(wm[0])

    def check_resources(frame):
        current = text()
        display_pid, wm_pid = startup_pids()
        cr3 = {int(pid): int(value, 16) for pid, value in re.findall(
            r"boring-spawn: child pid (\d+) cr3 (0x[0-9a-fA-F]+)", current)}
        init = re.search(
            r"m37-desktop: enter CPL3 pid (\d+) name \S+ cr3 (0x[0-9a-fA-F]+)",
            current)
        if init is None:
            raise RuntimeError("missing PID1 CR3 witness")
        cr3[int(init.group(1))] = int(init.group(2), 16)
        ptys = {int(pid): (int(slot), int(generation))
                for pid, slot, generation, _master, _slave in re.findall(
                    r"boring-pty: owner pid (\d+) slot (\d+) generation (\d+) master (\d+) slave (\d+)",
                    current)}
        buffers = {int(pid): (int(obj), int(size)) for pid, obj, size in re.findall(
            r"m36-buffer: owner pid (\d+) object (\d+) bytes (\d+)", current)}
        terminals = [dict(pid=tile["pid"], shell_pid=shell_for(tile["pid"]),
                          pty=ptys[tile["pid"]], buffer=buffers[tile["pid"]],
                          surface=tile["surface"], token=tile["token"])
                     for tile in frame["tiles"]]
        live = [1, display_pid, wm_pid] + [pid for terminal in terminals
                                          for pid in (terminal["pid"], terminal["shell_pid"])]
        if len(live) != 7 or len(set(live)) != 7:
            raise RuntimeError("M37 desktop process identities overlap")
        if len({cr3[pid] for pid in live}) != 7:
            raise RuntimeError("M37 desktop processes share an address space")
        if len({terminal["pty"] for terminal in terminals}) != 2:
            raise RuntimeError("M37 terminals share a PTY")
        if len({terminal["buffer"][0] for terminal in terminals}) != 2 or any(
                terminal["buffer"][1] != 800 * 600 * 4 for terminal in terminals):
            raise RuntimeError("M37 terminal shared buffers are not independent")
        if display_pid not in buffers or buffers[display_pid][0] in {
                terminal["buffer"][0] for terminal in terminals}:
            raise RuntimeError("display/terminal composition buffers overlap")
        if "boring-launch:" in current:
            raise RuntimeError("M37 desktop startup used legacy LAUNCH")
        if (f"boring-spawn: parent pid 1 child pid {display_pid}" not in current or
                f"boring-spawn: parent pid 1 child pid {wm_pid}" not in current):
            raise RuntimeError("display/WM were not PID1 SPAWN children")
        (OUT / "resources.json").write_text(json.dumps({
            "init_pid": 1, "display_pid": display_pid, "wm_pid": wm_pid,
            "live_pids": live, "terminals": terminals,
            "cr3": {str(pid): cr3[pid] for pid in live}}, indent=2) + "\n")

    try:
        witness("m37-desktop: real BoringFS root mounted")
        witness("m37-desktop: PID 1 scheduler task ready; desktop children must come from BoringFS")
        witness("boring-spawn: VFS executable source /bin/boring-display")
        witness("boring-spawn: VFS executable source /bin/boringwm")
        witness("display: M35 service and M31 input ready")
        witness("wm: boring.wm Ring3 policy ready; no pixel mappings")
        display_pid, wm_pid = startup_pids()
        if (display_pid, wm_pid) != (2, 3):
            raise RuntimeError(f"unexpected startup PIDs: display={display_pid} wm={wm_pid}")
        if HUB_MOUSE:
            witness("m66-desktop: q35 xHCI USB keyboard/hub mouse path online")
            usb_inject([usb_rel("x", 23), usb_rel("y", -17)])
            usb_inject([usb_button("left", True)])
            usb_inject([usb_button("left", False)])
            witness("display: M66 USB hub mouse movement reached Ring3 desktop")
            witness("display: M66 USB hub left button down reached Ring3 desktop")
            witness("display: M66 USB hub left button up reached Ring3 desktop")
        else:
            witness("m54-desktop: q35 i8042-free xHCI USB keyboard/tablet path online")
            # Baseline is decoded by M53 without producing a synthetic move;
            # the next real tablet report becomes the relative cursor event.
            usb_inject([usb_abs("x", 10000), usb_abs("y", 20000)])
            usb_inject([usb_abs("x", 12345), usb_abs("y", 23456)])
            usb_inject([usb_button("left", True)])
            usb_inject([usb_button("left", False)])
            witness("display: M54 USB tablet movement reached Ring3 desktop")
            witness("display: M54 USB left button down reached Ring3 desktop")
            witness("display: M54 USB left button up reached Ring3 desktop")

        key("ret", super_key=True)
        witness("wm: Super+Return spawned /bin/boring-terminal")
        witness("boring-spawn: VFS executable source /bin/boring-terminal")
        witness("boring-terminal: Ring3 managed client + PTY + boring-shell ready")
        witness("boring-spawn: VFS executable source /bin/boring-shell")
        prompt = latest(1)
        settled_capture("prompt", prompt, "prompt")
        terminal_a = prompt["tiles"][0]["pid"]

        if TYPEMATIC:
            type_text("abcdefghij")
            before_suffix = wait_prompt_suffix(
                "m67-before-repeat", prompt, terminal_a,
                lambda value: value == "abcdefghij",
                "all ten pre-repeat characters")

            # One physical/QMP key-down only. The guest PIT must create every
            # subsequent Backspace repeat until the single key-up below.
            qmp("input-send-event", {"events": [
                QMP["key_event"]("backspace", True)]})
            time.sleep(1.25)
            qmp("input-send-event", {"events": [
                QMP["key_event"]("backspace", False)]})
            time.sleep(0.20)

            after_screens = decode_terminal("m67-after-repeat", prompt)
            after_rows = after_screens[terminal_a]
            after_suffix = prompt_suffix(after_rows)
            if (not before_suffix.startswith(after_suffix) or
                    len(after_suffix) > len(before_suffix) - 3):
                raise RuntimeError(
                    "M67 held Backspace did not delete multiple characters: "
                    f"before={before_suffix!r} after={after_suffix!r}")
            print("M67 held Backspace deleted "
                  f"{len(before_suffix) - len(after_suffix)} characters "
                  "from one QMP key-down")

            cleared = after_suffix
            for remaining in range(len(after_suffix) - 1, -1, -1):
                key("backspace")
                expected_suffix = after_suffix[:remaining]
                cleared = wait_m67_prompt_suffix(
                    "m67-clear-line", prompt, terminal_a, expected_suffix)
            if cleared != "":
                raise RuntimeError(f"M67 line did not clear: {cleared!r}")

            launch_marker = "wm: Super+Return spawned /bin/boring-terminal"
            before_launches = text().count(launch_marker)
            qmp("input-send-event", {"events": [
                QMP["key_event"]("meta_l", True),
                QMP["key_event"]("ret", True)]})
            time.sleep(0.85)
            qmp("input-send-event", {"events": [
                QMP["key_event"]("ret", False),
                QMP["key_event"]("meta_l", False)]})
            witness(launch_marker, before_launches + 1)
            time.sleep(0.25)
            if text().count(launch_marker) != before_launches + 1:
                raise RuntimeError(
                    "M67 held Super+Return launched more than one terminal")

            dual = latest(2, prompt["frame"])
            focused_pid = next(tile["pid"] for tile in dual["tiles"]
                               if tile["token"] == dual["focus"])
            type_text("normal")
            wait_m67_prompt_suffix(
                "m67-ordinary-typing", dual, focused_pid, "normal")

            key("q", super_key=True)
            witness("boring-terminal: graceful cleanup complete")
            single = latest(1, dual["frame"])
            key("q", super_key=True)
            witness("boring-terminal: graceful cleanup complete", 2)
            witness("wm: session empty; clean exit")
            witness("boring-init: desktop WM exited status 0")
            witness("display: session drained; exiting with claims")
            witness("boring-init: desktop display exited status 0")
            witness("boring-init: desktop session drained")
            witness("m37-desktop: IPC/input/framebuffer/M32/PTY desktop resources drained")
            witness("m37-desktop: all spawned desktop tasks/processes reaped; PID 1 remains")
            witness("M54 USB-only graphical desktop acceptance passed.")
            witness("M37 native desktop session startup acceptance passed.")
            (OUT / "SUCCESS.txt").write_text(
                "M67 typematic + M54 USB-only desktop SUCCESS\n"
                "One QMP key-down held Backspace deleted multiple characters; "
                "held Super+Return launched exactly one terminal; ordinary typing "
                "and desktop teardown remained green.\n")
            print(f"M67 typematic USB desktop SUCCESS; evidence: {OUT}")
            return

        run_fetch(terminal_a)
        fetch_frame = latest(1, prompt["frame"] - 1)
        settled_capture("boringfetch", fetch_frame, "fetch")

        key("ret", super_key=True)
        witness("wm: Super+Return spawned /bin/boring-terminal", 2)
        witness("boring-terminal: Ring3 managed client + PTY + boring-shell ready", 2)
        dual = latest(2, fetch_frame["frame"])
        settled_capture("dual-ready", dual, "dual-ready")
        check_resources(dual)

        type_text("terminalb")
        settled_capture("dual-focused-b", dual, "dual-b")
        switch_anchor = dual
        if HUB_MOUSE:
            movement = "display: M66 USB hub mouse movement reached Ring3 desktop"
            movement_before = text().count(movement)
            focus_before = text().count("wm: action focus")
            focus_ready = "display: focus frame ready"
            focus_ready_before = text().count(focus_ready)
            started = time.monotonic()
            for index in range(16):
                usb_inject([usb_rel("x", 1 if index % 2 == 0 else -1)])
                witness(movement, movement_before + index + 1)
            elapsed = time.monotonic() - started
            witness("wm: action focus", focus_before + 1)
            witness(focus_ready, focus_ready_before + 1)
            pointer_focus = latest(2, dual["frame"])
            if pointer_focus["focus"] == dual["focus"]:
                raise RuntimeError("real USB mouse movement did not change pointer focus")
            (OUT / "mouse-latency.json").write_text(json.dumps({
                "injected_reports": 16,
                "observed_ring3_moves": 16,
                "elapsed_seconds_observational": elapsed,
                "expected_region_presents": 32,
                "expected_region_pixels": 2304,
                "expected_focus_border_transitions": 3,
                "expected_total_region_presents": 58,
                "expected_total_region_pixels": 37656,
                "pointer_focus_changed": True,
            }, indent=2) + "\n")
            focus_ready_before += 1
            key("j", super_key=True)
            witness(focus_ready, focus_ready_before + 1)
            restored = latest(2, pointer_focus["frame"])
            if restored["focus"] != dual["focus"]:
                raise RuntimeError("keyboard focus restore after pointer burst failed")
            switch_anchor = restored
            focus_ready_before += 1
        key("j", super_key=True)
        if HUB_MOUSE:
            witness(focus_ready, focus_ready_before + 1)
        switched = latest(2, switch_anchor["frame"])
        type_text("terminala")
        settled_capture("dual-focused-a", switched, "dual-a")

        key("q", super_key=True)
        witness("boring-terminal: CLOSE received")
        witness("boring-terminal: graceful cleanup complete")
        single = latest(1, switched["frame"])
        settled_capture("after-first-close", single, "single-after-close")

        key("q", super_key=True)
        witness("boring-terminal: CLOSE received", 2)
        witness("boring-terminal: graceful cleanup complete", 2)
        witness("wm: session empty; clean exit")
        witness("boring-init: desktop WM exited status 0")
        witness("display: session drained; exiting with claims")
        witness("boring-init: desktop display exited status 0")
        witness("boring-init: desktop session drained")
        witness("m37-desktop: IPC/input/framebuffer/M32/PTY desktop resources drained")
        witness("m37-desktop: all spawned desktop tasks/processes reaped; PID 1 remains")
        witness("M54 USB-only graphical desktop acceptance passed.")
        if HUB_MOUSE:
            stats = re.search(
                r"m66-desktop: framebuffer full/region/pixels=(\d+)/(\d+)/(\d+)",
                text())
            if (stats is None or int(stats.group(1)) < 1 or
                    int(stats.group(2)) != 58 or
                    int(stats.group(3)) - int(stats.group(1)) * 800 * 600 != 37656):
                raise RuntimeError(
                    "missing exact M66 cursor/focus region-present accounting")
            witness("M66 USB-hub mouse Ring3 desktop acceptance passed.")
        witness("M37 native desktop session startup acceptance passed.")
        label = ("M66 USB-hub mouse Ring3 desktop SUCCESS"
                 if HUB_MOUSE else "M54 USB-only graphical desktop SUCCESS")
        (OUT / "SUCCESS.txt").write_text(
            label + "\n"
            "PID1 SPAWN from persistent BoringFS -> display -> WM -> Super+Return terminal -> PTY shell -> boringfetch; dual focus/input; explicit display/WM waitpid drain with PID1 remaining.\n")
        print(f"{label}; evidence: {OUT}")
    except Exception as exc:
        if vm.poll() is None:
            try:
                qmp("stop")
                (OUT / "registers.txt").write_text(qmp(
                    "human-monitor-command", {"command-line": "info registers"}))
                qmp("screendump", {"filename": str(OUT / "failure.ppm")})
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
