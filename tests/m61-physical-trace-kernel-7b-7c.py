from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parent.parent
base = root / "tests/m61-physical-trace-kernel.sh"
entry = root / "kernel/core/entry.c"
previous = root / "tests/m61-physical-trace-kernel-71-72.py"
base_src = base.read_text()
entry_src = entry.read_text()


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


# 7B is emitted after the real PMM init returns. Add exactly one breadcrumb
# after the existing 7B and before the diagnostic wrapper returns to entry.c.
base_instrumented = replace_once(
    base_src,
    "    M61_POST(M61_POST_PMM_INIT_AFTER);\n    return result;\n",
    "    M61_POST(M61_POST_PMM_INIT_AFTER);\n"
    "    /* 92: 7B emitted; PMM wrapper is about to return to entry.c. */\n"
    "    M61_POST(0x92U);\n"
    "    return result;\n",
    "7B wrapper-return boundary",
)

# Direct candidate-only source breadcrumbs are preferable here to adding more
# linker wrappers. This temporary entry.c is compiled only by the M61 physical
# relink and is restored byte-for-byte immediately afterwards.
entry_instrumented = replace_once(
    entry_src,
    "#include <boring/irq.h>\n",
    "#include <boring/irq.h>\n#include <boring/io.h>\n",
    "candidate POST include",
)
entry_instrumented = replace_once(
    entry_instrumented,
    "#define TASK_TEST_TIMER_SPIN_LIMIT 50000000ULL\n",
    "#define TASK_TEST_TIMER_SPIN_LIMIT 50000000ULL\n\n"
    "#ifndef BORING_M61_PHYSICAL_BREADCRUMBS\n"
    "#error \"M61 7B-to-7C direct breadcrumbs must stay candidate-build gated\"\n"
    "#endif\n"
    "#define M61_7B_7C_POST(code) \\\n"
    "    x86_64_out8((uint16_t)0x80U, (uint8_t)(code))\n",
    "candidate POST macro",
)

old_pmm_gate = '''    if (!pmm_init(limine_memmap_request.response) ||
        !pmm_get_stats(&pmm_stats)) {
        serial_write_string("Physical memory manager: FAILED\\n");
        x86_64_halt_forever();
    }
'''
new_pmm_gate = '''    if (!pmm_init(limine_memmap_request.response)) {
        /* 98: PMM wrapper returned false; normal failure path follows. */
        M61_7B_7C_POST(0x98U);
        serial_write_string("Physical memory manager: FAILED\\n");
        x86_64_halt_forever();
    }
    /* 93: caller resumed and PMM result was true; stats call is next. */
    M61_7B_7C_POST(0x93U);
    if (!pmm_get_stats(&pmm_stats)) {
        /* 99: first post-PMM stats query returned false. */
        M61_7B_7C_POST(0x99U);
        serial_write_string("Physical memory manager: FAILED\\n");
        x86_64_halt_forever();
    }
    /* 94: first post-PMM stats query returned true; PMM report is next. */
    M61_7B_7C_POST(0x94U);
'''
entry_instrumented = replace_once(
    entry_instrumented, old_pmm_gate, new_pmm_gate, "PMM result/stats boundaries"
)
entry_instrumented = replace_once(
    entry_instrumented,
    '    serial_write_string("PMM: online\\n\\n");\n\n'
    '    if (!pmm_self_test()) {\n',
    '    serial_write_string("PMM: online\\n\\n");\n'
    '    /* 95: PMM reporting returned; self-test call is next. */\n'
    '    M61_7B_7C_POST(0x95U);\n\n'
    '    if (!pmm_self_test()) {\n',
    "PMM report/self-test boundary",
)
entry_instrumented = replace_once(
    entry_instrumented,
    '    if (!pmm_self_test()) {\n'
    '        x86_64_halt_forever();\n'
    '    }\n'
    '    serial_write_string("\\nBoringKernel physical memory test passed.\\n\\n");\n\n'
    '    if (!vmm_init(limine_hhdm_request.response,\n',
    '    if (!pmm_self_test()) {\n'
    '        x86_64_halt_forever();\n'
    '    }\n'
    '    /* 96: PMM self-test returned true; final PMM PASS serial is next. */\n'
    '    M61_7B_7C_POST(0x96U);\n'
    '    serial_write_string("\\nBoringKernel physical memory test passed.\\n\\n");\n'
    '    /* 97: final PMM PASS serial returned; VMM argument evaluation/call is next. */\n'
    '    M61_7B_7C_POST(0x97U);\n\n'
    '    if (!vmm_init(limine_hhdm_request.response,\n',
    "PMM self-test/final-serial/VMM boundary",
)

