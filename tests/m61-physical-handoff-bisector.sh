#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

SOURCE=tests/m61-physical-trace-kernel.sh
TEMP=tests/.m61-physical-handoff-bisector.sh

cleanup() {
    rm -f "$TEMP"
}
trap cleanup EXIT INT TERM
cp "$SOURCE" "$TEMP"

python3 - "$TEMP" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()


def replace_once(source, old, new, label):
    count = source.count(old)
    if count != 1:
        raise RuntimeError(
            f"M61 handoff bisector patch {label} expected once, found {count}")
    return source.replace(old, new, 1)


text = replace_once(
    text,
    "#include <boring/framebuffer_user.h>\n#include <boring/heap.h>\n",
    "#include <boring/framebuffer_user.h>\n"
    "#include <boring/graphics.h>\n"
    "#include <boring/heap.h>\n",
    "graphics include",
)

text = replace_once(
    text,
    "    M61_POST_DESKTOP_PRESENT = 0x6f,\n\n"
    "    M61_POST_FB_PROBE_BOOT_INIT_BEFORE = 0x70,\n",
    "    M61_POST_DESKTOP_PRESENT = 0x6f,\n"
    "    M61_POST_DESKTOP_INNER_RETURN = 0x38,\n"
    "    M61_POST_DESKTOP_HANDOFF_ENTER = 0x34,\n"
    "    M61_POST_DESKTOP_HANDOFF_RETURNED = 0x35,\n"
    "    M61_POST_DESKTOP_WITNESS_FAILED = 0x36,\n"
    "    M61_POST_DESKTOP_WITNESS_WRITTEN = 0x37,\n\n"
    "    M61_POST_FB_PROBE_BOOT_INIT_BEFORE = 0x70,\n",
    "handoff POST codes",
)

text = replace_once(
    text,
    "enum boring_framebuffer_user_result __real_boring_framebuffer_user_present(\n"
    "    struct process *process, uint32_t handle);\n\n"
    "void m61_post_serial_init(void);\n",
    "enum boring_framebuffer_user_result __real_boring_framebuffer_user_present(\n"
    "    struct process *process, uint32_t handle);\n"
    "void __real_boring_boot_console_desktop_handoff(void);\n"
    "void __wrap_boring_boot_console_desktop_handoff(void);\n\n"
    "void m61_post_serial_init(void);\n",
    "handoff prototypes",
)

handoff_wrapper = r'''void __wrap_boring_boot_console_desktop_handoff(void) {
    const struct boring_framebuffer *surface;
    uint64_t witness_x;
    uint64_t witness_y;
    uint64_t witness_width;
    uint64_t witness_height;
    uint32_t witness_color;

    M61_POST(M61_POST_DESKTOP_HANDOFF_ENTER);
    __real_boring_boot_console_desktop_handoff();
    M61_POST(M61_POST_DESKTOP_HANDOFF_RETURNED);

    surface = boring_framebuffer_get();
    if (!boring_framebuffer_surface_valid(surface)) {
        M61_POST(M61_POST_DESKTOP_WITNESS_FAILED);
        return;
    }

    witness_width = surface->width < 48ULL ? surface->width : 48ULL;
    witness_height = surface->height < 48ULL ? surface->height : 48ULL;
    witness_x = surface->width - witness_width;
    witness_y = surface->height - witness_height;
    witness_color = boring_color_pack(surface, 0xffU, 0x00U, 0xffU);
    if (!boring_graphics_fill_rect(surface, witness_x, witness_y,
                                   witness_width, witness_height,
                                   witness_color)) {
        M61_POST(M61_POST_DESKTOP_WITNESS_FAILED);
        return;
    }
    M61_POST(M61_POST_DESKTOP_WITNESS_WRITTEN);
}

'''
text = replace_once(
    text,
    "enum boring_framebuffer_user_result m61_post_boring_framebuffer_user_present(\n"
    "    struct process *process, uint32_t handle) {\n",
    handoff_wrapper
    + "enum boring_framebuffer_user_result m61_post_boring_framebuffer_user_present(\n"
      "    struct process *process, uint32_t handle) {\n",
    "handoff wrapper",
)

text = replace_once(
    text,
    "        M61_POST(M61_POST_DESKTOP_PRESENT);\n"
    "    }\n"
    "    return result;\n"
    "}\n"
    "EOF_POST_C\n",
    "        M61_POST(M61_POST_DESKTOP_PRESENT);\n"
    "        M61_POST(M61_POST_DESKTOP_INNER_RETURN);\n"
    "    }\n"
    "    return result;\n"
    "}\n"
    "EOF_POST_C\n",
    "inner present return witness",
)

text = replace_once(
    text,
    "--wrap=boring_framebuffer_user_present --wrap=boring_ipc_service_register",
    "--wrap=boring_framebuffer_user_present "
    "--wrap=boring_boot_console_desktop_handoff "
    "--wrap=boring_ipc_service_register",
    "linker handoff wrap",
)

text = replace_once(
    text,
    '    "m61_post_boring_framebuffer_user_present": ("6f",),\n}\n',
    '    "__wrap_boring_framebuffer_user_present": '
    '("39", "3a", "3b"),\n'
    '    "m61_post_boring_framebuffer_user_present": ("6f", "38"),\n'
    '    "__wrap_boring_boot_console_desktop_handoff": '
    '("34", "35", "36", "37"),\n}\n',
    "binary handoff verifier",
)

