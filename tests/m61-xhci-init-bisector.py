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
ring_reasons = {
    "XHCI_M61_RINGS_FALSE_SCRATCHPAD_UNSUPPORTED": 0x40,
    "XHCI_M61_RINGS_FALSE_DCBAA_PMM": 0x41,
    "XHCI_M61_RINGS_FALSE_DCBAA_HHDM": 0x42,
    "XHCI_M61_RINGS_FALSE_COMMAND_RING_PMM": 0x43,
    "XHCI_M61_RINGS_FALSE_COMMAND_RING_HHDM": 0x44,
    "XHCI_M61_RINGS_FALSE_EVENT_RING_PMM": 0x45,
    "XHCI_M61_RINGS_FALSE_EVENT_RING_HHDM": 0x46,
    "XHCI_M61_RINGS_FALSE_ERST_PMM": 0x47,
    "XHCI_M61_RINGS_FALSE_ERST_HHDM": 0x48,
    "XHCI_M61_RINGS_FALSE_DCBAAP_READBACK": 0x49,
    "XHCI_M61_RINGS_FALSE_SCRATCHPAD_COUNT_BOUND": 0x58,
    "XHCI_M61_RINGS_FALSE_SCRATCHPAD_PAGE_SIZE": 0x59,
    "XHCI_M61_RINGS_FALSE_SCRATCHPAD_ARRAY_PMM": 0x5A,
    "XHCI_M61_RINGS_FALSE_SCRATCHPAD_ARRAY_HHDM": 0x5B,
    "XHCI_M61_RINGS_FALSE_SCRATCHPAD_BUFFER_PMM": 0x5C,
    "XHCI_M61_RINGS_FALSE_SCRATCHPAD_BUFFER_HHDM": 0x5D,
}
ring_progress = {
    "XHCI_M61_RINGS_PROGRESS_DCBAA_ALLOCATION": 0x4A,
    "XHCI_M61_RINGS_PROGRESS_COMMAND_RING_ALLOCATION": 0x4B,
    "XHCI_M61_RINGS_PROGRESS_EVENT_RING_ALLOCATION": 0x4C,
    "XHCI_M61_RINGS_PROGRESS_ERST_ALLOCATION": 0x4D,
    "XHCI_M61_RINGS_PROGRESS_SOFTWARE_INITIALIZATION": 0x4E,
    "XHCI_M61_RINGS_PROGRESS_DCBAAP_WRITE": 0x4F,
    "XHCI_M61_RINGS_PROGRESS_CRCR_WRITE": 0x50,
    "XHCI_M61_RINGS_PROGRESS_CONFIG_WRITE": 0x51,
    "XHCI_M61_RINGS_PROGRESS_IMAN_WRITE": 0x52,
    "XHCI_M61_RINGS_PROGRESS_ERSTSZ_WRITE": 0x53,
    "XHCI_M61_RINGS_PROGRESS_ERSTBA_WRITE": 0x54,
    "XHCI_M61_RINGS_PROGRESS_ERDP_WRITE": 0x55,
    "XHCI_M61_RINGS_PROGRESS_DCBAAP_READBACK": 0x56,
    "XHCI_M61_RINGS_SUCCESS": 0x57,
}
new_codes = (
    set(progress.values()) | set(reasons.values()) | set(return_codes.values()) |
    set(ring_reasons.values()) | set(ring_progress.values())
)

# 30-33 identify faults during the first real framebuffer store. 80-87 and
# 92-99 are older physical-bisector claims even though their generators are
# separate from the current exact-head script. Keep them reserved along with
# every code emitted by the current candidate.
claimed = (
    set(range(0x30, 0x34)) |
    set(range(0x61, 0x80)) |
    set(range(0x80, 0x88)) |
    set(range(0x90, 0x9A)) |
    set(range(0xA0, 0xAC)) |
    set(range(0xC0, 0xC4)) |
    set(range(0xD0, 0xE7)) |
    set(range(0xE7, 0xFE))
)
if len(new_codes) != 59 or new_codes & claimed:
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

