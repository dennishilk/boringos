#!/usr/bin/env python3
from pathlib import Path
import re

R=Path(__file__).resolve().parents[1]
h=(R/"kernel/include/boring/m61_runtime_hid.h").read_text()
t=(R/"kernel/core/m61_physical_breadcrumbs.c").read_text()
w=(R/"tests/m61-physical-handoff-bisector.sh").read_text()
e=(R/"kernel/core/event_syscall.c").read_text()
u=(R/"kernel/core/usb_hid_impl.inc").read_text()
c=(R/"kernel/core/usb_hid.c").read_text()
i=(R/"kernel/core/input.c").read_text()
s=(R/"kernel/core/syscall.c").read_text()
d=(R/"user/boring-display/server.c").read_text()
b=(R/"tests/m61-build.sh").read_text()
a=(R/"kernel/arch/x86_64/xhci.c").read_text()
m=(R/"kernel/core/usb_mass_storage_impl.inc").read_text()
x=(R/"kernel/core/xhci.c").read_text()
xh=(R/"kernel/include/boring/xhci.h").read_text()

codes={n:int(v,16) for n,v in re.findall(
 r"^\s*(M61_RUNTIME_HID_POST_[A-Z_]+)\s*=\s*0x([0-9a-fA-F]{2}),?$",h,re.M)}
want={
 "M61_RUNTIME_HID_POST_A_SERVICE_LOOP":0xC9,
 "M61_RUNTIME_HID_POST_B_TRANSFER_EVENT":0xCA,
 "M61_RUNTIME_HID_POST_C_EVENT_VALIDATED":0xCB,
 "M61_RUNTIME_HID_POST_D_INPUT_QUEUED":0xCC,
 "M61_RUNTIME_HID_POST_E_INPUT_READ":0xCD}
assert codes==want, codes
control={n:int(v,16) for n,v in re.findall(
 r"^\s*(M61_POST37_[A-Z_]+)\s*=\s*0x([0-9a-fA-F]{2}),?$",h,re.M)}
want_control={
 "M61_POST37_ARM_RETURNED":0x20,
 "M61_POST37_FRAMEBUFFER_PRESENT_RETURNED":0x21,
 "M61_POST37_DISPLAY_PRESENT_RETURNED":0x22,
 "M61_POST37_DISPLAY_EVENT_LOOP_REENTRY":0x23,
 "M61_POST37_EVENT_SYSCALL_ENTRY":0x24,
 "M61_POST37_INPUT_OWNER_TRUE":0x25,
 "M61_POST37_FAST_READY":0x26,
 "M61_POST37_READY_IPC_LISTENER":0x27,
 "M61_POST37_READY_IPC_ENDPOINT":0x28,
 "M61_POST37_READY_INPUT":0x29,
 "M61_POST37_READY_FD":0x2A,
 "M61_POST37_READY_IPC_HUP":0x2B,
 "M61_POST37_POLL_ERROR":0x2C,
 "M61_POST37_INPUT_OWNER_FALSE":0x2D}
assert control==want_control, control
observations={n:int(v,16) for n,v in re.findall(
 r"^\s*(M61_RUNTIME_XHCI_POST_[A-Z_]+)\s*=\s*0x([0-9a-fA-F]{2}),?$",h,re.M)}
want_observations={
 "M61_RUNTIME_XHCI_POST_HID_ENDPOINTS_ARMED":0x5E,
 "M61_RUNTIME_XHCI_POST_ANY_CYCLE_READY_EVENT":0x5F,
 "M61_RUNTIME_XHCI_POST_PORT_STATUS_AT_HEAD":0xCE,
 "M61_RUNTIME_XHCI_POST_OTHER_EVENT_AT_HEAD":0xCF}
