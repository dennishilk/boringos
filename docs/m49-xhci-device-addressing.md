# M49 — bounded xHCI USB device addressing

Status: implementation candidate on BoringKernel 0.0.49-dev.

M49 extends the existing M48 command/event transport only far enough to reset
connected root ports, issue and validate Enable Slot and Address Device
commands, construct bounded PMM-owned slot/EP0 contexts, install DCBAAP slot
entries and retain an explicit root-port-to-slot addressed-device state.

The implementation is limited to directly attached root-port devices. It does
not request descriptors, expose arbitrary control transfers, configure HID
interrupt endpoints, consume device-traffic transfer events, connect the M48
HID decoder to hardware, deliver input, support hubs or add storage drivers.
QEMU evidence is emulator evidence and is not a physical-hardware claim.

## Implemented boundary

The M48 MMIO and DMA mappings now remain live while a bounded producer and
consumer retain command/event indices and cycle states. Commands occupy 252
TRBs before a cycle-correct Link TRB; the event ring contains 256 TRBs. Every
completion is matched to the exact submitted command physical address and must
carry the expected event type, success code and a real slot ID within the
controller-advertised bound. Interleaved port-status events are accepted only
with a valid root-port ID and success completion code.

At most eight directly attached root-port devices are addressed. Each connected
port must become enabled through the required bounded reset path and report a
known xHCI speed value. Enable Slot supplies the slot ID; BoringOS does not
choose or predict it. Three aligned PMM frames hold the input context, output
device context and EP0 transfer ring. Both 32-byte and 64-byte context layouts
are supported. Full/low/high/Super/SuperPlus speed maps to the bounded initial
EP0 maximum-packet assumption required before descriptors are available.

Slot and default-control-endpoint contexts contain only the root-port, speed,
Context Entries, CErr, control endpoint type, maximum packet size and real EP0
dequeue pointer needed by Address Device. DCBAAP is updated for the allocated
slot before the command. Only a validated successful completion records the
port-to-slot mapping.

## Candidate evidence

Host fixtures and ASan/UBSan cover both context sizes, speed-to-EP0 assumptions,
slot/port/alignment/length rejection, canaries, exact command-pointer matching,
TRB type, completion code and slot bounds.

The focused q35 QEMU test attaches the required `usb-kbd` and `usb-tablet` to a
real emulated `qemu-xhci`. Guest-consumed controller events dynamically yielded
port 5 / slot 1 / speed 3 and port 6 / slot 2 / speed 3 in the reference run,
with four exact command completions. These values are evidence, not production
constants.
