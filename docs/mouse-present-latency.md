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

The queue remains FIFO and does not coalesce button transitions. Normal button
and keyboard delivery remains serialized by `input_pending`. A pointer-enter
focus event is acknowledged before the focus-only control transaction so that
subsequent pointer movement is no longer held behind focus rendering.

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

Scene, window, wallpaper and client commit changes still use the unchanged
full-frame composition path. A full scene composition refreshes the saved
cursor underlay before drawing the cursor, so overlapping content and later
cursor moves remain correct. Focus-border changes use the separately bounded
path described below.

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

## Physical result and remaining focus boundary

The exact-head candidate `497d90917a37f8f6511e42cb14ddc18e2b425144`
was physically successful on Cthulhu: ordinary cursor motion is as responsive as
Linux on the same machine. The remaining observation is a roughly 250 ms pause
only when pointer focus crosses between windows. Motion immediately returns to
full speed after the focus change.

The focus path explains that isolated pause. `wm_pointer()` changes the token,
then the historical `sync_layout()` sends unchanged placements and performs a
complete wallpaper, window and framebuffer presentation. At 800x600 this
rewrites 480,000 pixels even though only two 3-pixel focus borders changed.

The narrow follow-up keeps the existing layouts, client configuration messages,
focus policy and full-present path. A focus action now requests a bounded focus
border presentation. For the normal two-window 800x600 layout it redraws eight
non-overlapping border bands totaling 11,736 pixels, a 40.9x reduction in
framebuffer writes, and skips wallpaper and window-interior composition. One
hundred modeled transitions therefore change 1,173,600 pixels instead of
48,000,000. If a placement is missing, invalid or overlapping, the display
service falls back to the existing full composition rather than applying an
unsafe partial update.

The cursor underlay is restored before the border bands are painted and captured
again before the cursor is redrawn. Host coverage compares the partial result
byte-for-byte with a full composition while the cursor overlaps a border, then
moves the cursor and verifies that the new border underlay is restored without
trails.

The refined physical observation identifies a second, narrower boundary: the
pointer stops sharply at the window edge, the focus highlight completes, and
only then does motion continue. The bounded border implementation still kept
the scanout inside the synchronous WM input transaction: all placement RPCs,
the focus present, frame logging and finally `DISPLAY_INPUT_ACK` happened in
that order. `boring-display` deliberately stopped reading further input while
the ACK was outstanding, so even a smaller focus frame formed a hard event-flow
barrier.

There was also an indirect full-frame path. The focus transaction re-sent
`BORING_WM_CONFIGURE` to every client even though no geometry changed. All
current native clients handle `CONFIGURE` by redrawing and sending `COMMIT`, and
each successful `COMMIT` invokes the full scene composer and full framebuffer
present. Thus the first border-only fix reduced the explicit focus present but
left one full-frame present per configured client behind it.

The follow-up makes focus metadata atomic and the focus scanout input-deferred.
BoringWM sends one `DISPLAY_PRESENT_FOCUS` request containing the focused window
and both border colors instead of re-sending every unchanged placement or
client `CONFIGURE`. Geometry configures remain on real layout changes. For a
pointer-generated focus change BoringWM sends `DISPLAY_INPUT_ACK` before the
focus request.
The display service updates only the focus colors, replies without scanning out
the border frame, and coalesces repeated pending focus paints into the latest
state. Its event loop continues servicing ready input and IPC work; only when
the queue is idle does it present the same eight bounded border bands. Full
layout changes and every unsafe-placement fallback retain the established
synchronous full compose.
