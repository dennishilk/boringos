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
    raise RuntimeError("expected exactly seven direct 7B-to-7C entry breadcrumbs plus the macro definition")
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

nm_output = subprocess.check_output(["nm", "-n", str(elf)], text=True)
for forbidden in (
    "__wrap_boring_framebuffer_get",
    "__real_boring_framebuffer_get",
    "__wrap_boring_graphics_fill_rect",
    "__real_boring_graphics_fill_rect",
    "__wrap_serial_write_string",
    "__real_serial_write_string",
):
    if forbidden in nm_output:
        raise RuntimeError(
            f"M61 7B-to-7C linked kernel contains forbidden wrapper symbol {forbidden}"
        )


def function_disassembly(name: str) -> str:
    return subprocess.check_output(
        ["objdump", "-d", "--disassemble=" + name, str(elf)], text=True
    )


instruction_re = re.compile(
    r"^\\s*([0-9a-f]+):\\s+((?:[0-9a-f]{2}\\s+)+)\\s*(.*)$", re.IGNORECASE
)


def parsed_instructions(body: str):
    rows = []
    for line in body.splitlines():
        match = instruction_re.match(line)
        if match is None:
            continue
        rows.append(
            (
                int(match.group(1), 16),
                bytes.fromhex(match.group(2)),
                match.group(3),
            )
        )
    return rows


def loads_al_code(raw: bytes, code: int) -> bool:
    if (len(raw) >= 2) and (raw[0] == 0xB0) and (raw[1] == code):
        return True
    if (len(raw) == 5) and (raw[0] == 0xB8) and (raw[1] == code):
        return True
    if (len(raw) == 3) and (raw[0] == 0xC6) and (raw[1] == 0xC0) and (raw[2] == code):
        return True
    if (len(raw) == 6) and (raw[0] == 0xC7) and (raw[1] == 0xC0) and (raw[2] == code):
        return True
    if (
        (len(raw) == 7)
        and (raw[0] == 0x48)
        and (raw[1] == 0xC7)
        and (raw[2] == 0xC0)
        and (raw[3] == code)
    ):
        return True
    return False


def is_port80_out(raw: bytes) -> bool:
    return (len(raw) == 2) and (raw[0] == 0xE6) and (raw[1] == 0x80)


def post_addresses(body: str, code: int):
    rows = parsed_instructions(body)
    found = []
    for out_index, (out_address, raw, _asm) in enumerate(rows):
        if not is_port80_out(raw):
            continue
        for load_index in range(max(0, out_index - 5), out_index):
            if loads_al_code(rows[load_index][1], code):
                found.append(out_address)
                break
    return found


pmm_wrapper = function_disassembly("m61_post_pmm_init")
vmm_wrapper = function_disassembly("m61_post_vmm_init")
entry_body = function_disassembly("m61_post_real_boring_kernel_entry")

for code in (0x7B, 0x92):
    addresses = post_addresses(pmm_wrapper, code)
    if len(addresses) != 1:
        raise RuntimeError(
            f"m61_post_pmm_init expected one POST 0x{code:02X}, found {addresses}"
        )
if post_addresses(pmm_wrapper, 0x7B)[0] >= post_addresses(pmm_wrapper, 0x92)[0]:
    raise RuntimeError("existing 7B is not before new 92 in linked PMM wrapper")

addresses_7c = post_addresses(vmm_wrapper, 0x7C)
if len(addresses_7c) != 1:
    raise RuntimeError(f"m61_post_vmm_init expected one POST 0x7C, found {addresses_7c}")

entry_posts = {}
for code in range(0x93, 0x9A):
    addresses = post_addresses(entry_body, code)
    if len(addresses) != 1:
        raise RuntimeError(
            f"entry expected one POST 0x{code:02X}, found {addresses}"
        )
    entry_posts[code] = addresses[0]

success_codes = (0x93, 0x94, 0x95, 0x96, 0x97)
if [entry_posts[code] for code in success_codes] != sorted(
    entry_posts[code] for code in success_codes
):
    raise RuntimeError("linked 93-97 success-path POST order is not preserved")

if "<m61_post_pmm_init>" not in entry_body:
    raise RuntimeError("linked entry does not call m61_post_pmm_init")
if "<pmm_get_stats>" not in entry_body:
    raise RuntimeError("linked entry does not call pmm_get_stats in 7B-to-7C interval")
if "<m61_post_vmm_init>" not in entry_body:
    raise RuntimeError("linked entry does not call m61_post_vmm_init")

call_re = re.compile(r"^\\s*([0-9a-f]+):.*\\bcall\\b.*<([^>]+)>", re.IGNORECASE | re.MULTILINE)
calls = [(int(address, 16), target) for address, target in call_re.findall(entry_body)]


def first_call(target: str, minimum: int = 0):
    for address, name in calls:
        if address >= minimum and name == target:
            return address
    return None


pmm_call = first_call("m61_post_pmm_init")
if pmm_call is None or pmm_call >= entry_posts[0x93]:
    raise RuntimeError("linked 93 is not after the PMM wrapper call")
stats_call = first_call("pmm_get_stats", entry_posts[0x93])
if stats_call is None or not (entry_posts[0x93] < stats_call < entry_posts[0x94]):
    raise RuntimeError("linked first pmm_get_stats call is not bracketed by 93/94")
vmm_call = first_call("m61_post_vmm_init", entry_posts[0x97])
if vmm_call is None or vmm_call <= entry_posts[0x97]:
    raise RuntimeError("linked VMM wrapper call is not after POST 97")

all_disasm = subprocess.check_output(["objdump", "-d", str(elf)], text=True)
for code in range(0x92, 0x9A):
    occurrences = len(post_addresses(all_disasm, code))
    if occurrences != 1:
        raise RuntimeError(
            f"linked POST 0x{code:02X} expected exactly once globally, found {occurrences}"
        )

print("M61 linked existing 7B meaning preserved: YES")
print("M61 linked existing 7C meaning preserved: YES")
print("M61 POST 7B-to-7C map: 92 wrapper-return; 93 PMM-success/stats-before; 94 stats-success/report-before; 95 report-after/selftest-before; 96 selftest-success/final-serial-before; 97 final-serial-after/VMM-call-before; 98 PMM-false; 99 initial-stats-false")
print("M61 linked new 92-99 breadcrumbs confined to 7B-to-7C interval: YES")
print("M61 linked framebuffer diagnostic writes reintroduced: NO")
print("M61 linked getter wrapper reintroduced: NO")
print("M61 linked PMM/VMM runtime semantics changed beyond candidate POST breadcrumbs: NO")
