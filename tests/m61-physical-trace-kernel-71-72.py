from pathlib import Path
import subprocess

root = Path(__file__).resolve().parent.parent
base = root / "tests/m61-physical-trace-kernel.sh"
tmp = root / "tests/.m61-physical-trace-kernel-71-72.generated.sh"
src = base.read_text()


def replace_once(old: str, new: str, label: str) -> None:
    global src
    count = src.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one anchor, found {count}")
    src = src.replace(old, new, 1)

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
const struct boring_framebuffer *__real_boring_framebuffer_get(void);
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
const struct boring_framebuffer *__wrap_boring_framebuffer_get(void);
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

const struct boring_framebuffer *__wrap_boring_framebuffer_get(void) {
    const struct boring_framebuffer *result;

    if (acquire_71_72_active && (acquire_71_72_phase == 2U)) {
        /* framebuffer_get() is only reached after the READY branch is taken. */
        M61_POST(M61_POST_FRAMEBUFFER_READY_CONFIRMED);
    }
    result = __real_boring_framebuffer_get();
    if (acquire_71_72_active && (acquire_71_72_phase == 2U)) {
        M61_POST(M61_POST_SELECTED_FRAMEBUFFER_RETURNED);
        acquire_71_72_phase = 3U;
    }
    return result;
}

bool __wrap_boring_framebuffer_surface_valid(
    const struct boring_framebuffer *surface) {
    const bool result = __real_boring_framebuffer_surface_valid(surface);

    if (acquire_71_72_active && (acquire_71_72_phase == 3U) && result) {
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
    "--wrap=boring_framebuffer_get --wrap=boring_framebuffer_surface_valid "
    "--wrap=boring_m61_framebuffer_get --wrap=boring_graphics_fill_rect "
    "--wrap=serial_write_string --wrap=pmm_init",
    "linker wrappers")

replace_once(
    "nm build/kernel.elf | grep -Fq 'boring_m61_post_62_to_63_sequence'",
    "nm build/kernel.elf | grep -Fq 'boring_m61_post_62_to_63_sequence'\n"
    "nm build/kernel.elf | grep -Fq 'boring_m61_post_71_to_72_sequence'",
    "71-to-72 nm marker")

replace_once(
    '    "__wrap_boring_framebuffer_boot_init": ("70", "71", "78", "79"),\n',
    '    "__wrap_boring_framebuffer_boot_init": ("70", "71", "78", "79"),\n'
    '    "__wrap_boring_m61_framebuffer_count": ("80", "81"),\n'
    '    "__wrap_boring_framebuffer_get": ("82", "83"),\n'
    '    "__wrap_boring_framebuffer_surface_valid": ("84",),\n'
    '    "__wrap_boring_m61_framebuffer_get": ("85", "86"),\n'
    '    "__wrap_boring_graphics_fill_rect": ("87", "88", "89", "8a", "8b", "8c", "8d"),\n'
    '    "__wrap_serial_write_string": ("8b", "8e", "8f"),\n',
    "binary verifier wrappers")

replace_once(
    "if out_count < 27:\n",
    "if out_count < 44:\n",
    "binary verifier minimum")

replace_once(
    'print("M61 POST 62-to-63 bisector: 70 71 72 73 74 75 76 77 78 79 7A 7B 7C 7D 7E 7F then 63")\n',
    'print("M61 POST 62-to-63 bisector: 70 71 72 73 74 75 76 77 78 79 7A 7B 7C 7D 7E 7F then 63")\n'
    'print("M61 POST 71-to-72 bisector: 80 81 82 83 84 85 86 87 88 89 8A 8B 8C 8D 8E 8F then 72")\n'
    'print("M61 first actual framebuffer memory write witness: 87 before / 88 after")\n',
    "verifier report")

tmp.write_text(src)
try:
    subprocess.run(["sh", str(tmp)], cwd=root, check=True)
finally:
    tmp.unlink(missing_ok=True)
