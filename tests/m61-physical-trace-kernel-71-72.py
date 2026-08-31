from pathlib import Path
import subprocess

root = Path(__file__).resolve().parent.parent
base = root / "tests/m61-physical-trace-kernel.sh"
tmp = root / "tests/.m61-physical-trace-kernel-71-72.generated.sh"
trace_base = root / "kernel/core/m61_physical_breadcrumbs.c"
trace_tmp = root / "kernel/core/.m61_physical_breadcrumbs_71_72.generated.c"
src = base.read_text()
trace_src = trace_base.read_text()


def replace_once(old: str, new: str, label: str) -> None:
    global src
    count = src.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one anchor, found {count}")
    src = src.replace(old, new, 1)


def trace_replace_once(old: str, new: str, label: str) -> None:
    global trace_src
    count = trace_src.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one anchor, found {count}")
    trace_src = trace_src.replace(old, new, 1)


trace_replace_once(
    "#include <boring/input.h>\n",
    "#include <boring/input.h>\n#include <boring/io.h>\n",
    "direct getter POST include")
trace_replace_once(
    "    fb = boring_framebuffer_get();\n",
    "    x86_64_out8((uint16_t)0x80U, (uint8_t)0x82U);\n"
    "    fb = boring_framebuffer_get();\n"
    "    x86_64_out8((uint16_t)0x80U, (uint8_t)0x83U);\n",
    "direct 82/83 getter boundary")

replace_once(
    "    M61_POST_HEAP_INIT_AFTER = 0x7f\n};",
    "    M61_POST_HEAP_INIT_AFTER = 0x7f,\n\n"
    "    M61_POST_ACQUIRE_RESUMED = 0x80,\n"
    "    M61_POST_FRAMEBUFFER_COUNT_RETURNED = 0x81,\n"
    "    M61_POST_FRAMEBUFFER_READY_CONFIRMED = 0x82,\n"
    "    M61_POST_SELECTED_FRAMEBUFFER_RETURNED = 0x83,\n"
    "    M61_POST_SELECTED_FRAMEBUFFER_VALID = 0x84,\n"
    "    M61_POST_CANDIDATE_ENUM_ENTERED = 0x85,\n"
    "    M61_POST_FIRST_CANDIDATE_METADATA = 0x86,\n"
    "    M61_POST_FIRST_FRAMEBUFFER_WRITE_BEFORE = 0x87,\n"
    "    M61_POST_FIRST_FRAMEBUFFER_WRITE_AFTER = 0x88,\n"
    "    M61_POST_WITNESS_WRITE_BEFORE = 0x89,\n"
    "    M61_POST_WITNESS_WRITE_AFTER = 0x8a,\n"
    "    M61_POST_CANDIDATE_LOOP_COMPLETED = 0x8b,\n"
    "    M61_POST_SELECTED_WITNESS_CONFIRMED = 0x8c,\n"
    "    M61_POST_TRACE_WRITES_BEFORE = 0x8d,\n"
    "    M61_POST_TRACE_WRITES_AFTER = 0x8e,\n"
    "    M61_POST_QMP_HOLD_BEFORE = 0x8f\n"
    "};",
    "71-to-72 enum")

old_seq = (
    "const uint8_t boring_m61_post_62_to_63_sequence[] = {\n"
    "    0x70U, 0x71U, 0x72U, 0x73U, 0x74U, 0x75U, 0x76U, 0x77U,\n"
    "    0x78U, 0x79U, 0x7aU, 0x7bU, 0x7cU, 0x7dU, 0x7eU, 0x7fU\n"
    "};")
new_seq = old_seq + (
    "\nconst uint8_t boring_m61_post_71_to_72_sequence[] = {\n"
    "    0x80U, 0x81U, 0x82U, 0x83U, 0x84U, 0x85U, 0x86U, 0x87U,\n"
    "    0x88U, 0x89U, 0x8aU, 0x8bU, 0x8cU, 0x8dU, 0x8eU, 0x8fU\n"
    "};")
replace_once(old_seq, new_seq, "71-to-72 sequence marker")

replace_once(
    "static uint8_t framebuffer_boot_init_calls;\nstatic bool init_posted;",
    "static uint8_t framebuffer_boot_init_calls;\n"
    "static bool acquire_71_72_active;\n"
    "static bool init_posted;",
    "71-to-72 state")

