# Mouse-to-cursor latency: measured boundary and narrow fix

The safe starting point is main `6fa59ebe1ff7762e4df4f1142c9e0708be65b336`, tree `b4848f3a2f0aa77a5827351c963a97a818517147`. Its runtime is the physically proven M66 USB/HID desktop plus one documentation-only commit.

## Audited path

The physical USB mouse path is:

1. `xhci_service_hid_reports()` consumes one bounded Interrupt-IN completion.
2. `m52_decode_report()` normalizes the descriptor-selected mouse report.
3. `m53_publish_pointer_move()` submits a relative move and wakes the input owner.
4. The canonical 128-entry input queue stores the event without allocating.
5. The `boring-display` event loop reads one event, updates the cursor and sends the normalized event plus final coordinates to BoringWM.
6. BoringWM keeps the established pointer-enter focus policy and acknowledges the event.
7. The original display path called `display_managed_compose()` and `FRAMEBUFFER_PRESENT` for every non-zero move.
8. `FRAMEBUFFER_PRESENT` copied, converted and wrote every logical scanout pixel to the firmware framebuffer.

The queue remains FIFO and does not coalesce button transitions. `input_pending` continues to serialize display-to-WM delivery until the manager ACK, so button, keyboard and focus ordering are unchanged.

## Classification

The bottleneck is category **E: full-frame software composition and framebuffer copy**, with category **D: cursor composition** contributing through the same unnecessary work.

At the intentional physical mode of 800x600, one ordinary move previously caused at least:

- 480,000 background pixels to be recomposed, before wallpaper and live windows;
- 480,000 pixels to be copied, converted and written to the firmware framebuffer;
- a second full compose/present when that move also changed pointer focus.

The bounded host measurement executes 100 moves. The baseline performs 48,000,000 composition pixels and 48,000,000 framebuffer-copy pixels. No USB, queue, IPC or scheduler stage has comparable per-move work in the audited path.

## Narrow fix

The cursor remains a Ring-3 software cursor. The display service now keeps only the previous cursor underlay: a fixed 6x12 XRGB8888 buffer (288 bytes). For a move it:

1. restores the old cursor rectangle in the existing composition buffer;
2. clips and applies the relative movement;
3. saves the new underlying pixels;
4. draws the cursor at the new position;
5. presents the old and new rectangles through syscall 45 `FRAMEBUFFER_PRESENT_REGION`.

Scene, window, wallpaper, client commit and focus-border changes still use the unchanged full-frame composition path. A full scene composition refreshes the saved cursor underlay before drawing the cursor, so overlapping content and later cursor moves remain correct.

For a cursor fully inside the scanout, each move presents two 6x12 regions: at most 144 pixels instead of 480,000, a 3333x reduction in firmware-framebuffer writes. The deterministic 100-move test therefore presents 14,400 region pixels. The measured host loop reduced from roughly 188,000 `clock()` ticks for full recomposition to roughly 200 ticks for cursor damage on the development runner; timing is observational, while pixel counts are the acceptance metric.

The M66 QEMU harness injects 17 real USB mouse moves in total, including a 16-report successive burst across the existing xHCI -> HID -> input -> display path. It requires all 16 burst moves in Ring 3, a real pointer-focus transition, exactly 34 region presents, and exactly 2,448 damage pixels instead of the baseline-equivalent 8,160,000 full-frame pixels. Wall-clock rate is recorded for diagnostics but is not a pass/fail threshold.

## Preserved boundaries

- no xHCI, topology, HID parsing or mouse report changes;
- no input coalescing and no button-transition loss;
- no scheduler, IPC or event-wakeup change;
- no sensitivity or acceleration change;
- no GOP mode or resolution change;
- no generalized compositor damage framework;
- no heap allocation, unbounded trace or busy wait.

Host coverage verifies overlapping old/new cursor regions, unique underlying pixels, absence of trails, top-left and bottom-right clipping, clipped no-op movement, physical-pitch padding, a region wider than the 4096-byte kernel scratch buffer, authority checks and exact full/region/pixel counters.