text = replace_once(
    text,
    'print("M61 USB false: F8 returned-false; reasons F9-FD replayed last")\n',
    'print("M61 USB false: F8 returned-false; reasons F9-FD replayed last")\n'
    'print("M61 desktop handoff gap: 6F present OK, 38 inner return, '
    '39 outer resumed, 3A serial enter, 3B serial returned, 34 handoff enter")\n'
    'print("M61 desktop handoff continuation: 35 returned, '
    '36 witness-failed or 37 witness-written")\n',
    "handoff verifier report",
)

path.write_text(text)
PY

sh "$TEMP"

python3 - <<'PY'
import re
import subprocess
from pathlib import Path

source = Path("kernel/core/m61_physical_breadcrumbs.c").read_text()
start = source.index(
    "enum boring_framebuffer_user_result __wrap_boring_framebuffer_user_present(")
end = source.index("\nstatic uint8_t framebuffer_fault_post_code(", start)
wrapper = source[start:end]
ordered = (
    "result = __real_boring_framebuffer_user_present(process, handle);",
    "M61_HANDOFF_POST(M61_HANDOFF_POST_OUTER_PRESENT_RESUMED);",
    "if (result != BORING_FRAMEBUFFER_USER_OK)",
    "if (final_present)",
    "desktop_presented = true;",
    "M61_HANDOFF_POST(M61_HANDOFF_POST_SERIAL_ENTER);",
    "serial_stage(stage_number, '+', label);",
    "M61_HANDOFF_POST(M61_HANDOFF_POST_SERIAL_RETURNED);",
    "boring_boot_console_desktop_handoff();",
)
positions = [wrapper.find(token) for token in ordered]
if any(position < 0 for position in positions) or positions != sorted(positions):
    raise RuntimeError(
        f"M61 handoff gap source order is incomplete: {positions!r}")

codes = {
    name: int(value, 16)
    for name, value in re.findall(
        r"^\s*(M61_HANDOFF_POST_[A-Z_]+)\s*=\s*0x([0-9a-fA-F]{2}),?$",
        source,
        re.MULTILINE,
    )
}
if codes != {
    "M61_HANDOFF_POST_OUTER_PRESENT_RESUMED": 0x39,
    "M61_HANDOFF_POST_SERIAL_ENTER": 0x3A,
    "M61_HANDOFF_POST_SERIAL_RETURNED": 0x3B,
}:
    raise RuntimeError(f"M61 handoff gap POST map changed: {codes!r}")

disassembly = {
    name: subprocess.check_output(
        ["objdump", "-d", "--disassemble=" + name, "build/kernel.elf"],
        text=True,
    )
    for name in (
        "__wrap_boring_framebuffer_user_present",
        "m61_post_boring_framebuffer_user_present",
        "__wrap_boring_boot_console_desktop_handoff",
    )
}
outer = disassembly["__wrap_boring_framebuffer_user_present"]
inner = disassembly["m61_post_boring_framebuffer_user_present"]
handoff = disassembly["__wrap_boring_boot_console_desktop_handoff"]
if not re.search(
    r"call\s+[^\n]*<m61_post_boring_framebuffer_user_present>", outer):
    raise RuntimeError("M61 outer present wrapper does not call the inner POST wrapper")
if not re.search(
    r"call\s+[^\n]*<__wrap_boring_boot_console_desktop_handoff>", outer):
    raise RuntimeError("M61 outer present wrapper does not call the wrapped handoff")
if not re.search(
    r"call\s+[^\n]*<boring_framebuffer_user_present>", inner):
    raise RuntimeError("M61 inner POST wrapper does not call the real present")
if re.search(
    r"(?:call|jmp)\s+[^\n]*<m61_post_boring_framebuffer_user_present>",
    inner,
):
    raise RuntimeError("M61 inner present wrapper recurses")
if not re.search(
    r"call\s+[^\n]*<boring_boot_console_desktop_handoff>", handoff):
    raise RuntimeError("M61 handoff wrapper does not call the real handoff")
print("M61_HANDOFF_WRAPPER_LAYERING=PASS")
print("M61_HANDOFF_GAP_SOURCE_ORDER=PASS")
PY

printf '%s\n' 'M61_PHYSICAL_HANDOFF_BISECTOR=PASS'
printf '%s\n' 'M61_POST_INNER_PRESENT_RETURN=38'
printf '%s\n' 'M61_POST_OUTER_PRESENT_RESUMED=39'
printf '%s\n' 'M61_POST_SERIAL_STAGE_ENTER=3A'
printf '%s\n' 'M61_POST_SERIAL_STAGE_RETURNED=3B'
printf '%s\n' 'M61_POST_HANDOFF_ENTER=34'
printf '%s\n' 'M61_POST_HANDOFF_RETURNED=35'
printf '%s\n' 'M61_POST_SCANOUT_WITNESS_FAILED=36'
printf '%s\n' 'M61_POST_SCANOUT_WITNESS_WRITTEN=37'
printf '%s\n' 'M61_SCANOUT_WITNESS=48x48 magenta bottom-right through boring_framebuffer_get() alias'