old_cpu = (
    "void m61_post_boring_cpu_inventory_init(void) {\n"
    "    /* Reaching this proves the immediate M61 framebuffer probe/witness returned. */\n"
    "    M61_POST(M61_POST_CPU_INVENTORY_BEFORE);")
new_cpu = (
    "void m61_post_boring_cpu_inventory_init(void) {\n"
    "    /* Reaching this proves the immediate M61 framebuffer probe/witness returned. */\n"
    "    acquire_71_72_active = false;\n"
    "    M61_POST(M61_POST_CPU_INVENTORY_BEFORE);")
replace_once(old_cpu, new_cpu, "CPU boundary resets acquire tracing")

extra = r'''
#include <boring/graphics.h>
#include <boring/serial.h>

uint64_t __real_boring_m61_framebuffer_count(void);
bool __real_boring_framebuffer_surface_valid(
    const struct boring_framebuffer *surface);
bool __real_boring_m61_framebuffer_get(
    uint64_t index, struct boring_framebuffer *surface);
bool __real_boring_graphics_fill_rect(
    const struct boring_framebuffer *surface,
    uint64_t x, uint64_t y, uint64_t width, uint64_t height,
    uint32_t color);
void __real_serial_write_string(const char *value);

uint64_t __wrap_boring_m61_framebuffer_count(void);
bool __wrap_boring_framebuffer_surface_valid(
    const struct boring_framebuffer *surface);
bool __wrap_boring_m61_framebuffer_get(
    uint64_t index, struct boring_framebuffer *surface);
bool __wrap_boring_graphics_fill_rect(
    const struct boring_framebuffer *surface,
    uint64_t x, uint64_t y, uint64_t width, uint64_t height,
    uint32_t color);
void __wrap_serial_write_string(const char *value);

static uint8_t acquire_71_72_phase;
static bool candidate_enum_posted;
static bool first_candidate_metadata_posted;
static bool first_framebuffer_write_posted;
static bool first_witness_write_posted;
static uint8_t first_witness_rectangles;
static bool candidate_loop_posted;
static bool selected_witness_posted;
static bool trace_writes_started;
static bool trace_writes_completed;
static bool qmp_hold_posted;

static bool m61_post_string_equal(const char *first, const char *second) {
    size_t index;

    if ((first == NULL) || (second == NULL)) {
        return false;
    }
    for (index = 0U; index < 96U; ++index) {
        if (first[index] != second[index]) {
            return false;
        }
        if (first[index] == '\0') {
            return true;
        }
    }
    return false;
}

uint64_t __wrap_boring_m61_framebuffer_count(void) {
    uint64_t result;

    if (!acquire_71_72_active) {
        acquire_71_72_active = true;
        acquire_71_72_phase = 1U;
        M61_POST(M61_POST_ACQUIRE_RESUMED);
    }
    result = __real_boring_m61_framebuffer_count();
    if (acquire_71_72_phase == 1U) {
        M61_POST(M61_POST_FRAMEBUFFER_COUNT_RETURNED);
        acquire_71_72_phase = 2U;
    }
    return result;
}

bool __wrap_boring_framebuffer_surface_valid(
    const struct boring_framebuffer *surface) {
    const bool result = __real_boring_framebuffer_surface_valid(surface);

    if (acquire_71_72_active && (acquire_71_72_phase == 2U) && result) {
        M61_POST(M61_POST_SELECTED_FRAMEBUFFER_VALID);
        acquire_71_72_phase = 4U;
    }
    return result;
}

bool __wrap_boring_m61_framebuffer_get(
    uint64_t index, struct boring_framebuffer *surface) {
    bool result;

    if (acquire_71_72_active && (acquire_71_72_phase >= 4U) &&
        !candidate_enum_posted) {
        candidate_enum_posted = true;
        M61_POST(M61_POST_CANDIDATE_ENUM_ENTERED);
    }
    result = __real_boring_m61_framebuffer_get(index, surface);
    if (acquire_71_72_active && result && !first_candidate_metadata_posted) {
        first_candidate_metadata_posted = true;
        acquire_71_72_phase = 5U;
        M61_POST(M61_POST_FIRST_CANDIDATE_METADATA);
    }
    return result;
}

bool __wrap_boring_graphics_fill_rect(
    const struct boring_framebuffer *surface,
    uint64_t x, uint64_t y, uint64_t width, uint64_t height,
    uint32_t color) {
    const bool tiny = (x == 0ULL) && (y == 0ULL) &&
                      (width == 2ULL) && (height == 2ULL);
    const bool witness = (y == 4ULL) && (width == 8ULL) &&
                         (height == 16ULL) && (x >= 4ULL) && (x <= 60ULL);
    const bool trace_panel = (x == 76ULL) && (y == 2ULL) &&
                             (height == 34ULL);
    bool result;

    if (acquire_71_72_active && first_candidate_metadata_posted && tiny &&
        !first_framebuffer_write_posted) {
        first_framebuffer_write_posted = true;
        M61_POST(M61_POST_FIRST_FRAMEBUFFER_WRITE_BEFORE);
        result = __real_boring_graphics_fill_rect(
            surface, x, y, width, height, color);
        M61_POST(M61_POST_FIRST_FRAMEBUFFER_WRITE_AFTER);
        return result;
    }

    if (acquire_71_72_active && first_framebuffer_write_posted && witness &&
        (first_witness_rectangles < 8U)) {
        if (!first_witness_write_posted) {
            first_witness_write_posted = true;
            M61_POST(M61_POST_WITNESS_WRITE_BEFORE);
        }
        result = __real_boring_graphics_fill_rect(
            surface, x, y, width, height, color);
        ++first_witness_rectangles;
        if (first_witness_rectangles == 8U) {
            M61_POST(M61_POST_WITNESS_WRITE_AFTER);
        }
        return result;
    }

    if (acquire_71_72_active && trace_panel) {
        if (!candidate_loop_posted) {
            candidate_loop_posted = true;
            M61_POST(M61_POST_CANDIDATE_LOOP_COMPLETED);
        }
        if (!selected_witness_posted) {
            selected_witness_posted = true;
            M61_POST(M61_POST_SELECTED_WITNESS_CONFIRMED);
        }
        if (!trace_writes_started) {
            trace_writes_started = true;
            M61_POST(M61_POST_TRACE_WRITES_BEFORE);
        }
    }

    return __real_boring_graphics_fill_rect(
        surface, x, y, width, height, color);
}

void __wrap_serial_write_string(const char *value) {
    if (acquire_71_72_active &&
        m61_post_string_equal(
            value, "M61 FRAMEBUFFER TRACE SELECTED WITNESS FAILED\n") &&
        !candidate_loop_posted) {
        candidate_loop_posted = true;
        M61_POST(M61_POST_CANDIDATE_LOOP_COMPLETED);
    }

    if (acquire_71_72_active && trace_writes_started &&
        !trace_writes_completed &&
        m61_post_string_equal(value, "M61 FRAMEBUFFER TRACE READY index=")) {
        trace_writes_completed = true;
        M61_POST(M61_POST_TRACE_WRITES_AFTER);
    }

    __real_serial_write_string(value);

    if (acquire_71_72_active && trace_writes_completed && !qmp_hold_posted &&
        m61_post_string_equal(
            value, " x=4 y=4 width=64 height=16\n")) {
        qmp_hold_posted = true;
        M61_POST(M61_POST_QMP_HOLD_BEFORE);
    }
}
'''
replace_once(
    "\nEOF_POST_C\n\n# QEMU pc/q35 already owns port 0x80 as its built-in ioport80 compatibility",
    "\n" + extra + "EOF_POST_C\n\n# QEMU pc/q35 already owns port 0x80 as its built-in ioport80 compatibility",
    "append 71-to-72 wrappers")

