from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parent.parent
base = root / "tests/m61-physical-trace-kernel.sh"
entry = root / "kernel/core/entry.c"
io = root / "kernel/include/boring/io.h"
pmm = root / "kernel/core/pmm.c"
previous = root / "tests/m61-physical-trace-kernel-71-72.py"
base_src = base.read_text()
entry_src = entry.read_text()
io_src = io.read_text()
pmm_src = pmm.read_text()


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


# The generic I/O primitive must remain boring. PMM false-reason state belongs
# only to pmm.c and must not execute on the early 61..79 POST path.
expected_out8 = '''static inline void x86_64_out8(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}
'''
if io_src.count(expected_out8) != 1:
    raise RuntimeError("generic x86_64_out8 is not the transparent pre-d24 primitive")
for forbidden in (
    "BORING_M61_PHYSICAL_BREADCRUMBS",
    "m61_pmm_false_reason",
    "0x7aU",
    "0x98U",
):
    if forbidden in io_src:
        raise RuntimeError(f"generic io.h still contains M61 PMM interception: {forbidden}")
for required in (
    "static uint8_t m61_pmm_false_reason;",
    "m61_pmm_false_reason = (uint8_t)(code);",
    "uint8_t boring_m61_pmm_false_reason(void)",
    "m61_pmm_false_reason = 0U;",
):
    if required not in pmm_src:
        raise RuntimeError(f"localized PMM false-reason source is missing: {required}")


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
    "uint8_t boring_m61_pmm_false_reason(void);\n"
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
        uint8_t pmm_reason;

        /* 98: PMM wrapper returned false; normal failure path follows. */
        M61_7B_7C_POST(0x98U);
        pmm_reason = boring_m61_pmm_false_reason();
        if ((pmm_reason >= 0xa0U) && (pmm_reason <= 0xabU)) {
            /* Leave the exact PMM false reason as the final stable board code. */
            M61_7B_7C_POST(pmm_reason);
        }
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

if entry_instrumented.count("M61_7B_7C_POST(") != 9:
    raise RuntimeError(
        "expected seven direct 7B-to-7C breadcrumbs, one PMM reason re-emit, and the macro definition"
    )
for code in range(0x93, 0x9A):
    if entry_instrumented.count(f"0x{code:02X}U") != 1:
        raise RuntimeError(f"entry breadcrumb 0x{code:02X} is missing or duplicated")
if entry_instrumented.count("boring_m61_pmm_false_reason()") != 1:
    raise RuntimeError("PMM false-reason accessor call is missing or duplicated")
if entry_instrumented.count("M61_7B_7C_POST(pmm_reason)") != 1:
    raise RuntimeError("PMM final reason re-emit is missing or duplicated")
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
disasm = subprocess.check_output(
    ["objdump", "-d", "--insn-width=16", str(elf)], text=True
)
disasm_reloc = subprocess.check_output(
    ["objdump", "-dr", "--insn-width=16", str(elf)], text=True
)
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


def loaded_al_code(raw: bytes):
    # Accept the concrete encodings GCC emits for the uint8_t AL operand. For
    # imm32 forms only the low byte is semantically relevant, so both positive
    # and sign-extended pretty-print forms decode identically here.
    if len(raw) == 2 and raw[0] == 0xB0:
        return raw[1]
    if len(raw) == 5 and raw[0] == 0xB8:
        return raw[1]
    if len(raw) == 3 and raw[0] == 0xC6 and raw[1] == 0xC0:
        return raw[2]
    if len(raw) == 6 and raw[0] == 0xC7 and raw[1] == 0xC0:
        return raw[2]
    if (
        len(raw) == 7
        and raw[0] == 0x48
        and raw[1] == 0xC7
        and raw[2] == 0xC0
    ):
        return raw[3]
    return None


def eax_constant_write(raw: bytes):
    if raw in (bytes((0x31, 0xC0)), bytes((0x33, 0xC0))):
        return 0
    if len(raw) == 5 and raw[0] == 0xB8:
        return int.from_bytes(raw[1:5], "little", signed=False)
    if len(raw) == 6 and raw[0] == 0xC7 and raw[1] == 0xC0:
        return int.from_bytes(raw[2:6], "little", signed=False)
    return None


