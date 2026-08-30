#!/usr/bin/env python3
"""M61: UEFI boot and persistent BoringFS root on the same xHCI USB image."""
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

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/m61-bootable-persistent-usb"
IMAGE = ROOT / "build/boringos-m61-usb.img"
SHA_FILE = ROOT / "build/boringos-m61-usb.img.sha256"
ROOT_PAYLOAD = ROOT / "build/m61-bundle/boringos-root.img"
QMP = runpy.run_path(str(ROOT / "tests/qmp-input.py"))
WM = runpy.run_path(str(ROOT / "tests/validate-wm-screenshot.py"))
TERM = runpy.run_path(str(ROOT / "tests/validate-m36-terminal-screenshot.py"))
SECTOR = 512
ROOT_FIRST_LBA = 133120
ROOT_SECTORS = 32768
IMAGE_SECTORS = 196608
ROOT_START = ROOT_FIRST_LBA * SECTOR
ROOT_LENGTH = ROOT_SECTORS * SECTOR
IMAGE_LENGTH = IMAGE_SECTORS * SECTOR


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def hash_region(path, start, length):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        handle.seek(start)
        remaining = length
        while remaining:
            chunk = handle.read(min(1024 * 1024, remaining))
            if not chunk:
                raise RuntimeError("short image while hashing protected region")
            digest.update(chunk)
            remaining -= len(chunk)
    return digest.hexdigest()


def extract_root(path, destination):
    payload_size = ROOT_PAYLOAD.stat().st_size
    if payload_size > ROOT_LENGTH:
        raise RuntimeError("BoringFS payload exceeds M61 root slice")
    with path.open("rb") as source, destination.open("wb") as target:
        source.seek(ROOT_START)
        remaining = payload_size
        while remaining:
            chunk = source.read(min(1024 * 1024, remaining))
            if not chunk:
                raise RuntimeError("short image while extracting BoringFS payload")
            target.write(chunk)
            remaining -= len(chunk)


def kernel_version():
    text = (ROOT / "kernel/core/entry.c").read_text()
    match = re.search(r"BoringKernel 0\.0\.\d+-dev", text)
    if match is None:
        raise RuntimeError("unable to resolve current BoringKernel version witness")
    return match.group(0)


def firmware_files():
    code = Path(os.environ.get("OVMF_CODE", "/usr/share/OVMF/OVMF_CODE_4M.fd"))
    variables = Path(os.environ.get("OVMF_VARS", "/usr/share/OVMF/OVMF_VARS_4M.fd"))
    if not code.is_file() or not variables.is_file():
        raise RuntimeError("OVMF_CODE_4M.fd / OVMF_VARS_4M.fd unavailable")
    return code, variables