assert observations==want_observations, observations
claimed=set(range(0x30,0x5E))|set(range(0x61,0xC9))|set(range(0xD0,0xFF))
assert len(set(codes.values())|set(observations.values()))==9
assert not((set(codes.values())|set(observations.values()))&claimed)
assert not(set(control.values()) & (claimed | set(codes.values()) | set(observations.values())))
assert list(control.values()) == list(range(0x20,0x2E))
assert "code != (uint8_t)(m61_runtime_hid_highest + 1U)" in t
assert "void boring_m61_runtime_xhci_observe(uint8_t code)" in t
assert "m61_runtime_hid_highest >=\n         (uint8_t)M61_RUNTIME_HID_POST_B_TRANSFER_EVENT" in t
assert "m61_post37_observed = 0U;" in t
post37=t[t.index("void boring_m61_post37_witness("):t.index("void boring_m61_runtime_hid_post(")]
assert "!m61_runtime_hid_armed" in post37
assert "m61_runtime_hid_highest >=\n         (uint8_t)M61_RUNTIME_HID_POST_A_SERVICE_LOOP" in post37
assert "m61_post37_observed & bit" in post37 and "m61_post37_observed | bit" in post37
assert post37.count("x86_64_out8(")==1
assert "M61_POST(M61_POST_DESKTOP_WITNESS_WRITTEN);\n    boring_m61_runtime_hid_arm();\n    boring_m61_post37_witness((uint8_t)M61_POST37_ARM_RETURNED);" in w
fb_return=t[t.index("enum boring_framebuffer_user_result __wrap_boring_framebuffer_user_present("):t.index("static uint8_t framebuffer_fault_post_code(")]
assert fb_return.index("boring_boot_console_desktop_handoff();") < fb_return.index("M61_POST37_FRAMEBUFFER_PRESENT_RETURNED") < fb_return.index("return result;",fb_return.index("M61_POST37_FRAMEBUFFER_PRESENT_RETURNED"))
assert "-DBORING_M61_PHYSICAL_BREADCRUMBS=1" in b

dispatch=e[e.index("void x86_64_syscall_dispatch_events("):]
assert "boring_m61_runtime_hid_is_armed() &&\n            m61_post37_is_display_process(process)" in dispatch
assert dispatch.index("M61_POST37_EVENT_SYSCALL_ENTRY") < dispatch.index("result = poll_watches(process, watches, count);")
assert "m61_post37_classify_ready(process, watches, count, result);" in dispatch
assert "M61_POST37_POLL_ERROR" in dispatch
assert "!boring_m61_runtime_hid_is_armed() &&\n        (result >= 0L) && (frame->rdx == BORING_EVENT_QUERY)" in dispatch
ready=e[e.index("static void m61_post37_classify_ready("):e.index("/* No consumption, allocation or authority transfer occurs during a wait. */")]
for token in ("M61_POST37_FAST_READY","M61_POST37_READY_IPC_HUP",
              "M61_POST37_READY_IPC_LISTENER","M61_POST37_READY_IPC_ENDPOINT",
              "M61_POST37_READY_INPUT","M61_POST37_READY_FD"):
    assert token in ready
assert "watch->peer_pid == 0ULL" in ready
owner=e[e.index("if (m54_is_input_owner(process)) {"):e.index("        if (!arm_fd_watches",e.index("if (m54_is_input_owner(process)) {"))]
assert owner.index("M61_POST37_INPUT_OWNER_TRUE") < owner.index("M61_RUNTIME_HID_POST_A_SERVICE_LOOP") < owner.index("xhci_service_hid_reports(&usb_state)")
assert "M61_POST37_INPUT_OWNER_FALSE" in owner
assert "task_yield();" in owner and owner.count("result = poll_watches(process, watches, count);")>=1
assert "x86_64_enable_and_halt();" not in owner
present_probe=d[d.index("static void m61_post37_present_return_probe("):d.index("static void present(")]
assert "BORING_EVENT_QUERY" in present_probe and "boring_event_wait(&watch, 1U" in present_probe
loop_probe=d[d.index("static void m61_post37_loop_reentry_probe("):d.index("static void present(")]
assert loop_probe.count("BORING_EVENT_IPC") == 2 and "boring_event_wait(watches, 2U" in loop_probe
control_flow=d[d.index("static void control("):d.index("static void receive(")]
assert control_flow.index("present();") < control_flow.index("control_reply(endpoint, status, r->surface);") < control_flow.index("m61_post37_present_return_probe(endpoint);")
main_flow=d[d.index("int boring_main(void) {"):]
assert main_flow.index("m61_post37_loop_reentry_probe((uint32_t)listener);") < main_flow.index("boring_event_wait(watches, count, 0U)")

