# M68 safe higher GOP audit

## Scope and immutable physical baseline

This task starts from `main` at `b8f13fb9c2ac3e03f88225eafa9cd00856172038` (tree `f28a45e8f55f2dfb5260c00d49cda3f96ed65869`). The accepted mouse/cursor/focus behavior is outside this change boundary. USB/HID, cursor-only region presentation, cursor underlay handling, pointer-focus hit testing, immediate input ACK, atomic focus RPC, and deferred/coalesced focus-border presentation are not to be redesigned here.

The known-safe physical framebuffer request remains `800x600x32`. The existing `limine-m61-usb.conf` is intentionally left unchanged.

## Phase A — current display/boot path

- Limine is pinned by the build to version `12.5.2`.
- The physical M61 USB path selects `800x600x32` in `limine-m61-usb.conf`; the kernel itself does not hardcode that geometry in its Limine framebuffer request.
- BoringKernel consumes the runtime Limine framebuffer metadata: address, width, height, pitch, bpp, memory model, and channel masks.
- `boring_framebuffer_surface_valid()` requires `pitch >= width * bytes_per_pixel`, checks multiplication overflow, and requires `byte_size == pitch * height`.
- The M61 framebuffer normalization path maps the actual runtime framebuffer `byte_size`; it does not derive mapping size from `width * 4`.
- The dedicated M61 framebuffer virtual window is 64 MiB.
- The Limine framebuffer structure already exposes `mode_count` and `modes`; BoringOS currently does not enumerate or select those modes in kernel code.
- The userspace display scanout is intentionally a tightly packed XRGB8888 composition buffer (`stride = width * 4`). Physical GOP pitch padding is handled only when pixels are copied to the hardware framebuffer.
- Current display ABI bounds are 1920x1080 and 16 MiB for the logical scanout buffer. A 1920x1080 XRGB8888 composition buffer is 8,294,400 bytes.
- Userspace shared buffers allow up to 64 MiB, so the 1920x1080 composition buffer is within the existing buffer contract.
- `boring-display` allocates the composition buffer dynamically from the runtime scanout `byte_size`; there is no fixed 800x600 composition array.
- Cursor clipping, cursor-damage regions, managed display placement, focus-border regions, BoringWM layout, and pointer hit testing all use runtime width/height.
- Existing framebuffer host coverage already checks padded physical pitch and preservation of pitch padding. Existing region-present coverage checks bounded updates, right/bottom rejection, and a 1920-pixel-wide row. Existing WM coverage reaches the ABI maximum screen size.

## Phase B — selected safe target

The first higher physical request is `1920x1080x32` because it is a conventional mode and is exactly the maximum geometry already admitted by the current display ABI. This is a preferred firmware/Limine request, not a claim that Cthulhu firmware has already been physically proven to expose that exact mode.

A dedicated M68 Limine configuration will contain two boot entries:

1. preferred `1920x1080x32` request;
2. explicit known-safe `800x600x32` fallback entry.

Limine itself retains its normal unavailable-request fallback behavior. The explicit second entry additionally preserves a selectable known-safe 800x600 path without modifying the accepted M61 configuration.

For deterministic higher-geometry testing, use a synthetic physical pitch of 8192 bytes for 1920x1080x32. This is intentionally larger than the visible row size of 7680 bytes. The resulting physical framebuffer byte span is 8,847,360 bytes, comfortably below the 64 MiB M61 framebuffer mapping window. The logical composition buffer remains tightly packed at 8,294,400 bytes.

## Phase C — required change boundary

No production kernel, compositor, BoringWM, USB, HID, scheduler, process-model, or display-IPC change is justified by the audit for the first 1920x1080 request. The implementation should therefore be limited to:

- a dedicated higher-GOP Limine configuration that leaves `limine-m61-usb.conf` untouched;
- deterministic geometry/regression tests for 800x600 and 1920x1080 including padded physical pitch and cursor/layout edge behavior;
- an isolated candidate-image wrapper/workflow that reuses the accepted M61 image construction without changing its baseline source configuration.

If a physical Cthulhu boot exposes a narrow correctness failure, that failure must be tied to the exact candidate/hash before any additional mode or architecture change is attempted.

## Deferred

Native 3440x1440, native GPU drivers, broad GOP mode enumeration, generic M69 present optimization, and M67 typematic work are deliberately deferred.

**3440x1440 WAS NOT ATTEMPTED.**
