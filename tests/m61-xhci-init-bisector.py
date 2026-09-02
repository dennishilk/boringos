#!/usr/bin/env python3
from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parent.parent
XHCI = ROOT / "kernel/arch/x86_64/xhci.c"
HEADER = ROOT / "kernel/include/boring/xhci.h"
POST = ROOT / "tests/m61-physical-trace-kernel.sh"

xhci_source = XHCI.read_text()
header_source = HEADER.read_text()
post_source = POST.read_text()


def function_definition(name: str, source: str) -> str:
    signature = re.compile(rf"\b(?:bool|uint8_t)\s+{re.escape(name)}\s*\(")
    for match in signature.finditer(source):
        position = match.end()
        depth = 1
        while position < len(source) and depth:
            if source[position] == "(":
                depth += 1
            elif source[position] == ")":
                depth -= 1
            position += 1
        tail = source[position:].lstrip()
        if depth or not tail.startswith("{"):
            continue
        body_start = source.index("{", position)
        position = body_start + 1
        depth = 1
        while position < len(source) and depth:
            if source[position] == "{":
                depth += 1
            elif source[position] == "}":
                depth -= 1
            position += 1
        if depth:
            raise RuntimeError(f"incomplete function definition: {name}")
        return source[match.start():position]
    raise RuntimeError(f"function definition missing: {name}")


progress = {
    "XHCI_M61_PROGRESS_CONTROLLER_DISCOVERY": 0x88,
    "XHCI_M61_PROGRESS_BAR_VALIDATION": 0x89,
    "XHCI_M61_PROGRESS_PCI_ENABLE": 0x8A,
    "XHCI_M61_PROGRESS_MMIO_MAP": 0x8B,
    "XHCI_M61_PROGRESS_CAPABILITY_PARSE": 0x8C,
    "XHCI_M61_PROGRESS_LEGACY_HANDOFF": 0x8D,
    "XHCI_M61_PROGRESS_HALT_COMMAND": 0x8E,
    "XHCI_M61_PROGRESS_HALT_WAIT": 0x8F,
    "XHCI_M61_PROGRESS_RESET_COMMAND": 0xAC,
    "XHCI_M61_PROGRESS_RESET_WAIT": 0xAD,
    "XHCI_M61_PROGRESS_CONTROLLER_READY_WAIT": 0xAE,
    "XHCI_M61_PROGRESS_RINGS_SETUP": 0xAF,
    "XHCI_M61_PROGRESS_START_COMMAND": 0xB0,
    "XHCI_M61_PROGRESS_START_WAIT": 0xB1,
    "XHCI_M61_PROGRESS_PORT_SCAN": 0xB2,
}
reasons = {
    "XHCI_M61_FALSE_INVALID_STATE": 0xB3,
    "XHCI_M61_FALSE_NO_CONTROLLER": 0xB4,
    "XHCI_M61_FALSE_INVALID_BAR": 0xB5,
    "XHCI_M61_FALSE_PCI_ENABLE": 0xB6,
    "XHCI_M61_FALSE_MMIO_MAP": 0xB7,
    "XHCI_M61_FALSE_CAPABILITIES": 0xB8,
    "XHCI_M61_FALSE_LEGACY_HANDOFF": 0xB9,
    "XHCI_M61_FALSE_HALT": 0xBA,
    "XHCI_M61_FALSE_RESET": 0xBB,
    "XHCI_M61_FALSE_CONTROLLER_NOT_READY": 0xBC,
    "XHCI_M61_FALSE_RINGS_SETUP": 0xBD,
    "XHCI_M61_FALSE_START": 0xBE,
}
return_codes = {"false": 0xBF, "true": 0xFE}
new_codes = set(progress.values()) | set(reasons.values()) | set(return_codes.values())

# 80-87 and 92-99 are older physical-bisector claims even though their
# generators are separate from the current exact-head script. Keep them
# reserved along with every code emitted by the current candidate.
claimed = (
    set(range(0x61, 0x80)) |
    set(range(0x80, 0x88)) |
    set(range(0x90, 0x9A)) |
    set(range(0xA0, 0xAC)) |
    set(range(0xC0, 0xC4)) |
    set(range(0xD0, 0xE7)) |
    set(range(0xE7, 0xFE))
)
if len(new_codes) != 29 or new_codes & claimed:
    raise RuntimeError("new xHCI POST namespace is not unique and unclaimed")

assignments = {
    name: int(value, 16)
    for name, value in re.findall(
        r"^\s*(XHCI_M61_(?:PROGRESS|FALSE)_[A-Z0-9_]+)\s*=\s*"
        r"0x([0-9a-fA-F]{2})\s*,?\s*$",
        xhci_source,
        re.MULTILINE,
    )
}
if assignments != {**progress, **reasons}:
    raise RuntimeError("xHCI progress/false enum does not match the audited map")
if len(assignments.values()) != len(set(assignments.values())):
    raise RuntimeError("xHCI direct POST codes are not unique")

xhci_init = function_definition("xhci_init", xhci_source)
progress_positions = [
    xhci_init.find(f"XHCI_M61_PROGRESS({name});") for name in progress
]
if any(position < 0 for position in progress_positions):
    raise RuntimeError("xHCI major progress breadcrumb is missing")
if progress_positions != sorted(progress_positions):
    raise RuntimeError("xHCI major progress breadcrumbs are reordered")

for index, name in enumerate(reasons):
    macro = "XHCI_M61_RETURN_FALSE" if index == 0 else "XHCI_M61_FAIL"
    invocation = f"{macro}({name});"
    if xhci_init.count(invocation) != 1:
        raise RuntimeError(f"xHCI false path is not classified exactly once: {name}")