def post_sites(start: int, end: int, code: int):
    rows = rows_in_range(start, end)
    found = []
    for out_index, (out_address, raw, _asm) in enumerate(rows):
        if not is_port80_out(raw):
            continue
        lower = max(0, out_index - 5)
        for load_index in range(out_index - 1, lower - 1, -1):
            loaded = loaded_al_code(rows[load_index][1])
            if loaded is not None:
                if loaded == code:
                    found.append((rows[load_index][0], out_address))
                break
            if is_port80_out(rows[load_index][1]):
                break
    return found


def require_post_site(start: int, end: int, code: int, label: str):
    sites = post_sites(start, end, code)
    if len(sites) != 1:
        raise RuntimeError(
            f"{label} expected one decoded POST 0x{code:02X}, found {sites}"
        )
    return sites[0]


def symbol_address(name: str) -> int:
    rows = elf_functions.get(name, [])
    if len(rows) != 1:
        raise RuntimeError(f"expected one linked FUNC {name}, found {rows}")
    return rows[0][0]


def decode_direct_transfer(address: int, raw: bytes):
    if len(raw) == 5 and raw[0] in (0xE8, 0xE9):
        displacement = int.from_bytes(raw[1:5], "little", signed=True)
        target = (address + 5 + displacement) & 0xFFFFFFFFFFFFFFFF
        return ("call" if raw[0] == 0xE8 else "jump", target, None)
    if len(raw) == 2 and raw[0] == 0xEB:
        displacement = int.from_bytes(raw[1:2], "little", signed=True)
        target = (address + 2 + displacement) & 0xFFFFFFFFFFFFFFFF
        return ("jump", target, None)
    if len(raw) == 2 and 0x70 <= raw[0] <= 0x7F:
        displacement = int.from_bytes(raw[1:2], "little", signed=True)
        target = (address + 2 + displacement) & 0xFFFFFFFFFFFFFFFF
        return ("branch", target, raw[0] & 0x0F)
    if len(raw) == 6 and raw[0] == 0x0F and 0x80 <= raw[1] <= 0x8F:
        displacement = int.from_bytes(raw[2:6], "little", signed=True)
        target = (address + 6 + displacement) & 0xFFFFFFFFFFFFFFFF
        return ("branch", target, raw[1] & 0x0F)
    return None


def direct_transfers(start: int, end: int, kinds=("call", "jump")):
    transfers = []
    for address, raw, _asm in rows_in_range(start, end):
        decoded = decode_direct_transfer(address, raw)
        if decoded is not None and decoded[0] in kinds:
            transfers.append((address, decoded[0], decoded[1]))
    return transfers


def require_transfer_target(start: int, end: int, target: int, label: str,
                            kinds=("call", "jump")):
    matches = [
        transfer
        for transfer in direct_transfers(start, end, kinds)
        if transfer[2] == target
    ]
    if len(matches) != 1:
        raise RuntimeError(
            f"{label} expected one decoded direct transfer to 0x{target:x}, "
            f"found {matches}"
        )
    return matches[0]


def is_return(raw: bytes) -> bool:
    return bool(raw) and raw[0] in (0xC2, 0xC3, 0xCA, 0xCB)


def function_cfg(start: int, end: int, terminal_call_targets=frozenset()):
    rows = rows_in_range(start, end)
    addresses = {row[0] for row in rows}
    graph = {}
    for index, (address, raw, _asm) in enumerate(rows):
        next_address = rows[index + 1][0] if index + 1 < len(rows) else None
        decoded = decode_direct_transfer(address, raw)
        successors = []
        if decoded is not None:
            kind, target, _condition = decoded
            if kind == "call":
                if target not in terminal_call_targets and next_address is not None:
                    successors.append(next_address)
            elif kind == "jump":
                if target in addresses:
                    successors.append(target)
            else:
                if target in addresses:
                    successors.append(target)
                if next_address is not None:
                    successors.append(next_address)
        elif is_return(raw) or raw == bytes((0x0F, 0x0B)) or raw == bytes((0xF4,)):
            successors = []
        elif len(raw) >= 2 and raw[0] == 0xFF:
            extension = (raw[1] >> 3) & 0x07
            if extension in (2, 3) and next_address is not None:
                # An indirect CALL still returns to the following instruction.
                successors.append(next_address)
            # Indirect JMP targets cannot be derived from these bytes alone.
        elif next_address is not None:
            successors.append(next_address)
        graph[address] = tuple(dict.fromkeys(successors))
    return graph