if entry_instrumented.count("M61_7B_7C_POST(") != 8:
    raise RuntimeError(
        "expected exactly seven direct 7B-to-7C entry breadcrumbs plus the macro definition"
    )
for code in range(0x93, 0x9A):
    if entry_instrumented.count(f"0x{code:02X}U") != 1:
        raise RuntimeError(f"entry breadcrumb 0x{code:02X} is missing or duplicated")
if base_instrumented.count("M61_POST(0x92U)") != 1:
    raise RuntimeError("wrapper breadcrumb 0x92 is missing or duplicated")

base.write_text(base_instrumented)
entry.write_text(entry_instrumented)
try:
    subprocess.run(["python3", str(previous)], cwd=root, check=True)
finally:
    base.write_text(base_src)
    entry.write_text(entry_src)

if base.read_text() != base_src or entry.read_text() != entry_src:
    raise RuntimeError("M61 7B-to-7C source restoration failed")

elf = root / "build/kernel.elf"
if not elf.exists():
    raise RuntimeError("M61 7B-to-7C relink did not produce build/kernel.elf")

# Consume all four requested linked-binary views. The proof below is based on
# symbol addresses/sizes plus decoded instruction bytes; pretty-printed objdump
# function blocks are never used as a correctness boundary.
nm_output = subprocess.check_output(["nm", "-n", str(elf)], text=True)
disasm = subprocess.check_output(["objdump", "-d", str(elf)], text=True)
disasm_reloc = subprocess.check_output(["objdump", "-dr", str(elf)], text=True)
readelf_output = subprocess.check_output(["readelf", "-sW", str(elf)], text=True)

for forbidden in (
    "__wrap_boring_framebuffer_get",
    "__real_boring_framebuffer_get",
    "__wrap_boring_graphics_fill_rect",
    "__real_boring_graphics_fill_rect",
    "__wrap_serial_write_string",
    "__real_serial_write_string",
):
    if forbidden in nm_output or forbidden in disasm_reloc:
        raise RuntimeError(
            f"M61 7B-to-7C linked kernel contains forbidden wrapper/reference {forbidden}"
        )

# nm is used to detect aliases and to provide a next-symbol fallback for the
# rare zero-sized ELF function symbol. readelf supplies the authoritative FUNC
# start/size whenever available.
nm_symbols = {}
for line in nm_output.splitlines():
    fields = line.split()
    if len(fields) < 3:
        continue
    try:
        address = int(fields[0], 16)
    except ValueError:
        continue
    nm_symbols.setdefault(fields[-1], []).append((address, fields[1]))

elf_functions = {}
for line in readelf_output.splitlines():
    fields = line.split()
    if len(fields) < 8 or fields[3] != "FUNC" or fields[6] == "UND":
        continue
    try:
        address = int(fields[1], 16)
        size = int(fields[2], 10)
    except ValueError:
        continue
    elf_functions.setdefault(fields[7], []).append(
        (address, size, fields[4], fields[5], fields[6])
    )


def function_range(name: str):
    rows = elf_functions.get(name, [])
    if len(rows) != 1:
        raise RuntimeError(
            f"readelf expected one defined FUNC symbol {name}, found {rows}"
        )
    start, size, _bind, _visibility, _section = rows[0]
    if size > 0:
        return start, start + size

    next_addresses = []
    for symbol_rows in nm_symbols.values():
        for address, kind in symbol_rows:
            if address > start and kind.lower() == "t":
                next_addresses.append(address)
    if not next_addresses:
        raise RuntimeError(f"cannot determine zero-sized function end for {name}")
    return start, min(next_addresses)


instruction_re = re.compile(
    r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2}\s+)+)\s*(.*)$",
    re.IGNORECASE,
)
instructions = []
for line in disasm.splitlines():
    match = instruction_re.match(line)
    if match is None:
        continue
    instructions.append(
        (int(match.group(1), 16), bytes.fromhex(match.group(2)), match.group(3))
    )


def rows_in_range(start: int, end: int):
    return [row for row in instructions if start <= row[0] < end]


def is_port80_out(raw: bytes) -> bool:
    return len(raw) == 2 and raw[0] == 0xE6 and raw[1] == 0x80