ring_assignments = {
    name: int(value, 16)
    for name, value in re.findall(
        r"^\s*(XHCI_M61_RINGS_(?:(?:PROGRESS|FALSE)_[A-Z0-9_]+|SUCCESS))"
        r"\s*=\s*0x([0-9a-fA-F]{2})\s*,?\s*$",
        xhci_source,
        re.MULTILINE,
    )
}
if ring_assignments != {**ring_reasons, **ring_progress}:
    raise RuntimeError("xHCI rings POST enum does not match the audited map")
if set(ring_assignments.values()) & set(assignments.values()):
    raise RuntimeError("xHCI rings POST codes collide with existing xHCI meanings")

xhci_init = function_definition("xhci_init", xhci_source)
progress_positions = [
    xhci_init.find(f"XHCI_M61_PROGRESS({name});") for name in progress
]
if any(position < 0 for position in progress_positions):
    raise RuntimeError("xHCI major progress breadcrumb is missing")
if progress_positions != sorted(progress_positions):
    raise RuntimeError("xHCI major progress breadcrumbs are reordered")

for index, name in enumerate(reasons):
    if index == 0:
        macro = "XHCI_M61_RETURN_FALSE"
    elif name == "XHCI_M61_FALSE_RINGS_SETUP":
        macro = "XHCI_M61_FAIL_PRESERVING_REASON"
    else:
        macro = "XHCI_M61_FAIL"
    invocation = f"{macro}({name});"
    if xhci_init.count(invocation) != 1:
        raise RuntimeError(f"xHCI false path is not classified exactly once: {name}")
if len(re.findall(
        r"XHCI_M61_(?:RETURN_FALSE|FAIL|FAIL_PRESERVING_REASON)\(",
        xhci_init)) != 12:
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

frame_alloc = function_definition("frame_alloc_zero", xhci_source)
rings = function_definition("rings_initialize", xhci_source)

frame_failure_sequence = (
    "xhci_m61_frame_alloc_failure = XHCI_M61_FRAME_ALLOC_NONE;",
    "pmm_alloc_frame_in_range(0ULL, XHCI_DMA_32BIT_LIMIT, physical)",
    "xhci_m61_frame_alloc_failure = XHCI_M61_FRAME_ALLOC_PMM;",
    "vmm_pmm_frame_to_hhdm(*physical, virtual_address)",
    "xhci_m61_frame_alloc_failure = XHCI_M61_FRAME_ALLOC_HHDM;",
    "(void)pmm_free_frame(*physical);",
    "*physical = 0ULL;",
)
frame_failure_positions = [frame_alloc.find(item) for item in frame_failure_sequence]
if any(position < 0 for position in frame_failure_positions):
    raise RuntimeError("frame allocator PMM/HHDM subreason path is incomplete")
if frame_failure_positions != sorted(frame_failure_positions):
    raise RuntimeError("frame allocator failure/free semantics were reordered")
if not re.search(
    r"#ifdef BORING_M61_PHYSICAL_BREADCRUMBS\s*"
    r"xhci_m61_frame_alloc_failure = XHCI_M61_FRAME_ALLOC_NONE;\s*#endif",
    frame_alloc,
):
    raise RuntimeError("frame allocator diagnostic state reset is not gated")
for subreason in ("PMM", "HHDM"):
    if not re.search(
        rf"#ifdef BORING_M61_PHYSICAL_BREADCRUMBS\s*"
        rf"xhci_m61_frame_alloc_failure = XHCI_M61_FRAME_ALLOC_{subreason};"
        rf"\s*#endif",
        frame_alloc,
    ):
        raise RuntimeError(f"frame allocator {subreason} subreason is not gated")

allocation_fields = (
    "dcbaa_physical", "command_ring_physical", "event_ring_physical",
    "erst_physical",
)
candidate_start = rings.find("#ifdef BORING_M61_PHYSICAL_BREADCRUMBS")
candidate_end = rings.find("#else", candidate_start)
normal_end = rings.find("#endif", candidate_end)
if min(candidate_start, candidate_end, normal_end) < 0:
    raise RuntimeError("ring allocation diagnostic split is incomplete")
candidate_allocations = rings[candidate_start:candidate_end]
normal_allocations = rings[candidate_end:normal_end]
allocation_pattern = r"frame_alloc_zero\(&state->([a-z_]+),"
if tuple(re.findall(allocation_pattern, candidate_allocations)) != allocation_fields:
    raise RuntimeError("candidate DMA allocation order changed")