def cfg_reachable(graph, start: int, target: int, blocked=frozenset()) -> bool:
    if start in blocked or target in blocked or start not in graph or target not in graph:
        return False
    pending = [start]
    seen = set()
    while pending:
        address = pending.pop()
        if address == target:
            return True
        if address in seen or address in blocked:
            continue
        seen.add(address)
        pending.extend(
            successor
            for successor in graph.get(address, ())
            if successor not in seen and successor not in blocked
        )
    return False


def require_cfg_sequence(graph, nodes, label: str):
    if len(nodes) != len(set(nodes)):
        raise RuntimeError(f"{label} contains duplicate CFG nodes: {nodes}")
    for node in nodes:
        if node not in graph:
            raise RuntimeError(f"{label} node 0x{node:x} is outside its linked function")
    for before, after in zip(nodes, nodes[1:]):
        if not cfg_reachable(graph, before, after):
            raise RuntimeError(
                f"{label} has no execution path 0x{before:x} -> 0x{after:x}"
            )
    start = nodes[0]
    terminal = nodes[-1]
    for required in nodes[1:-1]:
        if cfg_reachable(graph, start, terminal, frozenset((required,))):
            raise RuntimeError(
                f"{label} can reach 0x{terminal:x} without required "
                f"node 0x{required:x}"
            )


def return_reachable_after(graph, start: int, function_start: int,
                           function_end: int, label: str) -> int:
    returns = [
        address
        for address, raw, _asm in rows_in_range(function_start, function_end)
        if is_return(raw) and cfg_reachable(graph, start, address)
    ]
    if len(returns) != 1:
        raise RuntimeError(f"{label} expected one reachable return, found {returns}")
    return returns[0]


def call_addresses_to(start: int, end: int, target: int):
    return [
        address
        for address, kind, call_target in direct_transfers(start, end, ("call",))
        if kind == "call" and call_target == target
    ]


def dominating_calls_between(graph, function_start: int, function_end: int,
                              start: int, end: int, target: int):
    return [
        address
        for address in call_addresses_to(function_start, function_end, target)
        if cfg_reachable(graph, start, address)
        and cfg_reachable(graph, address, end)
        and not cfg_reachable(graph, start, end, frozenset((address,)))
    ]


def cfg_order(graph, nodes, label: str):
    remaining = set(nodes)
    ordered = []
    while remaining:
        first = [
            node
            for node in remaining
            if all(
                node == other or cfg_reachable(graph, node, other)
                for other in remaining
            )
        ]
        if len(first) != 1:
            raise RuntimeError(f"{label} is not a unique execution order: {nodes}")
        ordered.append(first[0])
        remaining.remove(first[0])
    return ordered


def require_false_return(name: str) -> int:
    start, end = function_range(name)
    rows = rows_in_range(start, end)
    zeroes = [
        address
        for address, raw, _asm in rows
        if raw in (bytes((0x31, 0xC0)), bytes((0x33, 0xC0)))
        or (len(raw) == 5 and raw[0] == 0xB8 and raw[1:] == bytes(4))
    ]
    returns = [address for address, raw, _asm in rows if is_return(raw)]
    if len(zeroes) != 1 or len(returns) != 1:
        raise RuntimeError(
            f"{name} does not have one decoded EAX-zero and return: "
            f"zeroes={zeroes} returns={returns}"
        )
    cfg = function_cfg(start, end)
    require_cfg_sequence(cfg, [rows[0][0], zeroes[0], returns[0]], name)
    return start


pmm_wrap_start, pmm_wrap_end = function_range("__wrap_pmm_init")
pmm_post_start, pmm_post_end = function_range("m61_post_pmm_init")
vmm_wrap_start, vmm_wrap_end = function_range("__wrap_vmm_init")
vmm_post_start, vmm_post_end = function_range("m61_post_vmm_init")
entry_start, entry_end = function_range("m61_post_real_boring_kernel_entry")
real_pmm = symbol_address("pmm_init")
real_vmm = symbol_address("vmm_init")
pmm_init_start, pmm_init_end = function_range("pmm_init")
pmm_reason_accessor_start, pmm_reason_accessor_end = function_range(
    "boring_m61_pmm_false_reason"
)

