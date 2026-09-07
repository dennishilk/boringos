# M52 — real xHCI HID interrupt-IN report transport

Milestone 52 connects the bounded HID decode foundation to actual controller-delivered USB data for the already addressed and configured direct-root-port devices from M49–M51.

This milestone does **not** publish USB input into the normal BoringOS input queue. It proves the transport and decode boundary only.

## Implemented boundary

For each M51-configured HID Interrupt-IN endpoint, BoringOS now owns a bounded runtime state with:

- one PMM-owned 4-KiB report DMA page,
- the existing M51 PMM-owned endpoint transfer ring,
- a producer index and producer-cycle bit,
- at most one outstanding report transfer per endpoint,
- exact expected-TRB physical ownership,
- submitted/completed/short-packet/report-byte/decoded-report counters,
- bounded decoded keyboard/pointer evidence.

The transport submits real xHCI Normal TRBs with IOC and interrupt-on-short-packet, rings `DB[slot_id]` with the descriptor-derived xHCI endpoint ID, consumes real Transfer Events from the shared event ring, and accepts a completion only when slot, endpoint ID, TRB pointer, ring ownership, completion code and residual length match the outstanding transfer.

DMA bytes are not decoded until the matching hardware Transfer Event has completed successfully. Completed endpoints are then re-armed through the same bounded producer/cycle path.

The transfer-ring Link TRB remains at index 252. Producer wrap toggles the cycle state explicitly; no TD is allowed to walk outside the bounded ring.

## HID decode boundary

The M48 boot-keyboard decoder now receives actual bytes delivered by the USB keyboard endpoint. It continues to reject rollover/error usages, duplicate usages, malformed report lengths and output overflow.

The QEMU `usb-tablet` device does not use the boot-mouse report shape. M52 therefore adds one deliberately narrow six-byte absolute-tablet decoder for exactly this supported transport shape:

```text
byte 0      buttons
bytes 1..2  absolute X, little-endian
bytes 3..4  absolute Y, little-endian
byte 5      wheel
```

This is **not** a generic HID report-descriptor interpreter.

## Focused host/model proof

`tests/m52-xhci-host-test.c` covers:

- Normal-TRB construction,
- producer/cycle advancement and Link-boundary wrap,
- ring/DMA alignment and length rejection,
- exact Transfer Event type/slot/endpoint/pointer validation,
- short-packet residual arithmetic,
- stale/wrong/unsupported event rejection,
- keyboard press/release transition decoding,
- exact six-byte absolute-tablet decoding.

The permanent M52 workflow also runs these fixtures under ASan/UBSan.

## Real QEMU acceptance

The permanent QEMU gate uses actual devices:

```text
-device qemu-xhci,id=xhci
-device usb-kbd,bus=xhci.0
-device usb-tablet,bus=xhci.0
```

The guest performs the real inherited path first:

```text
M49 address devices
→ M50 discover descriptors
→ M51 SET_CONFIGURATION + Configure Endpoint
→ M52 arm real Interrupt-IN transfers
```

Only after the guest reports that the transfers are armed does the host inject a real QMP `a` key press, absolute pointer movement and `a` release. The guest must receive those changes through the real qemu-xHCI devices and controller Transfer Events; the kernel acceptance path does not call the decoder with synthetic report bytes.

Focused acceptance run `33231871215` on implementation head `bc00930901c218cb6409a686a19a898d20233ebb` completed successfully. The controller-derived evidence was:

```text
M52 HID report slot=1 endpoint_id=3 protocol=1 submitted=2 completed=2 bytes=16 short=0
M52 keyboard transitions presses=1 releases=1 last_usage=4 last_down=0
M52 HID report slot=2 endpoint_id=3 protocol=0 submitted=2 completed=1 bytes=6 short=1
M52 pointer report x=12345 y=23456 buttons=0
M52 real Interrupt-IN submissions: 4
M52 real Interrupt-IN completions: 3
M52 real HID report bytes: 22
M52 decoded HID reports: 3
M52 xHCI HID interrupt-IN QEMU passed.
```

The observed slot IDs, endpoint IDs, report lengths and event counts are evidence, not production constants. The host knows the input it injected; the production transport derives hardware state from the controller and validated descriptors.

## Explicit non-goals

M52 does not add:

- insertion into the BoringOS canonical input queue,
- graphical desktop keyboard input through USB,
- BoringWM shortcuts through USB,
- visible cursor movement through USB,
- a generic HID framework or report-descriptor interpreter,
- USB hubs or arbitrary topology support,
- hotplug/disconnect recovery,
- USB mass storage,
- AHCI or NVMe,
- a physical-hardware success claim,
- M53 semantics.

The next milestone may integrate these already decoded real USB reports with the existing source-independent BoringOS input queue. That work is outside the M52 Semantic Freeze.

## Semantic Freeze and closeout

Runtime implementation freeze: `bc00930901c218cb6409a686a19a898d20233ebb`, tree `d25179a63df620741606ecfb24381813921db12c`. All 17 PR workflows on that exact runtime head were terminal SUCCESS. After the freeze, only M52 documentation and the established active-version witnesses may change.

Runtime-neutral closeout commit: `7e8baf74132be24ca7f55c69a383d225c50e1ead`, tree `aa968b812b01fc99682a78e57936d4506100ccf2`. That commit changes only active version witnesses plus M52 closeout documentation and removes the transient closeout helper; M52 runtime semantics remain frozen.

Active version after runtime-neutral closeout: **BoringKernel 0.0.53-dev**.
No M53 implementation is included.
