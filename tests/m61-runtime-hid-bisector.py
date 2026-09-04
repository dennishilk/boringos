#!/usr/bin/env python3
from pathlib import Path
import re

R=Path(__file__).resolve().parents[1]
h=(R/"kernel/include/boring/m61_runtime_hid.h").read_text()
t=(R/"kernel/core/m61_physical_breadcrumbs.c").read_text()
w=(R/"tests/m61-physical-handoff-bisector.sh").read_text()
e=(R/"kernel/core/event_syscall.c").read_text()
u=(R/"kernel/core/usb_hid_impl.inc").read_text()
i=(R/"kernel/core/input.c").read_text()
s=(R/"kernel/core/syscall.c").read_text()
d=(R/"user/boring-display/server.c").read_text()

codes={n:int(v,16) for n,v in re.findall(
 r"^\s*(M61_RUNTIME_HID_POST_[A-Z_]+)\s*=\s*0x([0-9a-fA-F]{2}),?$",h,re.M)}
want={
 "M61_RUNTIME_HID_POST_A_SERVICE_LOOP":0xC9,
 "M61_RUNTIME_HID_POST_B_TRANSFER_EVENT":0xCA,
 "M61_RUNTIME_HID_POST_C_EVENT_VALIDATED":0xCB,
 "M61_RUNTIME_HID_POST_D_INPUT_QUEUED":0xCC,
 "M61_RUNTIME_HID_POST_E_INPUT_READ":0xCD}
assert codes==want, codes
claimed=set(range(0x30,0x5E))|set(range(0x61,0xC9))|set(range(0xD0,0xFF))
assert not(set(codes.values())&claimed)
assert "code != (uint8_t)(m61_runtime_hid_highest + 1U)" in t
assert "M61_POST(M61_POST_DESKTOP_WITNESS_WRITTEN);\n    boring_m61_runtime_hid_arm();" in w

owner=e[e.index("if (m54_is_input_owner(process)) {"):e.index("        if (!arm_fd_watches",e.index("if (m54_is_input_owner(process)) {"))]
assert owner.index("M61_RUNTIME_HID_POST_A_SERVICE_LOOP") < owner.index("xhci_service_hid_reports(&usb_state)")
assert "task_yield();" in owner and owner.count("result = poll_watches(process, watches, count);")>=1
assert "x86_64_enable_and_halt();" not in owner

submit=u[u.index("static bool m52_submit_endpoint("):u.index("static uint32_t m53_usage_keycode")]
assert submit.index("runtime->expected_trb_physical = trb_physical;") < submit.index("runtime->transfer_outstanding = true;") < submit.rindex("return true;")
poll=u[u.index("static bool m52_poll_hid_reports_limit("):u.index("bool xhci_poll_hid_reports(")]
assert poll.index("type = (uint8_t)") < poll.index("M61_RUNTIME_HID_POST_B_TRANSFER_EVENT") < poll.index("m52_complete_event(active, &event, &completed)")
complete=u[u.index("static bool m52_complete_event("):u.index("static bool m52_poll_hid_reports_limit(")]
assert complete.index("xhci_validate_interrupt_transfer_event(") < complete.index("M61_RUNTIME_HID_POST_C_EVENT_VALIDATED") < complete.index("m52_decode_report(")
assert complete.count("return true;")==1
push=i[i.index("static bool input_push("):i.index("bool boring_input_init(")]
assert push.index("++input_state.count;") < push.index("M61_RUNTIME_HID_POST_D_INPUT_QUEUED") < push.index("return true;")
assert "watch->events = (input.queued_events != 0U) ? BORING_EVENT_READ : 0U;" in e
read=s[s.index("static uint64_t syscall_input_read("):s.index("static uint64_t syscall_input_release(")]
assert read.index("boring_input_read(process->pid, events, safe_max, &count)") < read.index("M61_RUNTIME_HID_POST_E_INPUT_READ") < read.index("syscall_copy_to_user")
assert "(boring_input_claim() != 0L)" in d and "boring_input_release" not in d

print("M61_RUNTIME_HID_BISECTOR=PASS")
print("POST_A=C9 POST_B=CA POST_C=CB POST_D=CC POST_E=CD")
print("POST37_ARM_ORDER=PASS")
print("POST_D_AFTER_INPUT_COUNT_INCREMENT=PASS")
print("INPUT_READINESS_AND_OWNER=PASS")