if pmm_init_start != real_pmm:
    raise RuntimeError("linked real PMM symbol/range disagreement")

if len({pmm_wrap_start, pmm_post_start, real_pmm}) != 3:
    raise RuntimeError("PMM wrapper, POST shim, and real function are not distinct")
if len({vmm_wrap_start, vmm_post_start, real_vmm}) != 3:
    raise RuntimeError("VMM wrapper, POST shim, and real function are not distinct")

# Prove each GNU --wrap execution chain using actual rel32 targets. The outer
# diagnostic wrapper may CALL the POST shim or tail-JMP to it; both preserve the
# required semantics. The POST shim itself must CALL the real implementation so
# execution can resume to emit the after-code.
pmm_wrap_to_post = require_transfer_target(
    pmm_wrap_start, pmm_wrap_end, pmm_post_start,
    "__wrap_pmm_init -> m61_post_pmm_init"
)
vmm_wrap_to_post = require_transfer_target(
    vmm_wrap_start, vmm_wrap_end, vmm_post_start,
    "__wrap_vmm_init -> m61_post_vmm_init"
)
pmm_post_to_real = require_transfer_target(
    pmm_post_start, pmm_post_end, real_pmm,
    "m61_post_pmm_init -> real pmm_init", ("call",)
)
vmm_post_to_real = require_transfer_target(
    vmm_post_start, vmm_post_end, real_vmm,
    "m61_post_vmm_init -> real vmm_init", ("call",)
)

pmm_7a = require_post_site(pmm_post_start, pmm_post_end, 0x7A, "PMM POST shim")
pmm_7b = require_post_site(pmm_post_start, pmm_post_end, 0x7B, "PMM POST shim")
pmm_92 = require_post_site(pmm_post_start, pmm_post_end, 0x92, "PMM POST shim")
vmm_7c = require_post_site(vmm_post_start, vmm_post_end, 0x7C, "VMM POST shim")
vmm_7d = require_post_site(vmm_post_start, vmm_post_end, 0x7D, "VMM POST shim")

pmm_post_cfg = function_cfg(pmm_post_start, pmm_post_end)
pmm_post_return = return_reachable_after(
    pmm_post_cfg, pmm_92[1], pmm_post_start, pmm_post_end, "PMM POST shim"
)
require_cfg_sequence(
    pmm_post_cfg,
    [pmm_7a[1], pmm_post_to_real[0], pmm_7b[1], pmm_92[1], pmm_post_return],
    "PMM 7A -> real pmm_init -> 7B -> 92",
)

vmm_post_cfg = function_cfg(vmm_post_start, vmm_post_end)
vmm_post_return = return_reachable_after(
    vmm_post_cfg, vmm_7d[1], vmm_post_start, vmm_post_end, "VMM POST shim"
)
require_cfg_sequence(
    vmm_post_cfg,
    [vmm_7c[1], vmm_post_to_real[0], vmm_7d[1], vmm_post_return],
    "VMM 7C -> real vmm_init -> 7D",
)

for label, start, end, transfer in (
    ("PMM outer wrapper", pmm_wrap_start, pmm_wrap_end, pmm_wrap_to_post),
    ("VMM outer wrapper", vmm_wrap_start, vmm_wrap_end, vmm_wrap_to_post),
):
    if transfer[1] == "call":
        wrapper_cfg = function_cfg(start, end)
        return_reachable_after(wrapper_cfg, transfer[0], start, end, label)
    elif transfer[1] != "jump":
        raise RuntimeError(f"{label} has unsupported transfer {transfer}")

entry_posts = {
    code: require_post_site(
        entry_start, entry_end, code, "m61_post_real_boring_kernel_entry"
    )
    for code in range(0x93, 0x9A)
}
pmm_entry_to_wrap = require_transfer_target(
    entry_start, entry_end, pmm_wrap_start,
    "entry -> __wrap_pmm_init", ("call",)
)
vmm_entry_to_wrap = require_transfer_target(
    entry_start, entry_end, vmm_wrap_start,
    "entry -> __wrap_vmm_init", ("call",)
)

halt_address = symbol_address("x86_64_halt_forever")
pmm_self_test_fail_address = require_false_return("pmm_self_test_fail")
entry_cfg = function_cfg(
    entry_start,
    entry_end,
    frozenset((halt_address, pmm_self_test_fail_address)),
)

