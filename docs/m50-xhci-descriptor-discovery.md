# M50 — bounded xHCI EP0 descriptor discovery

Status: implementation COMPLETE and Semantic Frozen. The runtime-neutral
closeout advances the active development banner only after this frozen runtime
state has been proven.

M50 extends the M49 addressed-device state only far enough to perform bounded,
synchronous standard `GET_DESCRIPTOR` control-IN traffic on EP0 for directly
attached xHCI root-port devices. It does not create a general USB control API.

## Implemented boundary

For every real M49-addressed device, BoringOS now submits a three-stage EP0
control-IN sequence using an inline-IDT Setup Stage TRB, one bounded IN Data
Stage TRB backed by a PMM-owned one-page descriptor buffer, and an OUT Status
Stage TRB with IOC. The EP0 producer retains its index and cycle state and
rejects a three-TRB request that cannot fit before the existing Link TRB at
index 252. Only one control transfer is outstanding for a device at a time.

The common bounded event dispatcher preserves M49 Port Status Event handling,
accepts Command Completion Events only for the exact outstanding command TRB,
and accepts Transfer Events only for the exact addressed device, slot, EP0
Endpoint ID 1, owned EP0 ring and expected data/status TRB pointers. Residual
lengths are bounded by the requested length. A Short Packet completion on the
Data Stage derives the actual byte count from the residual and still requires
the subsequent successful Status Stage event before the transfer completes.
Unknown, stale or foreign events fail explicitly.

Descriptor discovery is strictly ordered: first 8 bytes of the Device
Descriptor, speed-specific `bMaxPacketSize0` validation, optional bounded
Evaluate Context for EP0 only when the real value differs from the M49 initial
context, then the complete 18-byte Device Descriptor, the 9-byte Configuration
Descriptor header, and finally the bounded complete Configuration Descriptor
block. `descriptors_ready` is set only after all hardware events and structural
validation succeed.

The Device Descriptor must be exactly 18 bytes, type DEVICE, contain a valid
speed/MPS combination and advertise at least one configuration. The real USB
version, device class, VID and PID are retained. The Configuration Descriptor
must be type CONFIGURATION with `9 <= wTotalLength <= PMM_PAGE_SIZE`, and its
complete block must lie inside the actually received bytes. Every nested
descriptor must have `bLength >= 2` and stay within that total; unknown types
are structurally skipped rather than interpreted. M50 retains only generic
facts including configuration length and interface count.

Pure host/model coverage includes Setup/Data/Status TRB fields and little-endian
setup packing, producer index/cycle and Link-boundary rejection, physical
address/length/alignment validation, exact Transfer Event type/slot/endpoint/
pointer/completion checks, Short Packet residual handling and mandatory Status
completion, 32-byte and 64-byte Evaluate Context layouts with canaries, all
valid and invalid speed/MPS combinations, malformed Device/Configuration
Descriptors, configuration length bounds, malformed nested descriptors and
failure-state immutability. The model suite is also run under ASan/UBSan.

## Frozen real-QEMU evidence

The focused q35 acceptance attaches the required real emulated devices with:

```text
-device qemu-xhci,id=xhci
-device usb-kbd,bus=xhci.0
-device usb-tablet,bus=xhci.0
```

The reference exact-head run first completed the M49 addressing path and then
consumed real xHCI Transfer Events before validating descriptor DMA data. It
dynamically reported:

```text
M50 descriptor device port=5 slot=1 speed=3 vid=1575 pid=1 configuration_length=34 interfaces=1
M50 descriptor device port=6 slot=2 speed=3 vid=1575 pid=1 configuration_length=34 interfaces=1
M50 real transfer events: 8
M50 real descriptor bytes: 138
M50 Evaluate Context completions: 0
M50 xHCI EP0 descriptor discovery QEMU passed.
```

These observed ports, slots, speeds, descriptor facts and Evaluate Context count
are evidence only, never production constants. This QEMU evidence is emulator
evidence and makes no physical-hardware success claim.

Semantic Freeze implementation:
`7d07323d2c7f5cbe729b5cfa03b2278691416aa5`.
Tree: `e2ad5fd1ac8708baceb0034d67c76ced089c8aee`.

Exact-head SUCCESS: M50 #3 / 33227985224; M49 #8 / 33227985211;
M48 #13 / 33227985244; M47 #16 / 33227985152;
M46 #20 / 33227985208; M45 #24 / 33227985166;
M44 #29 / 33227985199; M43 #33 / 33227985165;
M42 #37 / 33227985163; M41 #41 / 33227985148;
M40 #44 / 33227985190; M39 #47 / 33227985221;
M38 #58 / 33227985215; M37 #91 / 33227985170;
complete Boot #557 / 33227985155.

After this Semantic Freeze no M50 runtime semantics may change. The separate
runtime-neutral closeout is limited to this documentation, the roadmap and the
established active version witnesses, advancing `BoringKernel 0.0.50-dev` to
`BoringKernel 0.0.51-dev`. No M51 implementation is included.


Active version after runtime-neutral closeout: **BoringKernel 0.0.51-dev**.
No M51 work is included.