def run_session(name, expect_existing):
    out = OUT / name
    out.mkdir(parents=True)
    serial = out / "serial.log"
    serial.write_text("")
    qemu_log_path = out / "qemu.log"
    qemu_log = qemu_log_path.open("w")
    code, variables = firmware_files()
    vars_copy = out / "OVMF_VARS.fd"
    shutil.copyfile(variables, vars_copy)
    command = [
        os.environ.get("QEMU", "qemu-system-x86_64"),
        "-M", "q35,i8042=off", "-cpu", os.environ.get("QEMU_CPU", "qemu64,apic=off"),
        "-m", "512M",
        "-drive", f"if=pflash,format=raw,unit=0,readonly=on,file={code}",
        "-drive", f"if=pflash,format=raw,unit=1,file={vars_copy}",
        "-drive", f"if=none,id=m61usb,format=raw,file={IMAGE},cache=writeback",
        "-device", "qemu-xhci,id=xhci,p3=0",
        "-device", "usb-storage,bus=xhci.0,port=1,drive=m61usb,removable=on,bootindex=1",
        "-device", "usb-kbd,bus=xhci.0,port=2,id=m61kbd",
        "-device", "usb-tablet,bus=xhci.0,port=3,id=m61tablet",
        "-boot", "menu=off,strict=on",
        "-vga", "std", "-display", "none",
        "-serial", f"file:{serial}", "-monitor", "none", "-qmp", "stdio",
        "-trace", "enable=usb_xhci_slot_address",
        "-trace", "enable=usb_xhci_xfer_start",
        "-trace", "enable=usb_xhci_xfer_success",
        "-no-reboot", "-no-shutdown",
    ]
    (out / "qemu-command.json").write_text(json.dumps(command, indent=2) + "\n")
    forbidden = ("-cdrom", "-kernel")
    if any(item in command for item in forbidden):
        raise RuntimeError("M61 topology contains a forbidden alternate boot path")
    if sum(1 for item in command if item.startswith("if=none,id=m61usb")) != 1:
        raise RuntimeError("M61 topology must contain exactly one BoringOS USB disk")

    vm = subprocess.Popen(command, cwd=ROOT, stdin=subprocess.PIPE,
                          stdout=subprocess.PIPE, stderr=qemu_log, bufsize=0)
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

    def qemu_text():
        return qemu_log_path.read_text(errors="replace")

    def wait(predicate, description, timeout=120):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            current = text()
            if any(marker in current for marker in (
                    "M37 desktop FAILED", "boring-init: FAILED", "Fatal exception",
                    "syscall fatal", "display: FAILED", "wm: FAILED",
                    "boring-terminal: FAILED", "m61-root: FAIL CLOSED")):
                raise RuntimeError(f"guest failure while waiting for {description}\n{current[-16000:]}")
            if vm.poll() is not None:
                raise RuntimeError(f"QEMU exited ({vm.returncode}) while waiting for {description}")
            result = predicate(current)
            if result:
                return result
            time.sleep(0.04)
        raise RuntimeError(f"first missing witness: {description}\n{text()[-16000:]}")

    def witness(marker, count=1):
        return wait(lambda current: current.count(marker) >= count,
                    f"{marker} x{count}")

    def qmp(command_name, arguments=None):
        nonlocal qmp_ready
        if not qmp_ready:
            QMP["receive_message"](pipes)
            QMP["execute"](pipes, "qmp_capabilities")
            qmp_ready = True
        result = QMP["execute"](pipes, command_name, arguments)
        with (out / "qmp.log").open("a") as record:
            record.write(json.dumps({"command": command_name,
                                     "arguments": arguments,
                                     "result": result}) + "\n")
        return result

    def keyboard_transfer_state():
        trace = qemu_text()
        addressed = re.findall(
            r"usb_xhci_slot_address\s+slotid (\d+), port (\S+)", trace)
        keyboard_slots = [int(slot) for slot, port in addressed
                          if (port == "2") or port.endswith(".2")]
        if not keyboard_slots:
            return None
        slot = keyboard_slots[-1]
        starts = re.findall(
            r"usb_xhci_xfer_start\s+(0x[0-9A-Fa-f]+): slotid (\d+), "
            r"epid (\d+), streamid \d+", trace)
        interrupt = [(pointer, int(epid)) for pointer, slot_text, epid in starts
                     if int(slot_text) == slot and int(epid) != 1]
        epids = {epid for _pointer, epid in interrupt}
        if len(epids) != 1:
            return None
        endpoint = next(iter(epids))
        pointers = [pointer for pointer, epid in interrupt if epid == endpoint]
        if not pointers:
            return None
        return slot, endpoint, pointers

    def release_key(code, description):
        state = wait(lambda _current: keyboard_transfer_state(),
                     "QEMU xHCI keyboard slot/endpoint trace")
        slot, endpoint, pointers = state
        before_starts = len(pointers)
        outstanding = pointers[-1]
        success_pattern = re.compile(
            rf"usb_xhci_xfer_success\s+{re.escape(outstanding)}: len \d+")
        success_before = len(success_pattern.findall(qemu_text()))
        qmp("input-send-event", {"events": [QMP["key_event"](code, False)]})
        wait(lambda _current: len(success_pattern.findall(qemu_text())) >=
             success_before + 1,
             f"keyboard release xHCI success {description}")
        wait(lambda _current: (
            (current_state := keyboard_transfer_state()) is not None and
            current_state[0] == slot and current_state[1] == endpoint and
            len(current_state[2]) >= before_starts + 1),
            f"keyboard release guest rearm {description}")
        with (out / "keyboard-release-proof.log").open("a") as record:
            record.write(
                f"{description}: slot={slot} endpoint={endpoint} "
                f"completed={outstanding} rearm={before_starts + 1}\n")

    def key(code, super_key=False):
        events = []
        if super_key:
            events.append(QMP["key_event"]("meta_l", True))
        events.extend((QMP["key_event"](code, True), QMP["key_event"](code, False)))
        if super_key:
            events.append(QMP["key_event"]("meta_l", False))
        qmp("input-send-event", {"events": events})
        time.sleep(0.025)

    def usb_inject(events):
        qmp("input-send-event", {"events": events})
        time.sleep(0.12)

    def usb_abs(axis, value):
        return {"type": "abs", "data": {"axis": axis, "value": value}}

    def usb_button(name_value, down):
        return {"type": "btn", "data": {"down": down, "button": name_value}}

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
        codes = {" ": "spc", "-": "minus", ".": "dot", "/": "slash"}
        for char in value:
            frame = frames()[-1]
            terminal_pid = next(tile["pid"] for tile in frame["tiles"]
                                if tile["token"] == frame["focus"])
            marker = f"fd-read: pid {shell_for(terminal_pid)} fd 0 bytes 1"
            before = text().count(marker)
            code = codes.get(char, char)
            qmp("input-send-event", {"events": [QMP["key_event"](code, True)]})
            witness(marker, before + 1)
            release_key(code, repr(char))

    def command_line(value):
        type_text(value)
        frame = frames()[-1]
        terminal_pid = next(tile["pid"] for tile in frame["tiles"]
                            if tile["token"] == frame["focus"])
        shell_pid = shell_for(terminal_pid)
        stdin_marker = f"fd-read: pid {shell_pid} fd 0 bytes 1"
        newline_marker = f"fd-write: pid {shell_pid} fd 1 bytes 2"
        shell_write_pattern = re.compile(
            rf"fd-write: pid {shell_pid} fd 1 bytes \d+")
        terminal_read_pattern = re.compile(
            rf"fd-read: pid {terminal_pid} fd 3 bytes \d+")
        current = text()
        stdin_before = current.count(stdin_marker)
        newline_before = current.count(newline_marker)
        shell_writes_before = len(shell_write_pattern.findall(current))
        terminal_reads_before = len(terminal_read_pattern.findall(current))

        qmp("input-send-event", {"events": [QMP["key_event"]("ret", True)]})
        witness(stdin_marker, stdin_before + 1)
        release_key("ret", "Return")

        witness(newline_marker, newline_before + 1)
        wait(lambda current_text: len(shell_write_pattern.findall(current_text)) >=
             shell_writes_before + 2,
             f"shell pid {shell_pid} regenerated prompt after command")
        wait(lambda current_text: len(terminal_read_pattern.findall(current_text)) >=
             terminal_reads_before + 1,
             f"terminal pid {terminal_pid} consumed regenerated prompt")
        return frames()[-1]

    def cat_persisted():
        source_count = text().count("boring-spawn: VFS executable source /bin/cat")
        child_count = len(re.findall(
            r"boring-spawn: parent pid \d+ child pid (\d+) task \d+ foreground", text()))
        command_frame = command_line("cat m61.txt")
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
            ppm = out / "persisted-m61.ppm"
            qmp("screendump", {"filename": str(ppm)})
            meta = dict(frame, width=width, height=height,
                        cursor_x=width - 1, cursor_y=height - 1)
            (out / "persisted-m61.json").write_text(json.dumps(meta, indent=2) + "\n")
            TERM["decode"].__globals__["MAX_COLS"] = 160
            TERM["decode"].__globals__["MAX_ROWS"] = 96
            screens = TERM["decode"](ppm, meta)
            rows = next(iter(screens.values()))
            if not any(row.strip() == "survived-usb" for row in rows):
                raise ValueError(f"persisted bytes absent from terminal pixels: {rows}")
            (out / "persisted-m61.txt").write_text("\n".join(rows) + "\n")
        finally:
            qmp("cont")

    def capture_wallpaper():
        geometry = re.search(r"boring-framebuffer: (\d+)x(\d+)x(?:24|32)", text())
        if geometry is None:
            raise RuntimeError("missing framebuffer geometry")
        width, height = map(int, geometry.groups())
        if (width, height) != (800, 600):
            raise RuntimeError("M61 wallpaper acceptance requires exact 800x600 scanout")
        qmp("stop")
        try:
            ppm = out / "empty-desktop-wallpaper.ppm"
            qmp("screendump", {"filename": str(ppm)})
            actual_width, actual_height, actual = WM["parse_ppm"](ppm)
            if (actual_width, actual_height) != (width, height):
                raise ValueError("wallpaper screenshot geometry mismatch")
            expected = WM["desktop_background"](width, height)
            x, y, region_width, region_height = 590, 520, 205, 35
            actual_region = bytearray()
            expected_region = bytearray()
            for row in range(y, y + region_height):
                first = (row * width + x) * 3
                last = first + region_width * 3
                actual_region.extend(actual[first:last])
                expected_region.extend(expected[first:last])
            if actual_region != expected_region:
                raise ValueError("empty desktop does not contain exact boring by design wallpaper region")
            if len(set(actual_region)) < 16:
                raise ValueError("wallpaper region is not visually distinctive")
            (out / "wallpaper-proof.txt").write_text(
                "empty 800x600 desktop exact wallpaper logo region: PASS\n"
                f"region={x},{y} {region_width}x{region_height}\n"
                f"region-sha256={hashlib.sha256(actual_region).hexdigest()}\n")
        finally:
            qmp("cont")

    def settled_capture(frame):
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
        witness(kernel_version())
        witness("m54-desktop: q35 i8042-free xHCI USB keyboard/tablet path online")
        witness("m61-root: Mass Storage 08/06/50 usb0 registered through M21")
        witness("m61-root: descriptor-derived Bulk OUT=")
        witness("m61-root: usb0 sectors=196608 root first/count=133120/32768")
        witness("m61-root: writable BoringFS mounted from usb0 bounded region")
        witness("m61-root: AHCI/VirtIO/RAMFS fallback disabled")
        witness("m37-desktop: real BoringFS root mounted")
        witness("boring-spawn: VFS executable source /bin/boring-display")
        witness("boring-spawn: VFS executable source /bin/boringwm")
        witness("display: M35 service and M31 input ready")
        witness("wm: boring.wm Ring3 policy ready; no pixel mappings")
        latest(0)
        capture_wallpaper()
        current = text()
        if ("VirtIO block:" in current) or ("m57-desktop:" in current) or ("AHCI:" in current):
            raise RuntimeError("M61 USB-root mode entered a forbidden fallback path")

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
        witness("boring-spawn: VFS executable source /bin/boring-shell")
        latest(1)

        if expect_existing:
            command_line("cd persist")
            frame = cat_persisted()
            settled_capture(frame)
            (out / "SECOND_BOOT_RENDER_PROOF.txt").write_text(
                "persistent file -> userspace cat -> BoringTerminal pixels: survived-usb\n")
            command_line("write m61.txt survived-usb")
        else:
            command_line("mkdir persist")
            command_line("cd persist")
            command_line("touch m61.txt")
            command_line("write m61.txt survived-usb")
            frame = cat_persisted()
            settled_capture(frame)

        key("q", super_key=True)
        witness("boring-terminal: graceful cleanup complete")
        witness("boring-init: desktop session drained")
        witness("m61-root: BOT/read/write/flush=")
        witness("m61-root: canonical HID dropped=0")
        witness("M61 USB root persistence flush complete.")
        witness("M54 USB-only graphical desktop acceptance passed.")
        witness("M37 native desktop session startup acceptance passed.")
        final_text = text()
        if ("m61-root: FAIL CLOSED" in final_text or "VirtIO block:" in final_text or
                "m57-desktop:" in final_text or "AHCI:" in final_text):
            raise RuntimeError("M61 final log contains a forbidden root path")
        (out / "SUCCESS.txt").write_text(
            f"M61 {name} same-USB UEFI/root desktop SUCCESS after real flush\n")
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