submit=u[u.index("static bool m52_submit_endpoint("):u.index("static uint32_t m53_usage_keycode")]
assert submit.index("runtime->expected_trb_physical = trb_physical;") < submit.index("runtime->transfer_outstanding = true;") < submit.rindex("return true;")
poll=c[c.index("static bool m60_poll_hid_reports_limit("):c.index("bool xhci_poll_hid_reports(")]
assert poll.index("m60_rearm_hid_endpoints(active, mmio)") < poll.index("M61_RUNTIME_XHCI_POST_HID_ENDPOINTS_ARMED")
assert poll.index("xhci_event_dequeue_position(active") < poll.index("M61_RUNTIME_XHCI_POST_ANY_CYCLE_READY_EVENT")
assert poll.index("type == XHCI_TRB_TYPE_PORT_STATUS_EVENT") < poll.index("M61_RUNTIME_XHCI_POST_PORT_STATUS_AT_HEAD") < poll.index("xhci_consume_port_status_event(active, &event)")
assert poll.index("M61_RUNTIME_XHCI_POST_OTHER_EVENT_AT_HEAD") < poll.index("M61_RUNTIME_HID_POST_B_TRANSFER_EVENT") < poll.index("m60_consume_hid_event_mapped(active, &event, &completed)")
assert poll.index("m60_consume_hid_event_mapped(active, &event, &completed)") < poll.index("xhci_event_dequeue_advance(active")
assert poll.index("xhci_event_dequeue_advance(active") < poll.index("m52_mmio_write64(mmio, interrupter + 0x18U")
legacy_poll=u[u.index("static bool m52_poll_hid_reports_limit("):u.index("bool xhci_poll_hid_reports(")]
assert "m52_consumed_events" not in u
assert legacy_poll.index("xhci_event_dequeue_position(active") < legacy_poll.index("xhci_event_dequeue_advance(active") < legacy_poll.index("m52_mmio_write64(mmio, interrupter + 0x18U")
mass_take=m[m.index("static bool take_event("):m.index("static bool commit_event(")]
mass_commit=m[m.index("static bool commit_event("):m.index("static bool consume_port_event(")]
assert "command_completions +" not in mass_take
assert "xhci_event_dequeue_position(msc_runtime.state" in mass_take
assert mass_commit.index("xhci_event_dequeue_advance(msc_runtime.state") < mass_commit.index("mmio_write64(msc_runtime.mmio")
arch_take=a[a.index("static bool event_take("):a.index("static bool consume_port_event(")]
assert arch_take.index("xhci_event_dequeue_position(&active_state") < arch_take.index("xhci_event_dequeue_advance(&active_state") < arch_take.index("mmio_write64(runtime_state.mmio")
assert "return xhci_consume_port_status_event(&active_state, event);" in a
assert "uint64_t event_dequeue_count;" in xh
assert "++state->event_dequeue_count;" in x
complete=u[u.index("static bool m52_complete_event("):u.index("static bool m52_poll_hid_reports_limit(")]
assert complete.index("xhci_validate_interrupt_transfer_event(") < complete.index("M61_RUNTIME_HID_POST_C_EVENT_VALIDATED") < complete.index("m52_decode_report(")
assert complete.index("m52_decode_report(") < complete.index("runtime->transfer_outstanding = false;") < complete.index("runtime->expected_trb_physical = 0ULL;")
assert complete.index("(*completed == UINT32_MAX)") < complete.index("m52_decode_report(")
assert complete.count("return true;")==1
decode=u[u.index("static bool m52_decode_report("):u.index("static bool m52_complete_event(")]
assert "descriptor->report_format == XHCI_HID_REPORT_BOOT_KEYBOARD" in decode
assert "descriptor->report_format == XHCI_HID_REPORT_BOOT_MOUSE" in decode
assert "XHCI_HID_REPORT_QEMU_ABSOLUTE_TABLET" in decode
assert "descriptor->protocol == 0U" not in decode
configuration=a[a.index("static bool configure_hid_device("):a.index("static bool discover_device_descriptors(")]
assert configuration.index("xhci_select_supported_hid_configuration(") < configuration.index("ep0_submit_set_configuration(") < configuration.index("configure_hid_boot_protocols(")
protocol=a[a.index("static bool ep0_submit_hid_set_protocol("):a.index("static bool configure_hid_boot_protocols(")]
assert "xhci_build_hid_set_protocol_control_td(" in protocol
assert protocol.index("device->control_outstanding = true;") < protocol.index("event_dispatch_wait(XHCI_EXPECT_CONTROL_NODATA_STATUS") < protocol.index("++device->set_protocol_completions;")
parser=x[x.index("bool xhci_parse_hid_configuration("):x.index("bool xhci_select_supported_hid_configuration(")]
selector=x[x.index("bool xhci_select_supported_hid_configuration("):x.index("static bool build_no_data_control_td(")]
assert "current_subclass = bytes[offset + 6U];" in parser
assert "endpoint.interface_subclass = current_subclass;" in parser
assert "XHCI_USB_HID_SUBCLASS_BOOT" in selector
assert "XHCI_HID_REPORT_QEMU_ABSOLUTE_TABLET" in selector
assert "if (format == XHCI_HID_REPORT_UNSUPPORTED) { continue; }" in selector
push=i[i.index("static bool input_push("):i.index("bool boring_input_init(")]
assert push.index("++input_state.count;") < push.index("M61_RUNTIME_HID_POST_D_INPUT_QUEUED") < push.index("return true;")
assert "watch->events = (input.queued_events != 0U) ? BORING_EVENT_READ : 0U;" in e
read=s[s.index("static uint64_t syscall_input_read("):s.index("static uint64_t syscall_input_release(")]
assert read.index("boring_input_read(process->pid, events, safe_max, &count)") < read.index("M61_RUNTIME_HID_POST_E_INPUT_READ") < read.index("syscall_copy_to_user")
assert "(boring_input_claim() != 0L)" in d and "boring_input_release" not in d

