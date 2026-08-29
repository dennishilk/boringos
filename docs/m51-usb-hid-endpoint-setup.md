# M51 — bounded USB device configuration and HID endpoint setup

Status: implementation COMPLETE and Semantic Frozen.

M51 extends the merged M50 addressed/descriptors_ready xHCI state only far enough to configure the bounded HID interfaces required by the direct-root-port QEMU usb-kbd and usb-tablet devices. It does not submit HID report transfers or expose USB input to BoringOS.

## Implemented runtime boundary

For each validated M50 configuration block, M51 structurally walks interface and endpoint descriptors without assuming adjacency beyond USB descriptor bounds. Unknown descriptors are skipped structurally. Only alternate-setting-zero HID interfaces are selected; M51 does not issue SET_INTERFACE. Selected endpoint descriptors must be IN, INTERRUPT, non-zero, uniquely mapped to an xHCI DCI, and satisfy speed-specific packet/interval bounds.

The USB endpoint address is mapped to its xHCI endpoint ID from the real descriptor. Full/Low-Speed interrupt intervals use the xHCI base-2 125 us encoding; High-Speed uses bInterval-1. SuperSpeed HID endpoint configuration is deliberately rejected in M51 because companion-descriptor semantics are outside this bounded milestone rather than being guessed.

EP0 is extended only for the exact standard no-data SET_CONFIGURATION request. Its Setup and Status Stage TRBs retain the established producer/cycle/Link bounds; no public arbitrary control-transfer API is introduced. The terminal EP0 Transfer Event must match the exact slot, Endpoint ID 1, owned Status TRB pointer, success code and zero residual.

Each selected HID Interrupt-IN endpoint receives a PMM-owned bounded transfer ring with its own Link TRB. M51 builds either 32-byte or 64-byte Configure Endpoint input contexts from the real descriptor facts, sets Slot plus endpoint Add Context flags, preserves the real root port and speed, encodes the maximum DCI as Context Entries, and sets interrupt-IN endpoint type, interval, error count, maximum packet size, dequeue pointer/DCS and bounded payload fields. A real Configure Endpoint command is then submitted and its exact Command Completion Event validated. `device_configured` and `hid_endpoint_ready` become true only after the control request, ring allocation/context construction and hardware command all succeed.

Host/model coverage includes interspersed unknown descriptors, truncation, zero-length descriptors, descriptor overruns, duplicate endpoints, OUT/non-interrupt rejection, packet and interval rejection, endpoint-address→DCI mapping, exact SET_CONFIGURATION setup/no-data status semantics, EP0 Link/cycle bounds, 32/64-byte endpoint contexts, PMM-ring alignment, Configure Endpoint construction/completion, failure-state immutability, canaries and ASan/UBSan.

## Frozen real-QEMU evidence

The exact acceptance uses the established real devices:

```text
-device qemu-xhci,id=xhci
-device usb-kbd,bus=xhci.0
-device usb-tablet,bus=xhci.0
```

After real M49 addressing and real M50 descriptor discovery, the frozen reference run dynamically reported:

```text
M51 configured device port=5 slot=1 configuration=1 hid_endpoints=1
M51 HID endpoint slot=1 address=129 endpoint_id=3 max_packet=8 interval=7 xhci_interval=6
M51 configured device port=6 slot=2 configuration=1 hid_endpoints=1
M51 HID endpoint slot=2 address=129 endpoint_id=3 max_packet=8 interval=4 xhci_interval=3
M51 real SET_CONFIGURATION completions: 2
M51 real Configure Endpoint completions: 2
M51 real Transfer Events consumed: 10
M51 xHCI HID endpoint setup QEMU passed.
```

Those ports, slots, configuration value, endpoint address/DCI, packet size and intervals are evidence only and are not production constants.

## Semantic Freeze

Implementation SHA: `67bf133ecd8c849bd7e3e3b4793b1efcafbf7700`.
Tree: `6413aab58139db5c35739e68510a39b71634db50`.

Exact-head SUCCESS before freeze documentation:
- M51 #1 / 33230074407
- M50 #18 / 33230074419
- M49 #23 / 33230074548
- M48 #28 / 33230074353
- M47 #31 / 33230074380
- M46 #35 / 33230074382
- M45 #39 / 33230074342
- M44 #44 / 33230074397
- M43 #48 / 33230074401
- M42 #52 / 33230074398
- M41 #56 / 33230074390
- M40 #59 / 33230074418
- M39 #62 / 33230074434
- M38 #73 / 33230074391
- M37 #106 / 33230074356
- complete Boot #572 / 33230074383

After this Semantic Freeze no M51 runtime semantics may change. The remaining M51 lifecycle is restricted to documentation and the established active-version witnesses advancing `BoringKernel 0.0.51-dev` to `BoringKernel 0.0.52-dev`. No M52 implementation is included.


Active version after runtime-neutral closeout: **BoringKernel 0.0.52-dev**.
No M52 implementation is included.