def host_check(label, protected_prefix, protected_suffix):
    extracted = OUT / f"root-{label}.img"
    extract_root(IMAGE, extracted)
    checker = str(ROOT / "build/boringfsck")
    checked = subprocess.check_output([checker, str(extracted)])
    (OUT / f"boringfsck-{label}.txt").write_bytes(checked)
    if b"Status: VALID" not in checked:
        raise RuntimeError(f"host BoringFS validation failed {label}")
    persisted = subprocess.check_output(
        [checker, "--cat", "/persist/m61.txt", str(extracted)])
    (OUT / f"persist-{label}.exact-bytes").write_bytes(persisted)
    if persisted != b"survived-usb\n":
        raise RuntimeError(f"wrong persisted bytes {label}: {persisted!r}")
    prefix = hash_region(IMAGE, 0, ROOT_START)
    suffix_start = ROOT_START + ROOT_LENGTH
    suffix = hash_region(IMAGE, suffix_start, IMAGE_LENGTH - suffix_start)
    if prefix != protected_prefix or suffix != protected_suffix:
        raise RuntimeError(f"EFI/GPT/non-root region changed {label}")
    (OUT / f"protected-regions-{label}.json").write_text(json.dumps({
        "prefix_bytes": ROOT_START,
        "prefix_sha256": prefix,
        "root_start": ROOT_START,
        "root_length": ROOT_LENGTH,
        "suffix_start": suffix_start,
        "suffix_bytes": IMAGE_LENGTH - suffix_start,
        "suffix_sha256": suffix,
        "persist_exact_hex": persisted.hex(),
    }, indent=2) + "\n")


