from pathlib import Path
import subprocess

root = Path(__file__).resolve().parent.parent
base = root / "tests/m61-physical-trace-kernel.sh"
chain = root / "tests/m61-physical-trace-kernel-7b-7c.py"
base_src = base.read_text()


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


patched = replace_once(
    base_src,
    "kernel/core/m61_physical_breadcrumbs.c kernel/core/m61_post80_generated.c' \\\n",
    "kernel/core/m61_physical_breadcrumbs.c kernel/core/m61_wm_terminal_post.c "
    "kernel/core/m61_post80_generated.c' \\\n",
    "final physical TEST_HARNESS_C WM-terminal hook",
)
patched = replace_once(
    patched,
    "--wrap=boring_ipc_service_register --wrap=x86_64_exception_dispatch' \\\n",
    "--wrap=boring_ipc_service_register --wrap=boring_ipc_service_connect "
    "--wrap=boring_ipc_poll --wrap=boring_ipc_send --wrap=boring_ipc_receive "
    "--wrap=x86_64_syscall_dispatch_m36 --wrap=x86_64_exception_dispatch' \\\n",
    "final physical linker WM-terminal wrappers",
)
patched = replace_once(
    patched,
    "nm build/kernel.elf | grep -Fq 'boring_m61_framebuffer_fault_post_codes'\n",
    "nm build/kernel.elf | grep -Fq 'boring_m61_framebuffer_fault_post_codes'\n"
    "nm build/kernel.elf | grep -Fq 'boring_m61_wm_terminal_post_codes'\n"
    "nm build/kernel.elf | grep -Fq '__wrap_boring_ipc_service_connect'\n"
    "nm build/kernel.elf | grep -Fq '__wrap_boring_ipc_poll'\n"
    "nm build/kernel.elf | grep -Fq '__wrap_boring_ipc_send'\n"
    "nm build/kernel.elf | grep -Fq '__wrap_boring_ipc_receive'\n"
    "nm build/kernel.elf | grep -Fq '__wrap_x86_64_syscall_dispatch_m36'\n",
    "final physical linked-kernel WM-terminal symbol proof",
)

for required in (
    "kernel/core/m61_wm_terminal_post.c",
    "--wrap=boring_ipc_service_connect",
    "--wrap=boring_ipc_poll",
    "--wrap=boring_ipc_send",
    "--wrap=boring_ipc_receive",
    "--wrap=x86_64_syscall_dispatch_m36",
    "boring_m61_wm_terminal_post_codes",
):
    if required not in patched:
        raise RuntimeError(f"final physical relink missing {required}")

subprocess.run(
    ["python3", "tests/m61-wm-terminal-post-verifier.py"], cwd=root, check=True
)
base.write_text(patched)
try:
    subprocess.run(["python3", str(chain)], cwd=root, check=True)
finally:
    base.write_text(base_src)

if base.read_text() != base_src:
    raise RuntimeError("M61 WM-terminal outer relink source restoration failed")

elf = root / "build/kernel.elf"
if not elf.exists():
    raise RuntimeError(
        "M61 WM-terminal physical relink did not produce build/kernel.elf"
    )

nm_output = subprocess.check_output(["nm", str(elf)], text=True)
for required in (
    "boring_m61_wm_terminal_post_codes",
    "__wrap_boring_ipc_service_connect",
    "__wrap_boring_ipc_poll",
    "__wrap_boring_ipc_send",
    "__wrap_boring_ipc_receive",
    "__wrap_x86_64_syscall_dispatch_m36",
):
    if required not in nm_output:
        raise RuntimeError(
            f"final physical kernel missing WM-terminal symbol {required}"
        )

disasm = subprocess.check_output(["objdump", "-d", str(elf)], text=True)
if "<__wrap_x86_64_syscall_dispatch_m36>" not in disasm:
    raise RuntimeError(
        "final physical kernel does not route SYS_SPAWN through M61 wrapper"
    )

print("M61 final physical WM->automatic-terminal sub-bisector relink: PASS")
