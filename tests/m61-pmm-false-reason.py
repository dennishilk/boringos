from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parent.parent
base = root / "tests/m61-physical-trace-kernel.sh"
entry = root / "kernel/core/entry.c"
pmm = root / "kernel/core/pmm.c"
previous = root / "tests/m61-physical-trace-kernel-71-72.py"
base_src = base.read_text()
entry_src = entry.read_text()
pmm_src = pmm.read_text()


def require_once(text: str, expected: str, label: str) -> None:
    count = text.count(expected)
    if count != 1:
        raise RuntimeError(f"{label}: expected one exact source anchor, found {count}")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    require_once(text, old, label)
    return text.replace(old, new, 1)


reason_blocks = {
    0xA0: '''    if (memory_map == NULL) {
        PMM_M61_FAILURE_POST(0xA0U);
        return false;
    }
''',
    0xA1: '''        if (!pmm_entry_end(entry, &first_end)) {
            PMM_M61_FAILURE_POST(0xA1U);
            return false;
        }
''',
    0xA2: '''            if (!pmm_entry_end(other, &second_end)) {
                PMM_M61_FAILURE_POST(0xA2U);
                return false;
            }
''',
    0xA3: '''            if (pmm_ranges_overlap(entry->base, first_end,
                                   other->base, second_end) &&
                pmm_overlap_requires_rejection(entry->type, other->type)) {
                PMM_M61_FAILURE_POST(0xA3U);
                return false;
            }
''',
    0xA4: '''        if (!pmm_entry_end(entry, &raw_end)) {
            pmm_reset_state();
            PMM_M61_FAILURE_POST(0xA4U);
            return false;
        }
''',
    0xA5: '''        if (!pmm_align_up(entry->base, &aligned_base)) {
            pmm_reset_state();
            PMM_M61_FAILURE_POST(0xA5U);
            return false;
        }
''',
    0xA6: '''        if (pmm_total_frames > PMM_MAX_FRAMES) {
            pmm_reset_state();
            PMM_M61_FAILURE_POST(0xA6U);
            return false;
        }
''',
    0xA7: '''        if ((pmm_total_frames > (UINT64_MAX - frame_count)) ||
            ((pmm_total_frames + frame_count) > PMM_MAX_FRAMES)) {
            pmm_reset_state();
            PMM_M61_FAILURE_POST(0xA7U);
            return false;
        }
''',
    0xA8: '''    if (pmm_total_frames == 0ULL) {
        pmm_reset_state();
        PMM_M61_FAILURE_POST(0xA8U);
        return false;
    }
''',
    0xA9: '''    if (memory_map->entries == NULL) {
        PMM_M61_FAILURE_POST(0xA9U);
        return false;
    }
''',
    0xAA: '''    if (memory_map->entry_count == 0ULL) {
        PMM_M61_FAILURE_POST(0xAAU);
        return false;
    }
''',
    0xAB: '''    if (memory_map->entry_count > PMM_MAX_MEMORY_MAP_ENTRIES) {
        PMM_M61_FAILURE_POST(0xABU);
        return false;
    }
''',
}
for code, block in reason_blocks.items():
    require_once(pmm_src, block, f"PMM direct false reason 0x{code:02X}")

require_once(
    pmm_src,
    '''    return (first_type == BORING_LIMINE_MEMMAP_USABLE) ||
           (second_type == BORING_LIMINE_MEMMAP_USABLE) ||
           (first_type == BORING_LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE) ||
           (second_type == BORING_LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE);
''',
    "PMM protected-overlap compatibility semantics",
)
require_once(
    pmm_src,
    '''#ifdef BORING_M61_PHYSICAL_BREADCRUMBS
#include <boring/io.h>
#define PMM_M61_FAILURE_POST(code) \\
    x86_64_out8((uint16_t)0x80U, (uint8_t)(code))
#else
#define PMM_M61_FAILURE_POST(code) ((void)0)
#endif
''',
    "candidate-gated direct PMM POST primitive",
)
for forbidden in (
    "last_reason", "reason_state", "reason_accessor", "pmm_failure_reason",
    "pmm_last_reason", "replay_reason",
):
    if forbidden in pmm_src.lower():
        raise RuntimeError(f"forbidden sticky/replay PMM diagnostic state: {forbidden}")