def run():
    if not IMAGE.is_file() or not SHA_FILE.is_file() or not ROOT_PAYLOAD.is_file():
        raise RuntimeError("run M61 build/image builder before acceptance")
    if IMAGE.stat().st_size != IMAGE_LENGTH:
        raise RuntimeError("unexpected M61 raw image size")
    expected_sha = SHA_FILE.read_text().split()[0]
    initial_sha = sha256(IMAGE)
    if expected_sha != initial_sha:
        raise RuntimeError("M61 SHA256 sidecar does not match fresh raw image")

    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)
    pristine = OUT / "pristine-before-acceptance.img"
    shutil.copyfile(IMAGE, pristine)
    protected_prefix = hash_region(IMAGE, 0, ROOT_START)
    suffix_start = ROOT_START + ROOT_LENGTH
    protected_suffix = hash_region(IMAGE, suffix_start, IMAGE_LENGTH - suffix_start)

    pre_root = OUT / "root-before-first.img"
    extract_root(IMAGE, pre_root)
    checker = str(ROOT / "build/boringfsck")
    checked = subprocess.check_output([checker, str(pre_root)])
    if b"Status: VALID" not in checked:
        raise RuntimeError("fresh embedded BoringFS root invalid")
    absent = subprocess.run([checker, "--cat", "/persist/m61.txt", str(pre_root)],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if absent.returncode == 0:
        raise RuntimeError("fresh image already contains /persist/m61.txt")
    (OUT / "fresh-root.txt").write_text(
        "BoringFS VALID; /persist/m61.txt absent before first boot\n")

    run_session("first-boot", False)
    after_first = sha256(IMAGE)
    if after_first == initial_sha:
        raise RuntimeError("first boot did not persist any USB-root mutation")
    host_check("after-first", protected_prefix, protected_suffix)

    run_session("second-boot", True)
    after_second = sha256(IMAGE)
    host_check("after-second", protected_prefix, protected_suffix)

    (OUT / "image-hashes.json").write_text(json.dumps({
        "fresh_sha256": initial_sha,
        "after_first_sha256": after_first,
        "after_second_sha256": after_second,
        "protected_prefix_sha256": protected_prefix,
        "protected_suffix_sha256": protected_suffix,
    }, indent=2) + "\n")
    (OUT / "SUCCESS.txt").write_text(
        "M61 BOOTABLE PERSISTENT USB SUCCESS\n"
        "UEFI boot, same usb0 BoringFS root, first write+flush, host exact bytes, "
        "same-image reboot terminal pixels, HID coexistence, and protected boot region passed.\n")

    shutil.copyfile(pristine, IMAGE)
    if sha256(IMAGE) != initial_sha:
        raise RuntimeError("failed to restore exact fresh flashable image")
    print(f"M61 bootable persistent USB passed; evidence: {OUT}")


if __name__ == "__main__":
    try:
        run()
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"m61-bootable-persistent-usb: {exc}")
        raise SystemExit(1)