def loads_al_code(raw: bytes, code: int) -> bool:
    # Accept the concrete encodings GCC emits for the uint8_t AL operand. For
    # imm32 forms only the low byte is semantically relevant, so both positive
    # and sign-extended pretty-print forms decode identically here.
    if len(raw) == 2 and raw[0] == 0xB0 and raw[1] == code:
        return True
    if len(raw) == 5 and raw[0] == 0xB8 and raw[1] == code:
        return True
    if len(raw) == 3 and raw[0] == 0xC6 and raw[1] == 0xC0 and raw[2] == code:
        return True
    if len(raw) == 6 and raw[0] == 0xC7 and raw[1] == 0xC0 and raw[2] == code:
        return True
    if (
        len(raw) == 7
        and raw[0] == 0x48
        and raw[1] == 0xC7
        and raw[2] == 0xC0
        and raw[3] == code
    ):
        return True
    return False


def post_out_addresses(start: int, end: int, code: int):
    rows = rows_in_range(start, end)
    found = []
    for out_index, (out_address, raw, _asm) in enumerate(rows):
        if not is_port80_out(raw):
            continue
        lower = max(0, out_index - 5)
        for load_index in range(out_index - 1, lower - 1, -1):
            if loads_al_code(rows[load_index][1], code):
                found.append(out_address)
                break
    return found


def require_post_once(start: int, end: int, code: int, label: str) -> int:
    addresses = post_out_addresses(start, end, code)
    if len(addresses) != 1:
        raise RuntimeError(
            f"{label} expected one decoded POST 0x{code:02X}, found {addresses}"
        )
    return addresses[0]


def direct_calls(start: int, end: int):
    calls = []
    for address, raw, asm in rows_in_range(start, end):
        if len(raw) == 5 and raw[0] == 0xE8:
            displacement = int.from_bytes(raw[1:5], "little", signed=True)
            target = (address + 5 + displacement) & 0xFFFFFFFFFFFFFFFF
            calls.append((address, target, asm))
    return calls


def symbol_address(name: str) -> int:
    rows = elf_functions.get(name, [])
    if len(rows) != 1:
        raise RuntimeError(f"expected one linked FUNC {name}, found {rows}")
    return rows[0][0]


def require_call_target(start: int, end: int, target: int, label: str) -> int:
    addresses = [address for address, call_target, _asm in direct_calls(start, end)
                 if call_target == target]
    if len(addresses) != 1:
        raise RuntimeError(f"{label} expected one direct E8 call, found {addresses}")
    return addresses[0]


pmm_start, pmm_end = function_range("m61_post_pmm_init")
vmm_start, vmm_end = function_range("m61_post_vmm_init")
entry_start, entry_end = function_range("m61_post_real_boring_kernel_entry")
real_pmm = symbol_address("pmm_init")
real_vmm = symbol_address("vmm_init")

# If GNU --wrap aliases survive in the symbol table, they must not disagree
# with the concrete wrapper/real addresses we prove from decoded call targets.
for alias, expected in (
    ("__wrap_pmm_init", pmm_start),
    ("__real_pmm_init", real_pmm),
    ("__wrap_vmm_init", vmm_start),
    ("__real_vmm_init", real_vmm),
):
    for address, _kind in nm_symbols.get(alias, []):
        if address != expected:
            raise RuntimeError(
                f"linked alias {alias}=0x{address:x} disagrees with expected 0x{expected:x}"
            )

pmm_7a = require_post_once(pmm_start, pmm_end, 0x7A, "PMM wrapper")
pmm_7b = require_post_once(pmm_start, pmm_end, 0x7B, "PMM wrapper")
pmm_92 = require_post_once(pmm_start, pmm_end, 0x92, "PMM wrapper")
pmm_real_call = require_call_target(
    pmm_start, pmm_end, real_pmm, "PMM wrapper -> real pmm_init"
)
if not (pmm_7a < pmm_real_call < pmm_7b < pmm_92):
    raise RuntimeError(
        "decoded PMM wrapper order is not 7A -> real pmm_init -> 7B -> 92"
    )

vmm_7c = require_post_once(vmm_start, vmm_end, 0x7C, "VMM wrapper")
vmm_7d = require_post_once(vmm_start, vmm_end, 0x7D, "VMM wrapper")
vmm_real_call = require_call_target(
    vmm_start, vmm_end, real_vmm, "VMM wrapper -> real vmm_init"
)
if not (vmm_7c < vmm_real_call < vmm_7d):
    raise RuntimeError("decoded VMM wrapper order is not 7C -> real vmm_init -> 7D")