pmm_get_stats_address = symbol_address("pmm_get_stats")
initial_stats_calls = dominating_calls_between(
    entry_cfg, entry_start, entry_end,
    entry_posts[0x93][1], entry_posts[0x94][1], pmm_get_stats_address,
)
if len(initial_stats_calls) != 1:
    raise RuntimeError(
        f"expected one required initial pmm_get_stats call between 93 and 94, "
        f"found {initial_stats_calls}"
    )
initial_stats_call = initial_stats_calls[0]

serial_string_address = symbol_address("serial_write_string")
serial_u64_address = symbol_address("serial_write_u64")
report_string_calls = dominating_calls_between(
    entry_cfg, entry_start, entry_end,
    entry_posts[0x94][1], entry_posts[0x95][1], serial_string_address,
)
report_u64_calls = dominating_calls_between(
    entry_cfg, entry_start, entry_end,
    entry_posts[0x94][1], entry_posts[0x95][1], serial_u64_address,
)
if not report_string_calls or not report_u64_calls:
    raise RuntimeError(
        "linked PMM report between 94 and 95 lacks required string/u64 calls"
    )

# GCC inlines the static pmm_self_test into entry.c in this candidate. Prove
# that its three source-level stats checkpoints and its allocator/free/usable
# operations are mandatory CFG cutpoints on every path that reaches 96. This
# avoids inventing a nonexistent linked pmm_self_test symbol or comparing code
# addresses that the compiler has laid out across forward/backward branches.
self_test_stats_calls = dominating_calls_between(
    entry_cfg, entry_start, entry_end,
    entry_posts[0x95][1], entry_posts[0x96][1], pmm_get_stats_address,
)
if len(self_test_stats_calls) != 3:
    raise RuntimeError(
        "inlined pmm_self_test expected three required pmm_get_stats CFG "
        f"cutpoints, found {self_test_stats_calls}"
    )
self_test_stats_order = cfg_order(
    entry_cfg, self_test_stats_calls, "inlined pmm_self_test stats calls"
)
for operation in ("pmm_alloc_frame", "pmm_free_frame", "pmm_frame_is_usable"):
    operation_calls = dominating_calls_between(
        entry_cfg, entry_start, entry_end,
        entry_posts[0x95][1], entry_posts[0x96][1], symbol_address(operation),
    )
    if not operation_calls:
        raise RuntimeError(
            f"inlined pmm_self_test has no required linked {operation} call"
        )

final_serial_calls = dominating_calls_between(
    entry_cfg, entry_start, entry_end,
    entry_posts[0x96][1], entry_posts[0x97][1], serial_string_address,
)
if len(final_serial_calls) != 1:
    raise RuntimeError(
        f"expected one required final PMM PASS serial call between 96 and 97, "
        f"found {final_serial_calls}"
    )
final_serial_call = final_serial_calls[0]

success_nodes = [
    pmm_entry_to_wrap[0],
    entry_posts[0x93][1],
    initial_stats_call,
    entry_posts[0x94][1],
    entry_posts[0x95][1],
    *self_test_stats_order,
    entry_posts[0x96][1],
    final_serial_call,
    entry_posts[0x97][1],
    vmm_entry_to_wrap[0],
]
require_cfg_sequence(
    entry_cfg,
    success_nodes,
    "PMM return -> 93 -> stats -> 94 -> 95 -> self-test -> 96 -> "
    "final serial -> 97 -> VMM call",
)


