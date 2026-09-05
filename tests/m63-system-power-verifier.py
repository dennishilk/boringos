#!/usr/bin/env python3
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
M62 = "f8b23490cd2e8e9095f6623d9d8b6230d3111080"

def read(path):
    return (ROOT / path).read_text()

def fail(message):
    raise SystemExit("M63 system power verifier: " + message)

def body(source, signature):
    start = source.find(signature)
    if start < 0:
        fail("missing function " + signature)
    brace = source.find("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace:index + 1]
    fail("unterminated function " + signature)

shell = read("user/boring-shell/main.c")
abi = read("kernel/include/boring/syscall_abi.h")
runtime = read("user/runtime/syscall.c")
syscall = read("kernel/core/syscall.c")
system = read("kernel/core/system_control.c")
process = read("kernel/core/process.c")
task = read("kernel/core/task.c")
acpi = read("kernel/core/acpi.c")
s5 = read("kernel/core/acpi_s5.c")
reset = read("kernel/arch/x86_64/platform_reset.c")
block = read("kernel/core/block_device.c")
usb = read("kernel/core/usb_mass_storage_impl.inc")
ahci = read("kernel/drivers/ahci_block.c")

if '"reboot"' not in shell or '"shutdown"' not in shell:
    fail("native shell power commands missing")
if "BORING_SYS_SYSTEM_CONTROL 44" not in abi or \
   "BORING_SYSTEM_REBOOT 1U" not in abi or \
   "BORING_SYSTEM_POWEROFF 2U" not in abi or \
   "BORING_SYS_SYSTEM_CONTROL" not in runtime or \
   "case BORING_SYS_SYSTEM_CONTROL:" not in syscall:
    fail("explicit system-control syscall seam missing")
if "system_control_spawn_allowed()" not in body(process, "bool process_create"):
    fail("process creation lacks one-way power-transition gate")

execute = body(system, "void system_control_execute")
sync = execute.find('M63_STORAGE_SYNC_BEGIN')
sync_ok = execute.find('M63_STORAGE_SYNC_OK')
reboot = execute.find('M63_REBOOT_HW_ENTER')
poweroff = execute.find('M63_POWEROFF_HW_ENTER')
if min(sync, sync_ok, reboot, poweroff) < 0 or \
   not (sync < sync_ok < reboot) or not (sync < sync_ok < poweroff):
    fail("storage sync does not structurally precede both hardware transitions")
if "block_device_flush_all()" not in execute:
    fail("system transition does not invoke block-device flush seam")

production = "\n".join((system, acpi, reset))
for forbidden in ("0x604", "0x0604", "0xb004", "isa-debug-exit",
                  "debug-exit", "safe to power off"):
    if forbidden.lower() in production.lower():
        fail("emulator-only/fake production mechanism present: " + forbidden)
if "ACPI_FADT_FLAG_RESET_REG_SUP" not in acpi or \
   "gas_write8(&runtime.reset_register" not in acpi or \
   "X86_RESET_CF9_PORT" not in reset:
    fail("real ACPI reset register plus bounded x86 fallback missing")
if "boring_acpi_s5_parse" not in acpi or \
   "ACPI_PM1_SLP_TYP_SHIFT" not in acpi or \
   "ACPI_PM1_SLP_EN" not in acpi or \
   "gas_write16(&runtime.pm1a_control" not in acpi:
    fail("real firmware-derived ACPI S5 PM1 control path missing")
if "AML_NAME_OP" not in s5 or "AML_PACKAGE_OP" not in s5 or \
   "aml_pkg_length" not in s5 or "aml_integer" not in s5:
    fail("strict static _S5 parser structure missing")
for forbidden in ("aml_execute", "aml_interpreter", "AML_METHOD_OP"):
    if forbidden in s5:
        fail("general AML execution/interpreter entered scope")

if "enum block_device_result block_device_flush_all" not in block or \
   "MSC_SCSI_SYNCHRONIZE_CACHE_10" not in usb or \
   ".flush = msc_backend_flush" not in usb or \
   ".flush = ahci_backend_flush" not in ahci:
    fail("device durability seam incomplete")

for token in ("process_registry_head", "process_registry_append",
              "kmalloc(sizeof(*process))", "kfree(object)"):
    if token not in process:
        fail("M62 process architecture token missing: " + token)
for token in ("task_registry_head", "task_registry_append",
              "kmalloc(sizeof(*task))", "kfree(object)"):
    if token not in task:
        fail("M62 task architecture token missing: " + token)
if "KERNEL_PROCESS_POLICY_LIMIT 64U" not in read("kernel/include/boring/process.h") or \
   "KERNEL_TASK_POLICY_LIMIT 64U" not in read("kernel/include/boring/task.h") or \
   "#define BORING_WM_CLIENT_MAX 16U" not in read("user/runtime/include/boring/wm.h") or \
   "#define WM_PEERS 16U" not in read("user/boringwm/main.c") or \
   "#define DISPLAY_PEERS 16U" not in read("user/boring-display/server.c"):
    fail("M62 policy/desktop peer limits changed")

unchanged = [
    "kernel/core/task.c", "kernel/core/m36_syscall.c", "kernel/core/ipc.c",
    "user/runtime/include/boring/wm.h", "user/boringwm/main.c",
    "user/boring-display/server.c",
]
if subprocess.run(["git", "diff", "--quiet", M62, "HEAD", "--", *unchanged],
                  cwd=ROOT).returncode != 0:
    fail("M62 task/spawn/IPC/WM architecture files changed")

changed = set(subprocess.check_output(
    ["git", "diff", "--name-only", M62, "HEAD"], cwd=ROOT, text=True
).splitlines())
frozen_prefixes = (
    "kernel/core/xhci.c", "kernel/arch/x86_64/xhci.c",
    "kernel/core/usb_hid.c", "kernel/core/framebuffer.c",
    "kernel/core/framebuffer_user.c", "kernel/arch/x86_64/vmm.c",
    "kernel/core/pmm.c",
)
bad = sorted(path for path in changed if path in frozen_prefixes)
if bad:
    fail("frozen physical subsystem changed: " + repr(bad))

markers = (
    "REBOOT_COMMAND_PRESENT=YES",
    "SHUTDOWN_COMMAND_PRESENT=YES",
    "SYSTEM_CONTROL_SYSCALL_PRESENT=YES",
    "REBOOT_PRODUCTION_PATH_NOT_QEMU_MAGIC=YES",
    "SHUTDOWN_PRODUCTION_PATH_NOT_QEMU_MAGIC=YES",
    "REAL_PLATFORM_RESET_PATH=YES",
    "REAL_ACPI_S5_PATH=YES",
    "STORAGE_SYNC_PRECEDES_REBOOT=YES",
    "STORAGE_SYNC_PRECEDES_POWEROFF=YES",
    "NEW_SPAWN_GATE_DURING_POWER_TRANSITION=YES",
    "GENERAL_AML_INTERPRETER_ADDED=NO",
    "M62_PROCESS_ARCHITECTURE_UNCHANGED=YES",
    "M62_TASK_ARCHITECTURE_UNCHANGED=YES",
)
proof = "\n".join(markers) + "\n"
out = ROOT / "build/m63-system-power-verifier.txt"
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(proof)
print(proof, end="")