if tuple(re.findall(allocation_pattern, normal_allocations)) != allocation_fields:
    raise RuntimeError("non-M61 DMA allocation order changed")
if " ||\n" not in normal_allocations or "return false;" not in normal_allocations:
    raise RuntimeError("non-M61 chained ring allocation behavior changed")

allocation_reason_pairs = {
    "dcbaa_physical": (
        "XHCI_M61_RINGS_FALSE_DCBAA_PMM",
        "XHCI_M61_RINGS_FALSE_DCBAA_HHDM",
    ),
    "command_ring_physical": (
        "XHCI_M61_RINGS_FALSE_COMMAND_RING_PMM",
        "XHCI_M61_RINGS_FALSE_COMMAND_RING_HHDM",
    ),
    "event_ring_physical": (
        "XHCI_M61_RINGS_FALSE_EVENT_RING_PMM",
        "XHCI_M61_RINGS_FALSE_EVENT_RING_HHDM",
    ),
    "erst_physical": (
        "XHCI_M61_RINGS_FALSE_ERST_PMM",
        "XHCI_M61_RINGS_FALSE_ERST_HHDM",
    ),
}
for index, field in enumerate(allocation_fields):
    start = candidate_allocations.find(f"frame_alloc_zero(&state->{field}")
    end = (
        candidate_allocations.find(
            f"frame_alloc_zero(&state->{allocation_fields[index + 1]}", start
        )
        if index + 1 < len(allocation_fields)
        else len(candidate_allocations)
    )
    allocation_failure = candidate_allocations[start:end]
    pmm_reason, hhdm_reason = allocation_reason_pairs[field]
    if (start < 0 or pmm_reason not in allocation_failure or
            hhdm_reason not in allocation_failure or
            "XHCI_M61_RINGS_ALLOC_RETURN_FALSE" not in allocation_failure):
        raise RuntimeError(f"ring allocation is not exactly classified: {field}")

if re.search(
    r"scratchpad_count\s*!=\s*0U\)\s*\{\s*"
    r"XHCI_M61_RETURN_FALSE\(\s*"
    r"XHCI_M61_RINGS_FALSE_SCRATCHPAD_UNSUPPORTED\);",
    rings,
):
    raise RuntimeError("legacy non-zero scratchpad rejection still exists")
for witness in (
    "#define XHCI_SCRATCHPAD_MAX_SUPPORTED 512U",
    "XHCI_PAGESIZE_4K",
    "scratchpad_array_physical",
    "scratchpad_buffer_physical[XHCI_SCRATCHPAD_MAX_SUPPORTED]",
    "scratchpads_initialize(base, operational, capabilities)",
    "runtime_state.dcbaa[0] = runtime_state.scratchpad_array_physical;",
    "scratchpads_release();",
):
    if witness not in xhci_source:
        raise RuntimeError(f"scratchpad implementation witness missing: {witness}")
if "XHCI_M61_RINGS_FALSE_SCRATCHPAD_UNSUPPORTED = 0x40" not in xhci_source:
    raise RuntimeError("frozen scratchpad-unsupported POST meaning 40 was removed")
if "pmm_alloc_frame_in_range(0ULL, XHCI_DMA_32BIT_LIMIT, physical)" not in frame_alloc:
    raise RuntimeError("scratchpad DMA path no longer inherits the 32-bit frame policy")

software_events = (
    "XHCI_M61_PROGRESS(XHCI_M61_RINGS_PROGRESS_SOFTWARE_INITIALIZATION);",
    "command[XHCI_COMMAND_RING_USABLE].parameter",
    "command[XHCI_COMMAND_RING_USABLE].status",
    "command[XHCI_COMMAND_RING_USABLE].control",
    "erst->ring_base",
    "erst->ring_size",
    "erst->reserved",
    "memory_barrier();",
    "XHCI_M61_PROGRESS(XHCI_M61_RINGS_PROGRESS_DCBAAP_WRITE);",
)
software_positions = [rings.find(item) for item in software_events]
if any(position < 0 for position in software_positions):
    raise RuntimeError("software ring/ERST initialization witness is incomplete")
if software_positions != sorted(software_positions):
    raise RuntimeError("software ring/ERST initialization was reordered")

