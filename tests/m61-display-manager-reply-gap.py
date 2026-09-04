#!/usr/bin/env python3
"""Focused source/binary proof for the M61 9B -> manager-reply bisector."""
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]
IPC_SYSCALL = (ROOT / "kernel/core/ipc_syscall.c").read_text()
EVENT_SYSCALL = (ROOT / "kernel/core/event_syscall.c").read_text()
IPC = (ROOT / "kernel/core/ipc.c").read_text()
DISPLAY = (ROOT / "user/boring-display/server.c").read_text()
DESKTOP = (ROOT / "kernel/core/m37_desktop_test.c").read_text()
ELF = ROOT / "build/kernel.elf"


def fail(message):
    raise RuntimeError(message)


def function_body(source, signature):
    start = source.rfind(signature)
    if start < 0:
        fail(f"missing function: {signature}")
    opening = source.find("{", start)
    if opening < 0:
        fail(f"missing function body: {signature}")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening:index + 1]
    fail(f"unterminated function: {signature}")


codes = {
    name: int(value, 16)
    for name, value in re.findall(
        r"^\s*(M61_(?:WM_|DISPLAY_)[A-Z_]+)\s*=\s*"
        r"0x([0-9a-fA-F]{2})\s*,?\s*$",
        IPC_SYSCALL,
        re.MULTILINE,
    )
}
expected_existing = {
    "M61_WM_DISPLAY_CONNECT_OK": 0x9A,
    "M61_WM_MANAGER_REQUEST_SENT": 0x9B,
    "M61_WM_MANAGER_REPLY_OK": 0x9C,
    "M61_WM_DISPLAY_PRESENT_SENT": 0x9D,
    "M61_DISPLAY_PRESENT_RECEIVED": 0x9E,
    "M61_WM_MANAGER_REPLY_REJECTED": 0x9F,
}
expected_gap = {
    "M61_DISPLAY_WM_ACCEPTED": 0xC4,
    "M61_DISPLAY_MANAGER_RECEIVED": 0xC5,
    "M61_DISPLAY_MANAGER_PEER_OK": 0xC6,
    "M61_DISPLAY_MANAGER_AUTH_OK": 0xC7,
    "M61_DISPLAY_MANAGER_REPLY_SENT": 0xC8,
}
if codes != expected_existing | expected_gap:
    fail(f"M61 WM/display POST map changed: {codes!r}")
if len(set(codes.values())) != len(codes):
    fail("M61 WM/display POST codes are not unique")
claimed_before_gap = (
    set(range(0x30, 0x5E)) |
    set(range(0x61, 0xA0)) |
    set(range(0xA0, 0xC4)) |
    set(range(0xD0, 0xFF))
)
if set(expected_gap.values()) & claimed_before_gap:
    fail("M61 manager-reply gap POST codes collide with frozen meanings")

candidate_start = IPC_SYSCALL.find("#ifdef BORING_M61_PHYSICAL_BREADCRUMBS")
candidate_end = IPC_SYSCALL.find("#endif", candidate_start)
if not (0 <= candidate_start < candidate_end):
    fail("M61 IPC candidate gate missing")
for token in expected_gap:
    position = IPC_SYSCALL.find(token)
    if not (candidate_start < position < candidate_end):
        fail(f"M61 manager-reply witness escaped candidate gate: {token}")

accept = function_body(IPC_SYSCALL, "static uint64_t service_accept(")
accept_core = accept.find("result = boring_ipc_service_accept(")
accept_note = accept.find("m61_note_service_accept(process, result, endpoint);")
accept_return = accept.find("return (uint64_t)endpoint;")
if not (0 <= accept_core < accept_note < accept_return):
    fail("accepted-endpoint witness is not after successful real accept")

receive = function_body(IPC_SYSCALL, "static uint64_t ipc_receive(")
receive_core = receive.find("result = boring_ipc_receive(")
receive_note = receive.find("m61_note_ipc_receive(")
receive_copy = receive.find("copy_to_user((uintptr_t)user_payload")
if not (0 <= receive_core < receive_note < receive_copy):
    fail("manager-receive witness is not on the real successful receive path")

send = function_body(IPC_SYSCALL, "static uint64_t ipc_send(")
send_core = send.find("result = boring_ipc_send(")
send_note = send.find("m61_note_ipc_send(")
send_return = send.find("return (result == BORING_IPC_RESULT_OK)")
if not (0 <= send_core < send_note < send_return):
    fail("manager-reply witness is not after the real send result")

event = function_body(
    EVENT_SYSCALL,
    "void x86_64_syscall_dispatch_events(",
)
copy_back = event.find(
    "!user_copy((uintptr_t)frame->rdi, watches, count * sizeof(watches[0]), true)")
query_gate = event.find("(frame->rdx == BORING_EVENT_QUERY) && (count == 1U)")
auth_note = event.find("boring_m61_note_event_query(")
result_store = event.rfind("frame->result =")
if not (0 <= copy_back < query_gate < auth_note < result_store):
    fail("probe-auth witness is not after successful query copy-back")

control = function_body(DISPLAY, "static void control(")
control_sequence = (
    "display_control_validate(r, sizeof(*r))",
    "boring_endpoint_peer(endpoint)",
    "if ((status == BORING_DISPLAY_STATUS_OK) && (peer <= 0L))",
    "if (r->type == DISPLAY_INFO)",
    "else if (r->type == DISPLAY_MANAGER)",
    "boring_service_connect(BORING_WM_SERVICE, BORING_WM_SERVICE_LENGTH)",
    "boring_endpoint_peer((uint32_t)probe) != peer",
    "managed.manager_endpoint = endpoint; manager_seen = true;",
    "boring_ipc_close((uint32_t)probe)",
    "control_reply(endpoint, status, r->surface)",
)
positions = [control.find(token) for token in control_sequence]
if any(position < 0 for position in positions) or positions != sorted(positions):
    fail(f"display manager authentication/reply order changed: {positions!r}")