if len(re.findall(r"XHCI_M61_(?:RETURN_FALSE|FAIL)\(", xhci_init)) != 12:
    raise RuntimeError("xHCI init does not contain exactly 12 classified false exits")

for forbidden in (
    "serial_write", "framebuffer", "delay", "sleep", "retry",
):
    if forbidden in xhci_init.lower():
        raise RuntimeError(f"xHCI diagnostic path gained forbidden behavior: {forbidden}")
if "#define XHCI_WAIT_LIMIT 10000000U" not in xhci_source:
    raise RuntimeError("xHCI wait limit changed")
if "#define XHCI_EVENT_WAIT_LIMIT 10000000U" not in xhci_source:
    raise RuntimeError("xHCI event wait limit changed")

gate = re.search(
    r"#ifdef BORING_M61_PHYSICAL_BREADCRUMBS\s*"
    r"#include <boring/io.h>[\s\S]*?"
    r"uint8_t boring_m61_xhci_failure_reason\(void\)[\s\S]*?"
    r"#else[\s\S]*?#define XHCI_M61_BEGIN\(\) do \{ \} while \(0\)"
    r"[\s\S]*?#endif",
    xhci_source,
)
if gate is None:
    raise RuntimeError("xHCI breadcrumbs/state/accessor are not candidate-gated")
if not re.search(
    r"#ifdef BORING_M61_PHYSICAL_BREADCRUMBS\s*"
    r"uint8_t boring_m61_xhci_failure_reason\(void\);\s*#endif",
    header_source,
):
    raise RuntimeError("xHCI failure-reason accessor declaration is not gated")

wrapper = function_definition("m61_post_xhci_init", post_source)
required_wrapper = (
    "M61_POST(M61_POST_XHCI_INIT_CALL);",
    "result = __real_xhci_init(state);",
    "M61_POST(M61_POST_XHCI_INIT_RETURNED_FALSE);",
    "failure_reason = boring_m61_xhci_failure_reason();",
    "M61_POST(failure_reason);",
    "M61_POST(M61_POST_XHCI_INIT_RETURNED_TRUE);",
)
wrapper_positions = [wrapper.find(item) for item in required_wrapper]
if any(position < 0 for position in wrapper_positions):
    raise RuntimeError("xHCI wrapper return/replay observation is incomplete")
if wrapper_positions != sorted(wrapper_positions):
    raise RuntimeError("xHCI wrapper return/replay observation is reordered")
true_position = wrapper.find("M61_POST(M61_POST_XHCI_INIT_RETURNED_TRUE);")
if "failure_reason" in wrapper[true_position:]:
    raise RuntimeError("xHCI success path replays a failure reason")

for name, value in (
    ("M61_POST_XHCI_INIT_CALL", 0xE9),
    ("M61_POST_XHCI_ADDRESS_CALL", 0xEA),
    ("M61_POST_XHCI_INIT_RETURNED_FALSE", return_codes["false"]),
    ("M61_POST_XHCI_INIT_RETURNED_TRUE", return_codes["true"]),
):
    matches = re.findall(
        rf"^\s*{name}\s*=\s*0x([0-9a-fA-F]{{2}})\s*,?\s*$",
        post_source,
        re.MULTILINE,
    )
    if matches != [f"{value:02x}"]:
        raise RuntimeError(f"M61 POST meaning changed or duplicated: {name}")

compile_flags = [
    "-Ikernel/include", "-Ilibs/boringfs/include", "-DBORING_TEST_MODE=22",
    "-std=c11", "-ffreestanding", "-fno-stack-protector", "-fno-pic",
    "-fno-pie", "-fno-builtin", "-m64", "-mno-red-zone", "-O2",
    "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-Wconversion",
    "-Wshadow", "-Wstrict-prototypes", "-Wmissing-prototypes",
]
with tempfile.TemporaryDirectory(prefix="m61-xhci-bisector-") as tmp:
    tmp_path = Path(tmp)
    normal = tmp_path / "xhci-normal.o"
    candidate = tmp_path / "xhci-m61.o"
    subprocess.run(
        ["cc", *compile_flags, "-c", str(XHCI), "-o", str(normal)],
        cwd=ROOT,
        check=True,
    )
    subprocess.run(
        ["cc", *compile_flags, "-DBORING_M61_PHYSICAL_BREADCRUMBS=1",
         "-c", str(XHCI), "-o", str(candidate)],
        cwd=ROOT,
        check=True,
    )
    normal_nm = subprocess.check_output(["nm", str(normal)], text=True)
    candidate_nm = subprocess.check_output(["nm", str(candidate)], text=True)
    if "boring_m61_xhci_failure_reason" in normal_nm:
        raise RuntimeError("non-M61 object contains xHCI diagnostic state/accessor")
    if "boring_m61_xhci_failure_reason" not in candidate_nm:
        raise RuntimeError("M61 object lacks xHCI failure-reason accessor")
    normal_disassembly = subprocess.check_output(
        ["objdump", "-d", "--disassemble=xhci_init", str(normal)], text=True
    )
    candidate_disassembly = subprocess.check_output(
        ["objdump", "-d", "--disassemble=xhci_init", str(candidate)], text=True
    )
    if re.search(r"\bout\b", normal_disassembly):
        raise RuntimeError("non-M61 xHCI init contains a POST output")
    if len(re.findall(r"\bout\b", candidate_disassembly)) < 27:
        raise RuntimeError("M61 xHCI binary lacks direct progress/reason outputs")

print("M61 xHCI init bisector source/codegen acceptance: PASS")
print("M61 xHCI progress: 88-8F AC-B2")
print("M61 xHCI false reasons: B3-BE; BF returned-false; FE returned-true")
