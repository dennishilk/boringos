#!/usr/bin/env python3
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
FROZEN = "1e3c0e83e8e9159480782a6be624975ccbe0da3a"
M62_FROZEN = "f8b23490cd2e8e9095f6623d9d8b6230d3111080"
PROCESS_H = (ROOT / "kernel/include/boring/process.h").read_text()
TASK_H = (ROOT / "kernel/include/boring/task.h").read_text()
PROCESS = (ROOT / "kernel/core/process.c").read_text()
TASK = (ROOT / "kernel/core/task.c").read_text()
M36 = (ROOT / "kernel/core/m36_syscall.c").read_text()
IPC = (ROOT / "kernel/core/ipc.c").read_text()
WM_H = (ROOT / "user/runtime/include/boring/wm.h").read_text()
WM = (ROOT / "user/boringwm/main.c").read_text()
DISPLAY = (ROOT / "user/boring-display/server.c").read_text()

def fail(message):
    raise SystemExit(f"M62 dynamic capacity verifier: {message}")

def function_body(source, signature):
    start = source.find(signature)
    if start < 0:
        fail(f"missing function: {signature}")
    brace = source.find("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{": depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0: return source[brace:index + 1]
    fail(f"unterminated function: {signature}")

if "KERNEL_PROCESS_POLICY_LIMIT 64U" not in PROCESS_H: fail("process policy limit missing")
if "KERNEL_TASK_POLICY_LIMIT 64U" not in TASK_H: fail("task policy limit missing")
if re.search(r"static\s+struct\s+process\s+\w+\s*\[", PROCESS): fail("static process storage remains")
if re.search(r"static\s+struct\s+kernel_task\s+\w+\s*\[", TASK): fail("static task storage remains")
if "kmalloc(sizeof(*process))" not in PROCESS or "kfree(object)" not in PROCESS: fail("dynamic process ownership missing")
if "process_registry_head" not in PROCESS or "process_registry_append" not in PROCESS: fail("dynamic process registry missing")
if "kmalloc(sizeof(*task))" not in TASK or TASK.count("kfree(object)") < 2: fail("dynamic task ownership missing")
if "task_registry_head" not in TASK or "task_registry_append" not in TASK: fail("dynamic task registry missing")
if "spawn_records[" in M36 or "kmalloc(sizeof(*record))" not in M36 or "m36_record_release" not in M36: fail("spawn storage fixed")
if "process_states[" in IPC or "IPC_PROCESS_STATE_MAX" in IPC: fail("IPC process state fixed")
if "kmalloc(sizeof(*state))" not in IPC or "process_state_remove(state)" not in IPC or "kfree(state)" not in IPC: fail("IPC dynamic reclaim missing")
selector = function_body(TASK, "static struct kernel_task *task_select_next_cooperative")
if "KERNEL_TASK_POLICY_LIMIT" in selector or "registry_next" not in selector: fail("scheduler fixed traversal remains")
exit_body = function_body(TASK, "void task_exit_current_process")
reap_body = function_body(TASK, "bool task_reap_finished_process")
if "kfree(" in exit_body: fail("current task freed in place")
if "(task == current_task)" not in reap_body or "kfree(object)" not in reap_body: fail("deferred task reap guard missing")
destroy_body = function_body(PROCESS, "bool process_destroy")
if "process_registry_remove(process)" not in destroy_body or "kfree(object)" not in destroy_body: fail("process object reclaim missing")
for source, token in ((PROCESS, "KERNEL_PROCESS_MAX"), (TASK, "KERNEL_TASK_MAX"), (M36, "KERNEL_PROCESS_MAX"), (IPC, "KERNEL_PROCESS_MAX")):
    if token in source: fail(f"legacy fixed-capacity token remains: {token}")
if re.search(r"\[[^\]]*KERNEL_(?:PROCESS|TASK)_POLICY_LIMIT[^\]]*\]", PROCESS_H + TASK_H + PROCESS + TASK + M36 + IPC):
    fail("policy limit used as storage dimension")
if "#define BORING_WM_CLIENT_MAX 16U" not in WM_H: fail("WM client limit not 16")
if "#define WM_PEERS 16U" not in WM or "#define DISPLAY_PEERS 16U" not in DISPLAY: fail("desktop peer limits not 16")

allowed = {
"README.md","README.de.md","kernel/include/boring/process.h","kernel/include/boring/task.h",
"kernel/core/process.c","kernel/core/task.c","kernel/core/m36_syscall.c","kernel/core/ipc.c",
"user/runtime/include/boring/wm.h","user/boringwm/main.c","user/boring-display/server.c",
"kernel/include/boring/m62_capacity_test.h","kernel/core/m62_capacity_test.c",
"kernel/core/m62_capacity_test_adapter.c","tests/m62-capacity-qemu.sh","tests/ipc-host-test.c",
"tests/m62-dynamic-capacity-verifier.py","tests/m62-desktop-capacity-qemu.py",
".github/workflows/m62-dynamic-capacity.yml"}
changed = set(subprocess.check_output(["git","diff","--name-only",FROZEN,M62_FROZEN],cwd=ROOT,text=True).splitlines())
unexpected = sorted(changed - allowed)
if unexpected: fail(f"M61 frozen subsystems changed unexpectedly: {unexpected}")
grep = subprocess.run(["git","grep","-n","-E",r"KERNEL_PROCESS_MAX|KERNEL_TASK_MAX","--","kernel","user"],cwd=ROOT,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
if grep.returncode == 0: fail("legacy fixed capacity references remain:\n" + grep.stdout)
if grep.returncode not in (0,1): fail("git grep failed: " + grep.stderr)

markers = (
"DYNAMIC_PROCESS_STORAGE=YES","DYNAMIC_TASK_STORAGE=YES","STATIC_PROCESS_ARRAY_REMOVED=YES",
"STATIC_TASK_ARRAY_REMOVED=YES","SPAWN_RECORD_STATIC_PROCESS_LIMIT_REMOVED=YES",
"IPC_STATIC_PROCESS_LIMIT_REMOVED=YES","SCHEDULER_NO_FIXED_TASK_TABLE_TRAVERSAL=YES",
"CURRENT_TASK_STACK_NOT_FREED_IN_PLACE=YES","PROCESS_REAP_RECLAIMS_DYNAMIC_OBJECTS=YES",
"TASK_REAP_RECLAIMS_DYNAMIC_OBJECTS=YES","POLICY_LIMIT_SEPARATE_FROM_STORAGE=YES",
"M61_RUNTIME_SUBSYSTEMS_UNCHANGED=YES")
proof = "\n".join(markers) + "\n"
out = ROOT / "build/m62-dynamic-capacity-verifier.txt"
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(proof)
print(proof,end="")