display_main = function_body(DISPLAY, "int boring_main(")
for token in (
    "peers[slot] = (uint32_t)ep;",
    "watches[count++] = (struct boring_event_watch){BORING_EVENT_IPC, peers[index]",
    "else { receive(watches[index].handle); }",
):
    if token not in display_main:
        fail(f"accepted display endpoint is not carried into event wait: {token}")
if "incoming_queue(connection, entry->side)->count != 0U" not in IPC:
    fail("accepted endpoint queue is not polled for READ")

m54_owner_start = event.find("if (m54_is_input_owner(process)) {")
m54_owner_end = event.find("#endif", m54_owner_start)
if not (0 <= m54_owner_start < m54_owner_end):
    fail("M54 input-owner event-wait branch missing")
m54_owner = event[m54_owner_start:m54_owner_end]
m54_yield = m54_owner.find("task_yield();")
m54_repoll_disable = m54_owner.find("x86_64_interrupts_disable();", m54_yield)
m54_repoll = m54_owner.find(
    "result = poll_watches(process, watches, count);",
    m54_repoll_disable,
)
m54_ready_gate = m54_owner.find("if (result != 0L) {", m54_repoll)
m54_ready_break = m54_owner.find("break;", m54_ready_gate)
m54_continue = m54_owner.find("continue;", m54_ready_break)
if not (
    0 <= m54_yield < m54_repoll_disable < m54_repoll <
    m54_ready_gate < m54_ready_break < m54_continue
):
    fail("M54 event wait lost its post-yield readiness re-poll")
if "x86_64_enable_and_halt();" in m54_owner:
    fail("M54 polled HID wait still depends on an unrelated interrupt to leave HLT")
if event.count("result = poll_watches(process, watches, count);") < 2:
    fail("M54 event wait missing post-yield readiness re-poll")
if "!timer_init(100U)" not in DESKTOP:
    fail("M54 physical desktop lost its 100 Hz scheduler timer")

accept_witness = function_body(
    IPC_SYSCALL,
    "static void m61_note_service_accept(",
)
if "!m61_manager_request_posted" in accept_witness:
    fail("C4 incorrectly depends on DISPLAY_MANAGER request timing")
for token in (
    'm61_process_name_ends_with(process, "boring-display")',
    "boring_ipc_poll(process, endpoint, &events, &peer_pid)",
    "(peer_pid != m61_wm_pid)",
    "M61_DISPLAY_WM_ACCEPTED",
):
    if token not in accept_witness:
        fail(f"C4 exact accepted-WM identity proof changed: {token}")

if not ELF.is_file():
    fail("build/kernel.elf missing; run the M61 build first")
symbols = subprocess.check_output(["nm", str(ELF)], text=True)
for symbol in (
    "boring_m61_note_event_query",
    "x86_64_syscall_dispatch_events",
):
    if re.search(rf" [Tt] {re.escape(symbol)}$", symbols, re.MULTILINE) is None:
        fail(f"linked M61 manager-reply witness missing: {symbol}")
disassembly = subprocess.check_output(["objdump", "-d", str(ELF)], text=True)
instructions = []
for line in disassembly.splitlines():
    match = re.match(
        r"^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2}\s+)*"
        r"([a-z][a-z0-9]*)\s*(.*)$",
        line,
        re.IGNORECASE,
    )
    if match is not None:
        instructions.append((match.group(1).lower(), match.group(2).strip()))
linked_posts = []
for index, (mnemonic, operands) in enumerate(instructions):
    if mnemonic != "out" or re.search(
            r"%al,\$0x80\b", operands, re.IGNORECASE) is None:
        continue
    for previous in range(index - 1, max(-1, index - 5), -1):
        prior_mnemonic, prior_operands = instructions[previous]
        immediate = re.search(
            r"\$0x([0-9a-f]+),%(?:e?ax|al)\b",
            prior_operands,
            re.IGNORECASE,
        )
        if prior_mnemonic == "mov" and immediate is not None:
            linked_posts.append(int(immediate.group(1), 16) & 0xFF)
            break
        if prior_mnemonic in ("call", "ret", "out"):
            break
for code in expected_gap.values():
    if linked_posts.count(code) != 1:
        fail(
            f"linked candidate has {linked_posts.count(code)} direct "
            f"port-0x80 writes for 0x{code:02X}")

print("DISPLAY_MANAGER_REPLY_PATH_EXPLAINED=YES")
print("M61_DISPLAY_MANAGER_REPLY_BISECTOR=C4,C5,C6,C7,C8")
print("C4=DISPLAY_ACCEPTED_WM_CONNECTION")
print("C5=DISPLAY_RECEIVED_EXACT_MANAGER_REQUEST")
print("C6=CONTROL_VALIDATION_AND_ENDPOINT_PEER_PASSED")
print("C7=WM_PROBE_AUTHENTICATION_PASSED")
print("C8=CONTROL_REPLY_SEND_RETURNED_OK")
print("M54_POST_YIELD_REPOLL_BEFORE_NEXT_HID_POLL=PASS")
print("M54_POLLED_HID_WAIT_HAS_NO_HLT_DEPENDENCY=PASS")
print("M54_YIELD_READY_EVENT_NOT_LOST=PASS")
print("C4_ACCEPT_WITNESS_TIMING_INDEPENDENT=PASS")
print("DISPLAY_MANAGER_AUTHENTICATION_WEAKENED=NO")
print("M61_MANAGER_REPLY_LINKED_POSTS=PASS")
