# M35 native BoringWM

Status: implementation in progress; not frozen or complete.

Controlled base: `e7ac44c437624386cb4a5666cfe1e446a696c643`, tree
`1d92fa3e4345beb2c334c4d324d324256b6a6fc0`, BoringKernel `0.0.35-dev`.
M34 main verification: push run 419 / 32997450788, successful.

## Boundaries

`boring.wm` is a native C Ring3 policy service. Applications own their M32
pixel buffers and send grants only to `boring.display`. The display service
owns composition and cursor rendering. BoringWM never maps application pixels.
The existing M34 acceptance, protocol v1, framebuffer syscalls 37–40 and
compositor behavior remain regression gates.

M35 adds a bounded display control extension for explicit placement, clipping,
solid borders and forwarding normalized M31 events. Display does not decide
focus, tiling, ordering or keybindings. The WM decides those values.

The existing four process/task slots cannot hold display + WM + three apps.
M35 will explicitly raise this finite capacity. Blocking on only one IPC
endpoint cannot serve unrelated apps and input. A generic bounded event wait
extension will expose readiness and authenticated peer identity on handles the
caller owns; it will contain no window-management knowledge. Existing syscall
numbers and semantics remain unchanged.

## Policy

Master left, stack right; master receives 3/5 of usable horizontal space after
the inner gap. Stack remainder pixels go to earlier tiles. Defaults: outer
gap 4, inner gap 8, border 3 pixels; dark `#282828` background, focused border
`#ebdbb2`, unfocused border `#3c3836`. Tiny screens clamp gaps/borders and never
produce unsigned underflow. No floating point.

New clients take focus. Focus next/previous wraps in tiling order. Removing a
focused client transfers focus to the next surviving tile, wrapping to master.
Reorder swaps with an adjacent tile without wrapping. Super+H/K/Left/Up focus
previous; Super+J/L/Down/Right focus next. Shift adds reorder. Super+Q requests
graceful app exit once. Super+Return records that no terminal launcher exists.
Mouse focus follows real cursor motion over a managed tile, without warping.

IDs are generation checked, endpoint bound, never raw pointers. Exhausted
generation slots retire rather than alias old IDs. App registration requires
an explicitly delegated surface and a verified association between its display
connection and WM connection. Display binds manager authority to one verified
connection, not to every connection of a process.

## Scope and proof

Three separate Ring3 acceptance apps, each with an independent address space,
M32 buffer and M33 connections, will demonstrate tiling, focus, reorder,
graceful close, unsolicited exit and WM death. Real QMP -> PS/2 -> i8042 -> M31
input is required. Host policy/protocol tests, ELF audits, BoringFS byte checks,
full framebuffer validation and all historical CI remain required.

No workspace system, alternate layout, bar, launcher UI, configuration parser,
terminal, PTY, M36 or M37 work is included. Version remains `0.0.35-dev` until
a successful exact-head semantic freeze.