require_once(
    base_src,
    '''    result = __real_pmm_init(memmap);
    if (!result) {
        return result;
    }
    M61_POST(M61_POST_PMM_INIT_AFTER);
    return result;
''',
    "M61 false-path 7B suppression",
)

commit = subprocess.check_output(
    ["git", "cat-file", "-p", "HEAD"], cwd=root, text=True
)
parents = [
    line.split()[1] for line in commit.splitlines() if line.startswith("parent ")
]
if not parents:
    raise RuntimeError("current commit has no parent for M61 runtime scope audit")
parent = parents[0]
if subprocess.run(
    ["git", "cat-file", "-e", f"{parent}^{{commit}}"],
    cwd=root,
    stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
).returncode != 0:
    subprocess.run(
        ["git", "fetch", "--no-tags", "--depth=1", "origin", parent],
        cwd=root,
        check=True,
    )
changed = set(
    subprocess.check_output(
        ["git", "diff", "--name-only", parent, "HEAD"], cwd=root, text=True
    ).splitlines()
)
if "kernel/include/boring/io.h" in changed:
    raise RuntimeError("generic x86_64_out8 implementation changed")
if any(path.startswith("kernel/core/framebuffer") or
       path.startswith("kernel/core/graphics") for path in changed):
    raise RuntimeError("framebuffer/graphics diagnostic source changed")
for forbidden_path in (
    "kernel/core/vmm.c",
    "kernel/core/usb_mass_storage.c",
    "kernel/core/boringfs_vfs.c",
    "kernel/core/boot_console.c",
):
    if forbidden_path in changed:
        raise RuntimeError(f"frozen runtime source changed: {forbidden_path}")

base_instrumented = replace_once(
    base_src,
    "    M61_POST(M61_POST_PMM_INIT_AFTER);\n    return result;\n",
    "    M61_POST(M61_POST_PMM_INIT_AFTER);\n"
    "    /* 92: true PMM result; wrapper is about to return. */\n"
    "    M61_POST(0x92U);\n"
    "    return result;\n",
    "true-only 7B/92 wrapper boundary",
)

entry_instrumented = replace_once(
    entry_src,
    "#include <boring/irq.h>\n",
    "#include <boring/irq.h>\n#include <boring/io.h>\n",
    "candidate caller POST include",
)
entry_instrumented = replace_once(
    entry_instrumented,
    "#define TASK_TEST_TIMER_SPIN_LIMIT 50000000ULL\n",
    "#define TASK_TEST_TIMER_SPIN_LIMIT 50000000ULL\n\n"
    "#ifndef BORING_M61_PHYSICAL_BREADCRUMBS\n"
    "#error \"M61 PMM-reason caller breadcrumbs must stay candidate-build gated\"\n"
    "#endif\n"
    "static inline void m61_pmm_reason_caller_post(uint8_t code) {\n"
    "    x86_64_out8((uint16_t)0x80U, code);\n"
    "}\n",
    "candidate caller POST helper",
)