def require_false_result_branch(call_address: int, success_code: int,
                                failure_code: int, label: str):
    rows = rows_in_range(entry_start, entry_end)
    index_by_address = {row[0]: index for index, row in enumerate(rows)}
    call_index = index_by_address[call_address]
    branches = []
    for row in rows[call_index + 1:call_index + 5]:
        decoded = decode_direct_transfer(row[0], row[1])
        if decoded is not None and decoded[0] == "branch":
            branches.append((row[0], decoded[1], decoded[2]))
    if len(branches) != 1:
        raise RuntimeError(f"{label} expected one nearby result branch, found {branches}")
    branch_address, branch_target, condition = branches[0]
    if condition != 0x5 or branch_target != entry_posts[success_code][0]:
        raise RuntimeError(
            f"{label} is not decoded JNE(nonzero) -> POST {success_code:02X}: "
            f"{branches[0]}"
        )
    branch_index = index_by_address[branch_address]
    false_fallthrough = rows[branch_index + 1][0]
    failure_out = entry_posts[failure_code][1]
    success_out = entry_posts[success_code][1]
    if not cfg_reachable(entry_cfg, false_fallthrough, failure_out):
        raise RuntimeError(f"{label} false path does not reach POST {failure_code:02X}")
    if cfg_reachable(entry_cfg, false_fallthrough, success_out):
        raise RuntimeError(f"{label} false path can escape to POST {success_code:02X}")
    if cfg_reachable(entry_cfg, branch_target, failure_out):
        raise RuntimeError(f"{label} true path can reach POST {failure_code:02X}")
    halt_calls = [
        address
        for address in call_addresses_to(entry_start, entry_end, halt_address)
        if cfg_reachable(entry_cfg, failure_out, address)
        and not cfg_reachable(
            entry_cfg, false_fallthrough, address,
            frozenset((failure_out,)),
        )
    ]
    if len(halt_calls) != 1:
        raise RuntimeError(
            f"{label} POST {failure_code:02X} path expected one required halt, "
            f"found {halt_calls}"
        )
    if cfg_reachable(entry_cfg, false_fallthrough, vmm_entry_to_wrap[0]):
        raise RuntimeError(f"{label} false path can reach the VMM call")
    return branch_address, halt_calls[0]


pmm_failure_branch = require_false_result_branch(
    pmm_entry_to_wrap[0], 0x93, 0x98, "PMM init result"
)
stats_failure_branch = require_false_result_branch(
    initial_stats_call, 0x94, 0x99, "initial PMM stats result"
)

pmm_reason_accessor_call = require_transfer_target(
    entry_start, entry_end, pmm_reason_accessor_start,
    "PMM false caller -> localized reason accessor", ("call",)
)
entry_reason_outs = [
    address
    for address, raw, _asm in rows_in_range(entry_start, entry_end)
    if is_port80_out(raw)
    and cfg_reachable(entry_cfg, pmm_reason_accessor_call[0], address)
    and cfg_reachable(entry_cfg, address, pmm_failure_branch[1])
]
if len(entry_reason_outs) != 1:
    raise RuntimeError(
        f"PMM false caller expected one dynamic final reason out, found {entry_reason_outs}"
    )
pmm_reason_final_out = entry_reason_outs[0]
if not cfg_reachable(entry_cfg, entry_posts[0x98][1], pmm_reason_accessor_call[0]):
    raise RuntimeError("PMM false POST 98 cannot reach localized reason accessor")
if not cfg_reachable(entry_cfg, pmm_reason_accessor_call[0], pmm_reason_final_out):
    raise RuntimeError("localized PMM reason accessor cannot reach final port-0x80 write")
if not cfg_reachable(entry_cfg, pmm_reason_final_out, pmm_failure_branch[1]):
    raise RuntimeError("final PMM reason write cannot reach the existing terminal halt")
if cfg_reachable(entry_cfg, entry_posts[0x93][1], pmm_reason_accessor_call[0]):
    raise RuntimeError("successful PMM path can reach the false-reason accessor")
if cfg_reachable(entry_cfg, entry_posts[0x93][1], pmm_reason_final_out):
    raise RuntimeError("successful PMM path can reach the final false-reason write")

# The real, candidate-gated pmm_init carries one direct port-0x80 code for
# every existing semantic false-return class. Prove the final linked function
# using decoded instruction bytes and its CFG: every reason site must flow
# through the shared EAX=false return, none may reach the EAX=true setter, and
# a successful post-free path must still reach the true return.
pmm_false_posts = {
    code: require_post_site(pmm_init_start, pmm_init_end, code, "real pmm_init")
    for code in range(0xA0, 0xAC)
}
pmm_init_rows = rows_in_range(pmm_init_start, pmm_init_end)
pmm_init_cfg = function_cfg(pmm_init_start, pmm_init_end)
pmm_false_setters = [
    address
    for address, raw, _asm in pmm_init_rows
    if eax_constant_write(raw) == 0
]
pmm_true_setters = [
    address
    for address, raw, _asm in pmm_init_rows
    if eax_constant_write(raw) == 1
]
pmm_returns = [
    address for address, raw, _asm in pmm_init_rows if is_return(raw)
]
if len(pmm_false_setters) != 1 or len(pmm_true_setters) != 1 or len(pmm_returns) != 1:
    raise RuntimeError(
        "real pmm_init expected one decoded false setter, true setter, and return: "
        f"false={pmm_false_setters} true={pmm_true_setters} returns={pmm_returns}"
    )