mmio_events = (
    ("XHCI_M61_RINGS_PROGRESS_DCBAAP_WRITE",
     "mmio_write64(base, operational + 0x30U, state->dcbaa_physical)"),
    ("XHCI_M61_RINGS_PROGRESS_CRCR_WRITE",
     "mmio_write64(base, operational + 0x18U,"),
    ("XHCI_M61_RINGS_PROGRESS_CONFIG_WRITE",
     "mmio_write32(base, operational + 0x38U,"),
    ("XHCI_M61_RINGS_PROGRESS_IMAN_WRITE",
     "mmio_write32(base, interrupter + 0x00U, 0U)"),
    ("XHCI_M61_RINGS_PROGRESS_ERSTSZ_WRITE",
     "mmio_write32(base, interrupter + 0x08U, 1U)"),
    ("XHCI_M61_RINGS_PROGRESS_ERSTBA_WRITE",
     "mmio_write64(base, interrupter + 0x10U, state->erst_physical)"),
    ("XHCI_M61_RINGS_PROGRESS_ERDP_WRITE",
     "mmio_write64(base, interrupter + 0x18U, state->event_ring_physical)"),
)
last_position = -1
for progress_name, write_call in mmio_events:
    progress_position = rings.find(f"XHCI_M61_PROGRESS({progress_name});")
    write_position = rings.find(write_call, progress_position)
    if progress_position <= last_position or write_position <= progress_position:
        raise RuntimeError(f"MMIO write witness/order changed: {progress_name}")
    last_position = write_position

readback_sequence = (
    "runtime_state.outstanding_command_physical = 0ULL;",
    "XHCI_M61_PROGRESS(XHCI_M61_RINGS_PROGRESS_DCBAAP_READBACK);",
    "mmio_read64(base, operational + 0x30U) != state->dcbaa_physical",
    "XHCI_M61_RETURN_FALSE(XHCI_M61_RINGS_FALSE_DCBAAP_READBACK);",
    "XHCI_M61_PROGRESS(XHCI_M61_RINGS_SUCCESS);",
    "return true;",
)
readback_positions = [rings.find(item) for item in readback_sequence]
if any(position < 0 for position in readback_positions):
    raise RuntimeError("DCBAAP readback failure/success split is incomplete")
if readback_positions != sorted(readback_positions):
    raise RuntimeError("DCBAAP readback failure/success split is reordered")
if "XHCI_M61_FAIL_PRESERVING_REASON(XHCI_M61_FALSE_RINGS_SETUP);" not in xhci_init:
    raise RuntimeError("generic BD no longer preserves the exact ring reason")

cleanup_events = (
    "rings_release(&active_state);",
    "state_clear(&active_state);",
    "runtime_clear();",
)
ring_release_start = xhci_source.find("static void rings_release(struct xhci_state *state)")
ring_release_end = xhci_source.find("static bool rings_initialize", ring_release_start)
if min(ring_release_start, ring_release_end) < 0:
    raise RuntimeError("central xHCI ring release helper is missing")
ring_release = xhci_source[ring_release_start:ring_release_end]
release_events = (
    "scratchpads_release();",
    "frame_release(state->erst_physical);",
    "frame_release(state->event_ring_physical);",
    "frame_release(state->command_ring_physical);",
    "frame_release(state->dcbaa_physical);",
    "runtime_state.dcbaa = NULL;",
    "runtime_state.command_ring = NULL;",
    "runtime_state.event_ring = NULL;",
)
release_positions = [ring_release.find(item) for item in release_events]
if any(position < 0 for position in release_positions) or release_positions != sorted(release_positions):
    raise RuntimeError("scratchpad/ring cleanup order is incomplete or reordered")
cleanup_start = xhci_init.rfind("if (!success) {")
cleanup_source = xhci_init[cleanup_start:]
cleanup_positions = [cleanup_source.find(item) for item in cleanup_events]
if any(position < 0 for position in cleanup_positions):
    raise RuntimeError("xHCI failed-ring cleanup is incomplete")
if cleanup_positions != sorted(cleanup_positions):
    raise RuntimeError("xHCI failed-ring cleanup order changed")