old_pmm_gate = '''    if (!pmm_init(limine_memmap_request.response) ||
        !pmm_get_stats(&pmm_stats)) {
        serial_write_string("Physical memory manager: FAILED\\n");
        x86_64_halt_forever();
    }
'''
new_pmm_gate = '''    if (!pmm_init(limine_memmap_request.response)) {
        /* Preserve the direct A0-AB pmm_init reason as the final POST code. */
        serial_write_string("Physical memory manager: FAILED\\n");
        x86_64_halt_forever();
    }
    /* 93: caller resumed and PMM result was true; stats call is next. */
    m61_pmm_reason_caller_post(0x93U);
    if (!pmm_get_stats(&pmm_stats)) {
        /* 99: first post-PMM stats query returned false. */
        m61_pmm_reason_caller_post(0x99U);
        serial_write_string("Physical memory manager: FAILED\\n");
        x86_64_halt_forever();
    }
    /* 94: first post-PMM stats query returned true; PMM report is next. */
    m61_pmm_reason_caller_post(0x94U);
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
    '    m61_pmm_reason_caller_post(0x95U);\n\n'
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
    '    m61_pmm_reason_caller_post(0x96U);\n'
    '    serial_write_string("\\nBoringKernel physical memory test passed.\\n\\n");\n'
    '    /* 97: final PMM PASS serial returned; VMM call is next. */\n'
    '    m61_pmm_reason_caller_post(0x97U);\n\n'
    '    if (!vmm_init(limine_hhdm_request.response,\n',
    "PMM self-test/final-serial/VMM boundary",
)

for code in (0x93, 0x94, 0x95, 0x96, 0x97, 0x99):
    if entry_instrumented.count(f"0x{code:02X}U") != 1:
        raise RuntimeError(f"caller breadcrumb 0x{code:02X} missing or duplicated")
if "0x98U" in entry_instrumented:
    raise RuntimeError("caller PMM-false POST 98 was reintroduced")
if base_instrumented.count("M61_POST(0x92U)") != 1:
    raise RuntimeError("true-only wrapper POST 92 missing or duplicated")

base.write_text(base_instrumented)
entry.write_text(entry_instrumented)
try:
    subprocess.run(["python3", str(previous)], cwd=root, check=True)
finally:
    base.write_text(base_src)
    entry.write_text(entry_src)

if base.read_text() != base_src or entry.read_text() != entry_src:
    raise RuntimeError("M61 PMM-reason source restoration failed")

elf = root / "build/kernel.elf"
if not elf.exists():
    raise RuntimeError("M61 PMM-reason relink did not produce build/kernel.elf")

pmm_dis = subprocess.check_output(
    ["objdump", "-d", "--disassemble=pmm_init", str(elf)], cwd=root, text=True
)
if "out" not in pmm_dis or "$0x80" not in pmm_dis:
    raise RuntimeError("linked pmm_init has no direct port-0x80 reason output")

pmm_instructions = []
for line in pmm_dis.splitlines():
    match = re.match(
        r"^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2}\s+)+\s*(.*)$", line, re.IGNORECASE
    )
    if match is not None:
        pmm_instructions.append(match.group(1).strip())
linked_reason_codes = set()
for previous_instruction, instruction in zip(pmm_instructions, pmm_instructions[1:]):
    if re.search(r"\bout\s+%al,\$0x80\b", instruction, re.IGNORECASE) is None:
        continue
    immediate = re.search(
        r"\bmov(?:abs)?\s+\$0x([0-9a-f]+),%(?:eax|rax|al)\b",
        previous_instruction,
        re.IGNORECASE,
    )
    if immediate is not None:
        linked_reason_codes.add(int(immediate.group(1), 16) & 0xFF)
for code in reason_blocks:
    if code not in linked_reason_codes:
        raise RuntimeError(f"linked pmm_init missing reason POST 0x{code:02X}")

shim_dis = subprocess.check_output(
    ["objdump", "-d", "--disassemble=m61_post_pmm_init", str(elf)],
    cwd=root, text=True,
)
for code in (0x7A, 0x7B, 0x92):
    if re.search(rf"\$0x0*{code:02x}\b", shim_dis, re.IGNORECASE) is None:
        raise RuntimeError(f"linked PMM shim missing POST 0x{code:02X}")

entry_dis = subprocess.check_output(
    ["objdump", "-d", "--disassemble=m61_post_real_boring_kernel_entry", str(elf)],
    cwd=root, text=True,
)
for code in (0x93, 0x94, 0x95, 0x96, 0x97, 0x99):
    if re.search(rf"\$0x0*{code:02x}\b", entry_dis, re.IGNORECASE) is None:
        raise RuntimeError(f"linked caller missing POST 0x{code:02X}")
if re.search(r"\$0x0*98\b", entry_dis, re.IGNORECASE) is not None:
    raise RuntimeError("linked caller still contains PMM-false POST 98")

print("M61 PMM direct A0-AB false-reason mapping: PASS")
print("M61 PMM false path preserves direct reason after 7A: PASS")
print("M61 PMM false-path 7B suppression: PASS")
print("M61 PMM false-path 92 suppression: PASS")
print("M61 caller PMM-false 98 suppression: PASS")
print("M61 PMM true-path 7A/7B/92/93+ progression preserved: PASS")
print("M61 generic x86_64_out8 modified: NO")
print("M61 framebuffer diagnostic added: NO")