print("M61_RUNTIME_HID_BISECTOR=PASS")
print("POST_A=C9 POST_B=CA POST_C=CB POST_D=CC POST_E=CD")
print("XHCI_ARMED=5E XHCI_READY=5F XHCI_PORT_HEAD=CE XHCI_OTHER_HEAD=CF")
print("ACTIVE_M60_CA_PATH=PASS")
print("GENERIC_EVENT_DEQUEUE_BEFORE_ERDP=PASS")
print("HID_DECODE_BEFORE_ENDPOINT_COMMIT=PASS")
print("BOOT_PROTOCOL_AND_PROTOCOL0_CLASSIFICATION=PASS")
print("POST37_ARM_ORDER=PASS")
print("POST37_ARM_RETURN_WITNESS=PASS")
print("FRAMEBUFFER_PRESENT_RETURN_WITNESS=PASS")
print("DISPLAY_PRESENT_RETURN_WITNESS=PASS")
print("DISPLAY_EVENT_LOOP_REENTRY_WITNESS=PASS")
print("POST37_EVENT_SYSCALL_ENTRY_WITNESS=PASS")
print("POST37_INPUT_OWNER_TRUE_WITNESS=PASS")
print("FAST_READY_CLASSIFICATION=PASS")
print("POST37_WITNESSES_CANDIDATE_GATED=PASS")
print("POST37_WITNESSES_ONE_SHOT=PASS")
print("C9_CAN_SUPERSEDE_CONTROL_FLOW_BISECTOR=PASS")
print("EXISTING_C9_CA_CB_CC_CD_PRESERVED=PASS")
print("XHCI_EVENT_DEQUEUE_FIX_PRESERVED=PASS")
print("PHYSICAL_HID_REPORT_HANDLING_FIX_PRESERVED=PASS")
print("POST_D_AFTER_INPUT_COUNT_INCREMENT=PASS")
print("INPUT_READINESS_AND_OWNER=PASS")