replace_once(
    "--wrap=boring_framebuffer_boot_init --wrap=pmm_init",
    "--wrap=boring_framebuffer_boot_init --wrap=boring_m61_framebuffer_count "
    "--wrap=boring_framebuffer_surface_valid --wrap=boring_m61_framebuffer_get "
    "--wrap=boring_graphics_fill_rect --wrap=serial_write_string --wrap=pmm_init",
    "linker wrappers")

replace_once(
    "kernel/core/m61_physical_breadcrumbs.c kernel/core/m61_post80_generated.c",
    "kernel/core/.m61_physical_breadcrumbs_71_72.generated.c "
    "kernel/core/m61_post80_generated.c",
    "candidate direct 82/83 source")

replace_once(
    "nm build/kernel.elf | grep -Fq 'boring_m61_post_62_to_63_sequence'",
    "nm build/kernel.elf | grep -Fq 'boring_m61_post_62_to_63_sequence'\n"
    "nm build/kernel.elf | grep -Fq 'boring_m61_post_71_to_72_sequence'",
    "71-to-72 nm marker")

replace_once(
    '    "__wrap_boring_framebuffer_boot_init": ("70", "71", "78", "79"),\n',
    '    "__wrap_boring_framebuffer_boot_init": ("70", "71", "78", "79"),\n'
    '    "__wrap_boring_m61_framebuffer_count": ("80", "81"),\n'
    '    "__wrap_boring_framebuffer_surface_valid": ("84",),\n'
    '    "__wrap_boring_m61_framebuffer_get": ("85", "86"),\n'
    '    "__wrap_boring_graphics_fill_rect": ("87", "88", "89", "8a", "8b", "8c", "8d"),\n'
    '    "__wrap_serial_write_string": ("8b", "8e", "8f"),\n',
    "binary verifier boundaries")