for forbidden in ("delay", "sleep", "retry"):
    if forbidden in rings.lower():
        raise RuntimeError(f"xHCI ring diagnostic gained forbidden behavior: {forbidden}")

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
    if len(re.findall(r"\bout\b", candidate_disassembly)) < 47:
        raise RuntimeError("M61 xHCI binary lacks ring progress/reason outputs")


# Execute the real static ring/scratchpad implementation against a bounded
# fake PMM/HHDM/MMIO model. This covers zero/one/multiple scratchpads,
# alignment/DMA bounds, partial failures, later ring failures, and cleanup.
harness_source = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "kernel/arch/x86_64/xhci.c"

#define MOCK_FRAMES 600U
_Alignas(4096) static uint8_t mock_pages[MOCK_FRAMES][4096];
static bool mock_used[MOCK_FRAMES];
static uint32_t mock_alloc_calls;
static uint32_t mock_hhdm_calls;
static uint32_t mock_fail_alloc_call;
static uint32_t mock_fail_hhdm_call;

static uint32_t frame_index(uint64_t physical) {
    assert((physical & 0xfffULL) == 0ULL);
    assert(physical >= 0x1000ULL);
    assert(physical < XHCI_DMA_32BIT_LIMIT);
    return (uint32_t)(physical / 0x1000ULL) - 1U;
}

bool pmm_alloc_frame_in_range(uint64_t minimum_physical_address,
                              uint64_t maximum_physical_address_exclusive,
                              uint64_t *physical_address) {
    uint32_t index;
    assert(minimum_physical_address == 0ULL);
    assert(maximum_physical_address_exclusive == XHCI_DMA_32BIT_LIMIT);
    assert(physical_address != NULL);
    ++mock_alloc_calls;
    if ((mock_fail_alloc_call != 0U) &&
        (mock_alloc_calls == mock_fail_alloc_call)) {
        return false;
    }
    for (index = 0U; index < MOCK_FRAMES; ++index) {
        if (!mock_used[index]) {
            mock_used[index] = true;
            *physical_address = ((uint64_t)index + 1ULL) * 0x1000ULL;
            return true;
        }
    }
    return false;
}

bool pmm_free_frame(uint64_t physical_address) {
    const uint32_t index = frame_index(physical_address);
    assert(index < MOCK_FRAMES);
    assert(mock_used[index]);
    mock_used[index] = false;
    return true;
}

bool vmm_pmm_frame_to_hhdm(uint64_t physical_address, void **virtual_address) {
    const uint32_t index = frame_index(physical_address);
    ++mock_hhdm_calls;
    if ((mock_fail_hhdm_call != 0U) &&
        (mock_hhdm_calls == mock_fail_hhdm_call)) {
        return false;
    }
    assert(index < MOCK_FRAMES);
    assert(mock_used[index]);
    *virtual_address = mock_pages[index];
    return true;
}

static void *mock_virtual(uint64_t physical) {
    const uint32_t index = frame_index(physical);
    assert(index < MOCK_FRAMES);
    assert(mock_used[index]);
    return mock_pages[index];
}

static uint32_t live_frames(void) {
    uint32_t index;
    uint32_t live = 0U;
    for (index = 0U; index < MOCK_FRAMES; ++index) {
        if (mock_used[index]) { ++live; }
    }
    return live;
}

static void reset_model(void) {
    memset(mock_pages, 0, sizeof(mock_pages));
    memset(mock_used, 0, sizeof(mock_used));
    mock_alloc_calls = 0U;
    mock_hhdm_calls = 0U;
    mock_fail_alloc_call = 0U;
    mock_fail_hhdm_call = 0U;
    state_clear(&active_state);
    runtime_clear();
}

static void setup_caps(struct xhci_capabilities *caps, uint16_t scratchpads) {
    memset(caps, 0, sizeof(*caps));
    caps->capability_length = 0x40U;
    caps->runtime_offset = 0x1000U;
    caps->max_slots = 8U;
    caps->scratchpad_count = scratchpads;
}

static void put32(uint8_t *base, uint32_t offset, uint32_t value) {
    memcpy(base + offset, &value, sizeof(value));
}

static uint64_t get64(const uint8_t *base, uint32_t offset) {
    uint64_t value;
    memcpy(&value, base + offset, sizeof(value));
    return value;
}

