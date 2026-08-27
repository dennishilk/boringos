# M35 native BoringWM

Status: M35 implementation complete and Semantic Frozen. This is the
0.0.36-dev documentation/version closeout; final delivery verification is
recorded in PR #46.

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
M35 raises this finite capacity to eight ordinary processes and eight tasks
(bootstrap objects remain separate). Blocking on only one IPC
endpoint cannot serve unrelated apps and input. A generic bounded event wait
extension, syscall 41 EVENT_WAIT, exposes readiness and authenticated peer
identity on handles the caller owns; it contains no window-management knowledge. Existing syscall
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
M32 buffer and M33 connections, demonstrate tiling, focus, reorder,
graceful close, unsolicited exit and WM death. Real QMP -> PS/2 -> i8042 -> M31
input is required. Host policy/protocol tests, ELF audits, BoringFS byte checks,
full framebuffer validation and all historical CI remain required.

No workspace system, alternate layout, bar, launcher UI, configuration parser,
terminal, PTY, M36 or M37 work is included. The successful semantic freeze used
`0.0.35-dev`; only the subsequent closeout changes the banner to `0.0.36-dev`.


## Protocol, authority and bounds

The native `boring.wm` service uses fixed 48-byte version-1 messages. App requests
are REGISTER (delegated display surface, no geometry), QUERY (own live token),
and UNREGISTER (own live token). Replies carry a status and validated token;
CONFIGURE, CLOSE and KEY are service-to-app events, never accepted app commands.
Six managed slots use a 24-bit generation plus an 8-bit slot identity. Slots
retire on generation exhaustion. Eight peer endpoints permit bounded probes.

The display extension uses the same M33 connection and `boring.display` service,
not a second pixel protocol. Original version-1 CREATE/COMMIT/DESTROY messages
and M32 grants are retained. Version-2 management requests are fixed 56 bytes:
INFO, MANAGER, DELEGATE, BIND, PLACE, UNBIND, PRESENT and INPUT_ACK; replies and
forwarded INPUT events are also fixed 56 bytes. There are sixteen surface slots.
Apps explicitly delegate their own surfaces. BIND compares the surface owner's
kernel-authenticated PID to the authenticated peer of the registering WM
connection. Subsequent WM client authority is bound to that exact endpoint.
The display verifies the manager against the named WM service and grants
management authority to only the exact requesting endpoint. Another connection
of the same process does not inherit management authority.

EVENT_WAIT accepts 1–32 writable 24-byte watches. IPC readiness is non-consuming;
READ/HUP and peer PID are returned only for caller-owned handles. Input readiness
requires the existing exclusive M31 input claim. QUERY returns immediately;
ordinary waits block through the existing scheduler. The last runnable waiter
uses interruptible HLT, not a poll loop. IRQ1/IRQ12 wake the input owner. One
forwarded input event remains outstanding until WM ACK, bounding the display/WM
input exchange. No new kernel window, focus, layout or keybinding knowledge.

## Lifecycle and acceptance

The display, WM and A/B/C are PIDs 1/2/3/4/5 in the dedicated acceptance boot.
All five enter CPL3 with distinct CR3 roots. Apps own full scanout-sized buffers;
composition clips those buffers to WM-provided interiors without scaling. WM
never creates or maps a pixel buffer and rejects/closes unexpected grants.

The exact visual sequence is:

| Frame | Tiling order | Focus |
| --- | --- | --- |
| initial-layout | A, B, C | C |
| focus-changed after Super+J | A, B, C | A |
| reordered after Super+Shift+J | B, A, C | A |
| after-close (A gracefully exits) | B, C | C |
| after-exit (plain X makes C exit) | B | B |

The test additionally checks forward/backward focus, an arrow binding, and real
mouse movement to B before closing A. Super+Return logs no launcher; closing B
then drains the session. A separate compile-only negative variant exits WM on
that action; every app then proves display INFO still works before exiting.
That variant is never distributed as the normal WM ISO.

Graceful apps destroy surfaces and unmap/close their buffers before exit.
Unsolicited exit deliberately leaves resources live. Endpoint HUP removes WM
state, display mappings and grants; focus and layout are repaired. Failed sends
also remove/retile the affected client. Kernel teardown proves zero services,
connections, queued messages, retained grants and M32 objects, released input
and framebuffer claims, all five task stacks freed, and all processes destroyed.
PMM recovery is checked, not claimed to equal the initial global frame count:
the inherited shared kernel mapping machinery retains its own frames.

Host tests check 1,115,824 policy, geometry, bounds, generation, authority and
composition assertions, including every 1..90 by 1..90 screen up to six clients.
The serial parser is tested at every streaming prefix and rejects malformed
completed frames. Actual Ring3 probes check protocol version/opcode/size,
geometry injection, duplicate/stale/foreign identity, invalid surface binding,
foreign display delegation/manager authority and EVENT_WAIT pointer/count/
flags/kind/reserved/handle/input ownership. Real QMP keyboard/mouse input drives
the final proof. The independent oracle compares all 480,000 pixels of each
800x600 reference PPM, including cursor, borders, gaps, order and app contents.

## BoringFS and running

M35 retains 96 blocks of 4096 bytes. All eight historical `/bin` files remain
byte-identical to their builds, including the original M34 `boring-display`.
Five additions are `boring-display-wm`, `boringwm`, and `wm-client-a/b/c`.
The additive display executable is separate so the historical M34 service and
three-process acceptance remain untouched. The M35 builder uses the verified
M34 fixture, measures every ELF, fills free extents and validates all thirteen
files byte-for-byte. The final build's geometry record is authoritative.
Historical 64-block, M32 80-block and M33/M34 96-block fixtures are unchanged.

See [RUNNING-M35.md](RUNNING-M35.md). The bundle includes normal native WM and
preserved persistent-shell ISOs. The WM uses Limine module startup; it does not
claim M37 root-backed desktop supervision. Permanent CI retains every M0–M34
gate and adds host/ELF/oracle tests, both real WM QEMU modes, five framebuffer
references, M35 exact BoringFS contents and the verified human QEMU bundle.


## M35 Semantic Freeze record

- Base: `e7ac44c437624386cb4a5666cfe1e446a696c643`
- Freeze SHA: `154344d2bc4e136b7e53c473b35b42dcb7a41348`
- Freeze tree: `36504b61c61c46fb2c438e868ca4050b0527fa6e`
- Full exact-head CI: **Run #425 / 33039917519 / SUCCESS**
- Event: `pull_request`; branch: `agent/native-boringwm`; version: `0.0.35-dev`
- PR: [#46](https://github.com/dennishilk/boringos/pull/46)

All intended M35 semantics and all M0–M34 gates passed before this freeze.
The temporary workflow is absent. Closeout changes only active version
witnesses to **BoringKernel 0.0.36-dev** and documentation; no WM semantics.
Final-head CI, guarded squash, exact merged-main push CI and artifact hashes
are recorded in the PR's post-freeze verification trail. No M36 work is included.