entry_posts = {
    code: require_post_once(
        entry_start, entry_end, code, "m61_post_real_boring_kernel_entry"
    )
    for code in range(0x93, 0x9A)
}
pmm_wrapper_call = require_call_target(
    entry_start, entry_end, pmm_start, "entry -> m61_post_pmm_init"
)
vmm_wrapper_call = require_call_target(
    entry_start, entry_end, vmm_start, "entry -> m61_post_vmm_init"
)

pmm_get_stats_address = symbol_address("pmm_get_stats")
pmm_self_test_address = symbol_address("pmm_self_test")
stats_calls = [
    address
    for address, target, _asm in direct_calls(entry_start, entry_end)
    if target == pmm_get_stats_address
]
self_test_calls = [
    address
    for address, target, _asm in direct_calls(entry_start, entry_end)
    if target == pmm_self_test_address
]
if not stats_calls:
    raise RuntimeError("entry has no decoded direct call to pmm_get_stats")
if len(self_test_calls) != 1:
    raise RuntimeError(f"entry expected one decoded pmm_self_test call, found {self_test_calls}")

first_stats_call = min(address for address in stats_calls if address > pmm_wrapper_call)
self_test_call = self_test_calls[0]
if not (
    pmm_wrapper_call
    < entry_posts[0x93]
    < first_stats_call
    < entry_posts[0x94]
    < entry_posts[0x95]
    < self_test_call
    < entry_posts[0x96]
    < entry_posts[0x97]
    < vmm_wrapper_call
):
    raise RuntimeError(
        "decoded 7B-to-7C success path is not PMM-call -> 93 -> stats -> 94 -> "
        "95 -> self-test -> 96 -> 97 -> VMM-call"
    )

# Failure-only 98/99 remain in the same linked entry function. Their source
# anchors are already proven above and their decoded port writes are unique;
# neither path can reach VMM because both immediately take the existing halt
# path. Do not impose linear-address ordering on compiler-placed failure blocks.
for code in (0x98, 0x99):
    if not (entry_start <= entry_posts[code] < entry_end):
        raise RuntimeError(f"decoded failure POST 0x{code:02X} escaped entry function")

# Fresh codes must be unique globally as actual port-0x80 writes, not merely as
# immediate constants elsewhere in data or code.
for code in range(0x92, 0x9A):
    global_hits = []
    for index, (address, raw, _asm) in enumerate(instructions):
        if not is_port80_out(raw):
            continue
        lower = max(0, index - 5)
        for load_index in range(index - 1, lower - 1, -1):
            if loads_al_code(instructions[load_index][1], code):
                global_hits.append(address)
                break
    if len(global_hits) != 1:
        raise RuntimeError(
            f"linked POST 0x{code:02X} expected one decoded global port write, found {global_hits}"
        )

print(
    f"M61 linked symbol ranges: pmm-wrapper=0x{pmm_start:x}-0x{pmm_end:x} "
    f"vmm-wrapper=0x{vmm_start:x}-0x{vmm_end:x} "
    f"entry=0x{entry_start:x}-0x{entry_end:x}"
)
print(
    f"M61 decoded PMM boundary: 7A@0x{pmm_7a:x} real-call@0x{pmm_real_call:x} "
    f"7B@0x{pmm_7b:x} 92@0x{pmm_92:x}"
)
print(
    f"M61 decoded VMM boundary: 7C@0x{vmm_7c:x} real-call@0x{vmm_real_call:x} "
    f"7D@0x{vmm_7d:x}"
)
print("M61 verifier root cause fixed: double-escaped regex extraction removed")
print("M61 linked existing 7B meaning preserved: YES")
print("M61 linked existing 7C meaning preserved: YES")
print(
    "M61 POST 7B-to-7C map: 92 wrapper-return; 93 PMM-success/stats-before; "
    "94 stats-success/report-before; 95 report-after/selftest-before; "
    "96 selftest-success/final-serial-before; 97 final-serial-after/VMM-call-before; "
    "98 PMM-false; 99 initial-stats-false"
)
print("M61 linked new 92-99 breadcrumbs confined to 7B-to-7C execution interval: YES")
print("M61 linked framebuffer diagnostic writes reintroduced: NO")
print("M61 linked getter wrapper reintroduced: NO")
print("M61 linked PMM/VMM runtime semantics changed beyond candidate POST breadcrumbs: NO")