static void assert_dma_frame(uint64_t physical) {
    assert(physical != 0ULL);
    assert((physical & (PMM_PAGE_SIZE - 1ULL)) == 0ULL);
    assert(physical < XHCI_DMA_32BIT_LIMIT);
}

static void release_and_assert_clean(struct xhci_state *state) {
    rings_release(state);
    assert(live_frames() == 0U);
    assert(state->dcbaa_physical == 0ULL);
    assert(state->command_ring_physical == 0ULL);
    assert(state->event_ring_physical == 0ULL);
    assert(state->erst_physical == 0ULL);
    assert(runtime_state.scratchpad_array_physical == 0ULL);
    assert(runtime_state.scratchpad_count == 0U);
    assert(runtime_state.dcbaa == NULL);
}

static void test_zero_scratchpads(void) {
    _Alignas(8) uint8_t mmio[XHCI_MMIO_WINDOW_SIZE] = {0U};
    struct xhci_capabilities caps;
    struct xhci_state state;
    struct xhci_trb *command;
    struct xhci_erst_entry *erst;
    reset_model();
    state_clear(&state);
    setup_caps(&caps, 0U);
    /* PAGESIZE deliberately remains zero: zero-scratchpad behavior must not
       gain a new controller-page-size dependency. */
    assert(rings_initialize(mmio, &caps, &state));
    assert(mock_alloc_calls == 4U);
    assert(((uint64_t *)mock_virtual(state.dcbaa_physical))[0] == 0ULL);
    assert(runtime_state.scratchpad_count == 0U);
    assert(runtime_state.scratchpad_array_physical == 0ULL);
    command = (struct xhci_trb *)mock_virtual(state.command_ring_physical);
    assert(command[XHCI_COMMAND_RING_USABLE].parameter == state.command_ring_physical);
    assert((command[XHCI_COMMAND_RING_USABLE].control & XHCI_TRB_TYPE_SHIFT) != 0U);
    erst = (struct xhci_erst_entry *)mock_virtual(state.erst_physical);
    assert(erst->ring_base == state.event_ring_physical);
    assert(erst->ring_size == XHCI_EVENT_RING_TRBS);
    assert(get64(mmio, 0x40U + 0x30U) == state.dcbaa_physical);
    release_and_assert_clean(&state);
}

static void test_scratchpads(uint16_t count) {
    _Alignas(8) uint8_t mmio[XHCI_MMIO_WINDOW_SIZE] = {0U};
    struct xhci_capabilities caps;
    struct xhci_state state;
    uint64_t *dcbaa;
    uint64_t *array;
    uint16_t index;
    reset_model();
    state_clear(&state);
    setup_caps(&caps, count);
    put32(mmio, 0x40U + 0x08U, XHCI_PAGESIZE_4K);
    assert(rings_initialize(mmio, &caps, &state));
    assert(runtime_state.scratchpad_count == count);
    assert_dma_frame(runtime_state.scratchpad_array_physical);
    dcbaa = (uint64_t *)mock_virtual(state.dcbaa_physical);
    assert(dcbaa[0] == runtime_state.scratchpad_array_physical);
    array = (uint64_t *)mock_virtual(runtime_state.scratchpad_array_physical);
    for (index = 0U; index < count; ++index) {
        uint16_t previous;
        assert_dma_frame(array[index]);
        assert(array[index] == runtime_state.scratchpad_buffer_physical[index]);
        for (previous = 0U; previous < index; ++previous) {
            assert(array[index] != array[previous]);
        }
    }
    assert(get64(mmio, 0x40U + 0x30U) == state.dcbaa_physical);
    release_and_assert_clean(&state);
}

static void test_over_bound(void) {
    _Alignas(8) uint8_t mmio[XHCI_MMIO_WINDOW_SIZE] = {0U};
    struct xhci_capabilities caps;
    struct xhci_state state;
    reset_model();
    state_clear(&state);
    setup_caps(&caps, (uint16_t)(XHCI_SCRATCHPAD_MAX_SUPPORTED + 1U));
    put32(mmio, 0x40U + 0x08U, XHCI_PAGESIZE_4K);
    assert(!rings_initialize(mmio, &caps, &state));
    assert(mock_alloc_calls == 1U);
    release_and_assert_clean(&state);
}