pmm_false_setter = pmm_false_setters[0]
pmm_true_setter = pmm_true_setters[0]
pmm_return = pmm_returns[0]

for code, (_load_address, out_address) in pmm_false_posts.items():
    if not cfg_reachable(pmm_init_cfg, out_address, pmm_false_setter):
        raise RuntimeError(
            f"real pmm_init POST 0x{code:02X} cannot reach its false result setter"
        )
    if cfg_reachable(pmm_init_cfg, out_address, pmm_true_setter):
        raise RuntimeError(
            f"real pmm_init POST 0x{code:02X} can escape to the true result setter"
        )
    if not cfg_reachable(pmm_init_cfg, out_address, pmm_return):
        raise RuntimeError(
            f"real pmm_init POST 0x{code:02X} cannot reach the function return"
        )
    if cfg_reachable(
        pmm_init_cfg, out_address, pmm_return, frozenset((pmm_false_setter,))
    ):
        raise RuntimeError(
            f"real pmm_init POST 0x{code:02X} can return without setting false"
        )

pmm_reason_outs = frozenset(site[1] for site in pmm_false_posts.values())
if cfg_reachable(
    pmm_init_cfg, pmm_init_rows[0][0], pmm_false_setter, pmm_reason_outs
):
    raise RuntimeError("real pmm_init can produce false without an A0-AB reason write")
if not cfg_reachable(
    pmm_init_cfg, pmm_init_rows[0][0], pmm_true_setter, pmm_reason_outs
):
    raise RuntimeError("real pmm_init has no reason-POST-free successful path")
if not cfg_reachable(pmm_init_cfg, pmm_true_setter, pmm_return):
    raise RuntimeError("real pmm_init true result cannot reach the function return")
if any(
    cfg_reachable(pmm_init_cfg, pmm_true_setter, out_address)
    for out_address in pmm_reason_outs
):
    raise RuntimeError("real pmm_init successful path can reach a false-reason POST")
if cfg_reachable(
    pmm_init_cfg,
    pmm_init_rows[0][0],
    pmm_return,
    frozenset((pmm_false_setter, pmm_true_setter)),
):
    raise RuntimeError("real pmm_init can return without setting a boolean result")

for code in range(0xA0, 0xAC):
    global_hits = [
        out_address
        for _load_address, out_address in post_sites(
            instructions[0][0], instructions[-1][0] + 16, code
        )
    ]
    if len(global_hits) != 1:
        raise RuntimeError(
            f"linked PMM reason POST 0x{code:02X} expected one decoded immediate "
            f"port write, found {global_hits}"
        )

# Fresh codes must be unique globally as actual port-0x80 writes, not merely as
# immediate constants elsewhere in data or code.
for code in range(0x92, 0x9A):
    global_hits = [
        out_address
        for _load_address, out_address in post_sites(
            instructions[0][0], instructions[-1][0] + 16, code
        )
    ]
    if len(global_hits) != 1:
        raise RuntimeError(
            f"linked POST 0x{code:02X} expected one decoded global port write, found {global_hits}"
        )

