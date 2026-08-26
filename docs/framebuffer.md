# Native framebuffer graphics foundation

Milestone 30 gives BoringOS a small native graphical output path without
turning graphics into a boot dependency or a userspace ABI.

## Why the Limine framebuffer

The current x86_64 bootstrap already uses Limine. M30 therefore consumes
Limine's firmware/QEMU-provided framebuffer metadata instead of introducing a
PCI GPU driver, VGA framework, Linux framebuffer ABI, DRM stack, or another
hardware abstraction layer. The bootloader-owned protocol description is
translated once into a BoringOS-owned `struct boring_framebuffer`; the
renderer never carries Limine structures through the rest of the kernel.

Framebuffer availability is optional. The serial console remains the
boot-authoritative output path and fd 0/1/2 retain their M29 serial semantics.

## Surface ownership and validation

A usable surface records only the validated address, width, height, pitch,
byte size, bpp, RGB memory model, bytes per pixel, and RGB mask sizes/shifts.
The validator accepts bounded RGB surfaces at 24 or 32 bits per pixel. It
rejects zero geometry, unsupported bpp/memory models, invalid or overlapping
masks, `pitch < width * bytes_per_pixel`, integer overflow in row/surface size,
and an address range that cannot be represented safely.

The Limine response is inspected through a small bounded search. Unsupported
or malformed framebuffer metadata disables graphics rather than halting the
kernel.

## Integer-only software renderer

`kernel/core/graphics.c` implements a deliberately small immediate renderer:

- RGB color packing from the surface's mask sizes and shifts;
- bounded `put_pixel`;
- clipped clear/fill rectangles;
- clipped horizontal and vertical lines; and
- clipped rectangle borders.

All arithmetic is integer-only. There is no floating point, SIMD requirement,
scene graph, compositor, widget toolkit, image decoder, or heap allocation.
Every primitive validates the surface and clips/rejects coordinates before a
framebuffer write. Pixel offsets use already-validated pitch and geometry.

## Project-owned bitmap font

The dashboard uses a BoringOS-authored 5x7 bitmap alphabet with a six-pixel
advance. The source directly defines printable ASCII 32 through 126, including
letters, digits, punctuation, brackets, slash/backslash and the symbols needed
by diagnostics. Text can be drawn at integer scale; M30 uses crisp scale 1 and
2 rendering and no antialiasing or external font blob.

## Boot dashboard

`boot_dashboard.c` is composition code above the reusable primitives. It
consumes an explicit `boring_boot_dashboard_info` value instead of reaching
through unrelated subsystem globals. The visual identity is dark and
technical: restrained grid, bordered panels, cyan accent, a primitive-drawn
BoringOS mark, hierarchy, real framebuffer geometry, real detected memory and
truthful subsystem status indicators.

The persistent-root acceptance path renders the final informational screen only
after the real VirtIO-backed BoringFS root is mounted. It then continues into
`boring-init` and the serial `boring-shell`. No graphical input or graphical
shell is implied.

The renderer is intentionally one-shot. There is no redraw timer or animation
loop.

## Failure fallback

Framebuffer discovery and dashboard rendering are non-critical. Missing,
unsupported, or malformed framebuffer state produces a concise serial message
and the existing boot continues. A dashboard render failure also falls back to
serial-only operation. M30 must not turn an accepted headless BoringOS boot
into a panic.

## Verification

`tests/framebuffer-host-test.c` uses synthetic 24/32-bpp memory surfaces,
including row padding and guard canaries, to exercise surface validation,
packing, clipping, rectangles, borders and font drawing without allowing
writes outside the supplied framebuffer.

`tests/framebuffer-qemu.sh` boots the real persistent-root image with a Limine
RGB framebuffer and QEMU's standard VGA device. QMP `screendump` captures the
actual guest display to `build/framebuffer-reference.ppm`. The standard-library
validator parses that PPM, checks the guest-reported geometry, rejects blank or
black output, verifies meaningful occurrences of the BoringOS palette, samples
known composition regions, and requires the serial shell plus M29
`boringfetch`/descriptor-backed `cat` behavior to remain alive after rendering.

## Explicit M30 non-goals

M30 does not add keyboard or mouse handling, an input subsystem, graphics
syscalls, userspace framebuffer mapping, a display server, `boring-display`, a
terminal emulator, GUI clients, BoringWM, compositing, external graphics
libraries, networking, audio, or Milestone 31 work. Input is intentionally
deferred.