replace_once(
    '        if re.search(rf"\\$0x0*{code}\\b", body, re.IGNORECASE) is None:\n',
    '        if re.search(rf"\\$0x(?:0*|f+){code}\\b", body, re.IGNORECASE) is None:\n',
    "accept sign-extended POST immediates")

old_binary_tail = (
    'if out_count < 27:\n'
    '    raise RuntimeError(f"M61 POST binary has only {out_count} milestone outputs")\n')
new_binary_tail = r'''if out_count < 42:
    raise RuntimeError(f"M61 POST binary has only {out_count} milestone outputs")

nm_output = subprocess.check_output(["nm", "-n", "build/kernel.elf"], text=True)
symbols = {}
for line in nm_output.splitlines():
    fields = line.split()
    if len(fields) < 3:
        continue
    try:
        address = int(fields[0], 16)
    except ValueError:
        continue
    symbols.setdefault(fields[-1], []).append((address, fields[1]))

for forbidden in ("__wrap_boring_framebuffer_get", "__real_boring_framebuffer_get"):
    if forbidden in symbols:
        raise RuntimeError(f"M61 linked kernel still contains forbidden getter wrapper symbol {forbidden}")

getter_rows = symbols.get("boring_framebuffer_get", [])
if len(getter_rows) != 1:
    raise RuntimeError(
        f"M61 linked kernel expected one boring_framebuffer_get symbol, found {len(getter_rows)}")
getter_address, getter_kind = getter_rows[0]
if getter_kind.lower() != "t":
    raise RuntimeError(
        f"M61 boring_framebuffer_get is not linked text code (nm type {getter_kind})")

disasm = subprocess.check_output(["objdump", "-d", "build/kernel.elf"], text=True)
disasm_reloc = subprocess.check_output(["objdump", "-dr", "build/kernel.elf"], text=True)
for forbidden in ("__wrap_boring_framebuffer_get", "__real_boring_framebuffer_get"):
    if forbidden in disasm_reloc:
        raise RuntimeError(
            f"M61 objdump -dr still exposes forbidden getter wrapper reference {forbidden}")

getter_window = subprocess.check_output(
    ["objdump", "-d", f"--start-address=0x{getter_address:x}",
     f"--stop-address=0x{getter_address + 32:x}", "build/kernel.elf"],
    text=True)
if re.search(
        rf"^\s*{getter_address:x}:\s+[0-9a-f]{{2}}",
        getter_window, re.IGNORECASE | re.MULTILINE) is None:
    raise RuntimeError("M61 boring_framebuffer_get symbol has no linked instruction code")

header_re = re.compile(r"^\s*([0-9a-f]+)\s+<([^>]+)>:$", re.IGNORECASE)
instruction_re = re.compile(
    r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2}\s+)+)\s*(.*)$",
    re.IGNORECASE)
instructions = []
owner = None
for line in disasm.splitlines():
    header = header_re.match(line)
    if header is not None:
        owner = header.group(2)
        continue
    match = instruction_re.match(line)
    if match is None:
        continue
    raw = bytes.fromhex(match.group(2))
    instructions.append((int(match.group(1), 16), raw, match.group(3), owner))

call_rows = []
for index, (address, raw, asm, call_owner) in enumerate(instructions):
    if (len(raw) == 5) and (raw[0] == 0xe8):
        displacement = int.from_bytes(raw[1:5], "little", signed=True)
        target = (address + 5 + displacement) & 0xffffffffffffffff
        if target == getter_address:
            call_rows.append((index, address, asm, call_owner))
if not call_rows:
    raise RuntimeError(
        "M61 linked kernel has no direct E8 rel32 call targeting boring_framebuffer_get")


def loads_al_code(raw: bytes, code: int) -> bool:
    if (len(raw) >= 2) and (raw[0] == 0xb0) and (raw[1] == code):
        return True
    if (len(raw) == 5) and (raw[0] == 0xb8) and (raw[1] == code):
        return True
    if (len(raw) == 3) and (raw[0] == 0xc6) and (raw[1] == 0xc0) and (raw[2] == code):
        return True
    if (len(raw) == 6) and (raw[0] == 0xc7) and (raw[1] == 0xc0) and (raw[2] == code):
        return True
    if ((len(raw) == 7) and (raw[0] == 0x48) and
        (raw[1] == 0xc7) and (raw[2] == 0xc0) and (raw[3] == code)):
        return True
    return False


def is_port80_out(raw: bytes) -> bool:
    return (len(raw) == 2) and (raw[0] == 0xe6) and (raw[1] == 0x80)


def post_before(call_index: int, code: int):
    lower = max(0, call_index - 12)
    for out_index in range(call_index - 1, lower - 1, -1):
        if not is_port80_out(instructions[out_index][1]):
            continue
        for load_index in range(out_index - 1, max(lower - 1, out_index - 5), -1):
            if loads_al_code(instructions[load_index][1], code):
                return load_index, out_index
    return None


def post_after(call_index: int, code: int):
    upper = min(len(instructions), call_index + 13)
    for out_index in range(call_index + 1, upper):
        if not is_port80_out(instructions[out_index][1]):
            continue
        for load_index in range(max(call_index + 1, out_index - 4), out_index):
            if loads_al_code(instructions[load_index][1], code):
                return load_index, out_index
    return None

boundary = None
for call_index, call_address, call_asm, call_owner in call_rows:
    before = post_before(call_index, 0x82)
    after = post_after(call_index, 0x83)
    if (before is not None) and (after is not None):
        boundary = (call_index, call_address, call_asm, call_owner, before, after)
        break
if boundary is None:
    raise RuntimeError(
        "M61 real getter E8 call is not bracketed by direct POST 82/out and POST 83/out")

call_index, call_address, call_asm, call_owner, before, after = boundary
before_load, before_out = before
after_load, after_out = after
if not (instructions[before_load][0] < instructions[before_out][0] < call_address <
        instructions[after_load][0] < instructions[after_out][0]):
    raise RuntimeError("M61 direct 82/getter/83 machine-code order is not preserved")

acquire_symbols = sorted(
    name for name in symbols
    if (name == "acquire_framebuffers") or name.startswith("acquire_framebuffers."))
acquire_symbol_state = ",".join(acquire_symbols) if acquire_symbols else "optimized-into-caller"
print(f"M61 linked acquire_framebuffers symbol state: {acquire_symbol_state}")
print(f"M61 linked real getter E8 caller: {call_owner or 'unknown'}")
print(f"M61 linked real getter address: 0x{getter_address:x}")
print(f"M61 linked real getter call address: 0x{call_address:x}")
'''
replace_once(old_binary_tail, new_binary_tail, "linked real getter verifier")

replace_once(
    'print("M61 POST 62-to-63 bisector: 70 71 72 73 74 75 76 77 78 79 7A 7B 7C 7D 7E 7F then 63")\n',
    'print("M61 POST 62-to-63 bisector: 70 71 72 73 74 75 76 77 78 79 7A 7B 7C 7D 7E 7F then 63")\n'
    'print("M61 POST 71-to-72 bisector: 80 81 82 83 84 85 86 87 88 89 8A 8B 8C 8D 8E 8F then 72")\n'
    'print("M61 getter linker wrap removed: YES")\n'
    'print("M61 direct source 82/83 boundary: YES")\n'
    'print("M61 linked real getter E8 boundary: YES")\n'
    'print("M61 first actual framebuffer memory write witness: 87 before / 88 after")\n',
    "verifier report")

if "--wrap=boring_framebuffer_get" in src:
    raise RuntimeError("M61 getter linker wrap unexpectedly remains in generated link")
tmp.write_text(src)
trace_tmp.write_text(trace_src)
try:
    subprocess.run(["sh", str(tmp)], cwd=root, check=True)
finally:
    tmp.unlink(missing_ok=True)
    trace_tmp.unlink(missing_ok=True)