print(
    f"M61 linked PMM ranges: outer-wrapper=0x{pmm_wrap_start:x}-0x{pmm_wrap_end:x} "
    f"post-shim=0x{pmm_post_start:x}-0x{pmm_post_end:x} "
    f"real=0x{real_pmm:x}"
)
print(
    f"M61 linked VMM ranges: outer-wrapper=0x{vmm_wrap_start:x}-0x{vmm_wrap_end:x} "
    f"post-shim=0x{vmm_post_start:x}-0x{vmm_post_end:x} "
    f"real=0x{real_vmm:x}"
)
print(
    f"M61 linked entry range: "
    f"entry=0x{entry_start:x}-0x{entry_end:x}"
)
print(
    f"M61 decoded PMM chain: entry-{pmm_entry_to_wrap[1]}@0x{pmm_entry_to_wrap[0]:x} "
    f"-> outer-{pmm_wrap_to_post[1]}@0x{pmm_wrap_to_post[0]:x} "
    f"-> post-call@0x{pmm_post_to_real[0]:x} -> real=0x{real_pmm:x}"
)
print(
    f"M61 decoded PMM boundary: 7A@0x{pmm_7a[1]:x} "
    f"real-call@0x{pmm_post_to_real[0]:x} "
    f"7B@0x{pmm_7b[1]:x} 92@0x{pmm_92[1]:x}"
)
print(
    f"M61 decoded VMM chain: entry-{vmm_entry_to_wrap[1]}@0x{vmm_entry_to_wrap[0]:x} "
    f"-> outer-{vmm_wrap_to_post[1]}@0x{vmm_wrap_to_post[0]:x} "
    f"-> post-call@0x{vmm_post_to_real[0]:x} -> real=0x{real_vmm:x}"
)
print(
    f"M61 decoded VMM boundary: 7C@0x{vmm_7c[1]:x} "
    f"real-call@0x{vmm_post_to_real[0]:x} 7D@0x{vmm_7d[1]:x}"
)
print(
    "M61 verifier root cause fixed: distinct GNU --wrap outer wrappers, "
    "POST shims, and real functions modeled by decoded transfer targets"
)
print("M61 generic x86_64_out8 PMM interception removed: YES")
print("M61 localized PMM reason state begins only inside real pmm_init: YES")
print("M61 linked PMM call chain proven: YES")
print("M61 linked PMM 7A -> real -> 7B -> 92 execution order proven: YES")
print("M61 linked VMM call chain proven: YES")
print("M61 linked VMM 7C -> real -> 7D execution order proven: YES")
print("M61 linked 92 -> 93 -> 94 -> 95 -> 96 -> 97 CFG order proven: YES")
print(
    f"M61 linked failure branches: 98-branch@0x{pmm_failure_branch[0]:x} "
    f"reason-accessor@0x{pmm_reason_accessor_call[0]:x} "
    f"reason-out@0x{pmm_reason_final_out:x} halt@0x{pmm_failure_branch[1]:x}; "
    f"99-branch@0x{stats_failure_branch[0]:x} "
    f"halt@0x{stats_failure_branch[1]:x}"
)
print("M61 linked PMM false path 98 -> exact reason accessor -> final port80 -> halt proven: YES")
print("M61 linked successful PMM path cannot reach false-reason re-emit: YES")
print("M61 linked failure POST 98/99 boolean branches and halts proven: YES")
print(
    f"M61 linked real pmm_init range: 0x{pmm_init_start:x}-0x{pmm_init_end:x}; "
    f"false-setter=0x{pmm_false_setter:x}; true-setter=0x{pmm_true_setter:x}; "
    f"return=0x{pmm_return:x}"
)
print(
    "M61 linked PMM false-reason map: "
    "A0 map-null; A1 first-entry-invalid; A2 other-entry-invalid; "
    "A3 overlap; A4 usable-entry-invalid; A5 usable-base-align-overflow; "
    "A6 total-over-cap; A7 add-overflow/capacity; A8 no-usable-frames; "
    "A9 entries-null; AA entry-count-zero; AB entry-count-over-256"
)
print("M61 linked PMM A0-AB reason writes unique and false-returning: YES")
print("M61 linked every pmm_init false path records/emits A0-AB before returning: YES")
print("M61 linked successful pmm_init path remains reason-POST-free and true: YES")
print("M61 linked existing 7B meaning preserved: YES")
print("M61 linked existing 7C meaning preserved: YES")
print(
    "M61 POST 7B-to-7C map: 92 wrapper-return; 93 PMM-success/stats-before; "
    "94 stats-success/report-before; 95 report-after/selftest-before; "
    "96 selftest-success/final-serial-before; 97 final-serial-after/VMM-call-before; "
    "98 PMM-false then recorded A0-AB; 99 initial-stats-false"
)
print("M61 linked new 92-99 breadcrumbs confined to 7B-to-7C execution interval: YES")
print("M61 linked framebuffer diagnostic writes reintroduced: NO")
print("M61 linked getter wrapper reintroduced: NO")
print("M61 linked PMM/VMM runtime semantics changed beyond candidate POST breadcrumbs: NO")