static void test_page_size_fail_closed(void) {
    _Alignas(8) uint8_t mmio[XHCI_MMIO_WINDOW_SIZE] = {0U};
    struct xhci_capabilities caps;
    struct xhci_state state;
    reset_model();
    state_clear(&state);
    setup_caps(&caps, 1U);
    assert(!rings_initialize(mmio, &caps, &state));
    assert(mock_alloc_calls == 1U);
    release_and_assert_clean(&state);
}

static void test_array_allocation_failure(void) {
    _Alignas(8) uint8_t mmio[XHCI_MMIO_WINDOW_SIZE] = {0U};
    struct xhci_capabilities caps;
    struct xhci_state state;
    reset_model();
    state_clear(&state);
    setup_caps(&caps, 2U);
    put32(mmio, 0x40U + 0x08U, XHCI_PAGESIZE_4K);
    mock_fail_alloc_call = 2U;
    assert(!rings_initialize(mmio, &caps, &state));
    release_and_assert_clean(&state);
}

static void test_buffer_partial_failure(void) {
    _Alignas(8) uint8_t mmio[XHCI_MMIO_WINDOW_SIZE] = {0U};
    struct xhci_capabilities caps;
    struct xhci_state state;
    reset_model();
    state_clear(&state);
    setup_caps(&caps, 3U);
    put32(mmio, 0x40U + 0x08U, XHCI_PAGESIZE_4K);
    mock_fail_alloc_call = 4U;
    assert(!rings_initialize(mmio, &caps, &state));
    release_and_assert_clean(&state);
}

static void test_buffer_hhdm_failure(void) {
    _Alignas(8) uint8_t mmio[XHCI_MMIO_WINDOW_SIZE] = {0U};
    struct xhci_capabilities caps;
    struct xhci_state state;
    reset_model();
    state_clear(&state);
    setup_caps(&caps, 2U);
    put32(mmio, 0x40U + 0x08U, XHCI_PAGESIZE_4K);
    /* HHDM calls: DCBAA=1, array=2, buffer0=3, buffer1=4. */
    mock_fail_hhdm_call = 4U;
    assert(!rings_initialize(mmio, &caps, &state));
    release_and_assert_clean(&state);
}

static void test_late_ring_failure(void) {
    _Alignas(8) uint8_t mmio[XHCI_MMIO_WINDOW_SIZE] = {0U};
    struct xhci_capabilities caps;
    struct xhci_state state;
    reset_model();
    state_clear(&state);
    setup_caps(&caps, 2U);
    put32(mmio, 0x40U + 0x08U, XHCI_PAGESIZE_4K);
    /* Allocations: DCBAA, array, two buffers, command, event. */
    mock_fail_alloc_call = 6U;
    assert(!rings_initialize(mmio, &caps, &state));
    release_and_assert_clean(&state);
}

int main(void) {
    test_zero_scratchpads();
    test_scratchpads(1U);
    test_scratchpads(4U);
    test_over_bound();
    test_page_size_fail_closed();
    test_array_allocation_failure();
    test_buffer_partial_failure();
    test_buffer_hhdm_failure();
    test_late_ring_failure();
    return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="m61-xhci-scratchpad-") as tmp:
    tmp_path = Path(tmp)
    harness = tmp_path / "scratchpad-host.c"
    binary = tmp_path / "scratchpad-host"
    harness.write_text(harness_source)
    subprocess.run(
        ["cc", "-I.", "-Ikernel/include", "-Ilibs/boringfs/include",
         "-std=c11", "-ffunction-sections", "-fdata-sections", "-fno-pie",
         str(harness), "-Wl,--gc-sections", "-no-pie", "-o", str(binary)],
        cwd=ROOT,
        check=True,
    )
    subprocess.run([str(binary)], cwd=ROOT, check=True)

print("M61 xHCI init bisector source/codegen acceptance: PASS")
print("M61 xHCI progress: 88-8F AC-B2")
print("M61 xHCI false reasons: B3-BE; BF returned-false; FE returned-true")
print("M61 xHCI ring progress/success: 4A-57")
print("M61 xHCI ring false reasons: 40-49 58-5D")
print("M61 xHCI scratchpad DMA/allocation/cleanup acceptance: PASS")
print("M61 xHCI ring allocation PMM/HHDM subreasons: YES")
print("M61 xHCI ring exact reason final-stable after BD/BF: YES")
