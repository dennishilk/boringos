# BoringWM behavioral reference

## Purpose

The existing [`dennishilk/boringwm`](https://github.com/dennishilk/boringwm) repository is the original Rust/X11 BoringWM implementation. It remains a separate project and must not be rewritten, repurposed, copied, or vendored into BoringOS during this bootstrap phase.

For BoringOS, the original project is valuable as a **behavioral reference**, not as a source dependency. The future native BoringOS BoringWM is intended to be written in C and to speak a small BoringOS-native display/window protocol rather than X11.

This document records the behavior that is worth preserving and separates it from implementation details that exist specifically because the current BoringWM runs on X11.

## Reference inspected

Bootstrap review inspected the public `main` branch of `dennishilk/boringwm`, including its README and the implementation areas responsible for layout, state, key handling, configuration, and X11 window-management behavior.

The original project currently describes itself as a small keyboard-first X11 master/stack tiling window manager with explicit state, predictable ordering, and deliberately limited scope.

## Behavior to preserve in native BoringOS BoringWM

### 1. Deterministic master/stack layout

The central layout model is portable and should remain part of BoringWM's identity:

- zero clients produce no tiled geometry;
- one tiled client occupies the available work area subject to configured gaps and borders;
- with two or more tiled clients, the first client is the master;
- remaining clients form the stack;
- stack clients divide the available height deterministically;
- remainder pixels are assigned predictably rather than producing unstable geometry;
- the master ratio is bounded rather than allowed to create unusable layouts;
- gaps and borders are bounded so tiny screens still receive non-zero client dimensions.

The current implementation clamps the master ratio to the range `0.2..=0.8`. The exact numeric range can be reviewed later, but the principle of safe bounded geometry should be preserved.

### 2. Stable client ordering

Client order is explicit state. That should remain true in the native implementation.

The native BoringWM should preserve these semantics:

- newly managed clients enter a deterministic workspace order;
- the first tiled entry is the master;
- focus cycling does not implicitly reorder clients;
- explicit reorder actions swap a focused client with its neighbor;
- promotion explicitly moves the focused client into the master position;
- client removal selects a deterministic replacement focus rather than an arbitrary one.

This separation between **focus**, **order**, and **layout** is more important than the particular container types used by the Rust implementation.

### 3. Workspace model

The original implementation uses a fixed set of workspaces, nine by default, with per-workspace ordering and remembered focus.

Portable behavior worth recreating:

- each client belongs to exactly one workspace;
- each workspace maintains its own ordered client list;
- each workspace remembers its focused client where possible;
- switching workspaces restores remembered focus, with a deterministic fallback;
- moving the focused client to another workspace removes it from the old workspace exactly once and assigns it to the target workspace exactly once;
- an empty workspace has no focused client.

The native BoringOS implementation does not need to inherit EWMH desktop properties or X11 desktop-number conventions in order to preserve this behavior.

### 4. Focus behavior

The existing BoringWM is keyboard-first and predictable:

- `Mod+J` focuses the next client;
- `Mod+K` focuses the previous client;
- cycling wraps around the workspace order;
- a newly managed client on the active workspace becomes focused;
- removing the focused client chooses a deterministic nearby replacement;
- deliberate pointer-enter focus is supported by the X11 implementation;
- when no client is focused, focus returns to the root context rather than an arbitrary stale window.

For native BoringOS, the policy is portable but the mechanism will differ. A BoringOS input/display protocol should express focus changes directly instead of reproducing X11 focus calls.

### 5. Keyboard-first actions

The existing default action model is part of the project's identity and should be treated as a behavioral starting point:

| Existing binding | Behavior to preserve conceptually |
|---|---|
| `Mod+J` / `Mod+K` | focus next / previous |
| `Mod+Shift+J` / `Mod+Shift+K` | reorder focused client |
| `Mod+M` | promote focused client to master |
| `Mod+H` / `Mod+L` | decrease / increase master ratio |
| `Mod+1…9` | switch workspace |
| `Mod+Shift+1…9` | move focused client to workspace |
| `Mod+Q` | request focused client to close |
| `Mod+F` | toggle fullscreen |
| `Mod+Space` | toggle floating |
| `Mod+Shift+R` | restart window manager |
| `Mod+Shift+E` | exit window manager |

Application-launch bindings in the X11 version (`terminal`, file manager, browser, launcher) express a useful keyboard-first philosophy, but their concrete commands are host-environment configuration and should not be copied into early BoringOS.

### 6. Promotion

Promotion is intentionally simple: the focused client is swapped into position zero of the active workspace order. It does not require a special master object or hidden history.

That simple behavior is a good fit for BoringOS and should be recreated unless later usability testing demonstrates a strong reason to change it.

### 7. Small, strict configuration philosophy

The original BoringWM favors explicit configuration and rejects malformed or unknown values instead of guessing. Current configuration covers a small set of commands and visual/layout values, including gaps, border width, colors, master ratio, workspace count, and a modifier choice.

Portable principles to retain:

- configuration remains deliberately small;
- defaults are usable;
- missing configuration is normal;
- malformed configuration fails clearly;
- unknown fields are not silently accepted;
- numeric values are validated and bounded;
- external commands, where supported, are represented as argument vectors rather than shell command strings.

The current flat TOML subset and Unix/XDG paths are not requirements for BoringOS. A native format and location should be chosen only after the BoringOS filesystem and userspace conventions are defined.

## Behavior that is X11-specific and should not be copied

The following are implementation mechanisms of the Rust/X11 BoringWM rather than native BoringOS requirements:

- connecting through `x11rb` to an X server;
- owning `SubstructureRedirect` on the X root window;
- X11 root windows and X11 window IDs;
- EWMH atoms and root properties;
- `_NET_ACTIVE_WINDOW`, `_NET_CLIENT_LIST`, `_NET_WM_STATE`, `_NET_WM_DESKTOP`, and related EWMH desktop metadata;
- `WM_DELETE_WINDOW` and `WM_TAKE_FOCUS` protocol messages;
- X11 `ConfigureRequest`, `MapRequest`, `UnmapNotify`, and `DestroyNotify` mechanics;
- `override_redirect` handling;
- X11 key grabs and current US X keycodes;
- X11-specific startup adoption and check-window behavior;
- X11/EWMH fullscreen signaling.

Native BoringOS should define the smallest set of protocol messages necessary to express equivalent user-visible behavior directly.

## Portable concepts that need a new native mechanism

Some features have portable intent even though their current implementation is X11-specific:

- **manage / unmanage:** native BoringOS needs explicit client create/map/destroy lifecycle events;
- **close request:** native clients need a clean close-request event, with process termination policy kept separate;
- **focus:** the display/window protocol needs explicit focus ownership and keyboard delivery rules;
- **fullscreen:** the native protocol can model fullscreen as window state without EWMH atoms;
- **floating dialogs:** the protocol may eventually expose a role or hint, but early BoringOS should avoid importing the full X11 window-type model;
- **startup adoption/restart:** this should only be added once the native display service has a clear ownership and reconnection model.

## Integration options for BoringOS

### Option 1 — keep the Rust/X11 repository completely external

BoringOS documentation links to `dennishilk/boringwm` and records the expected behavior locally.

**Advantages**

- keeps BoringOS free of Rust/X11 build dependencies;
- makes it impossible to confuse the reference implementation with native BoringOS code;
- avoids submodule friction for contributors and tooling;
- preserves the independent release history of the original BoringWM;
- keeps the BoringOS bootstrap repository small.

**Disadvantage**

- the external repository can evolve, so behavioral references should identify what was reviewed.

### Option 2 — Git submodule such as `reference/boringwm-rust/`

A submodule would pin a particular BoringWM commit inside the BoringOS tree without copying its source.

**Advantages**

- exact reference revision is obvious;
- convenient for side-by-side automated comparison later.

**Disadvantages**

- makes every clone and recursive checkout more complex;
- visually places Rust/X11 source inside the BoringOS tree even though it is not part of the OS;
- creates unnecessary coupling before native BoringWM work begins;
- may mislead readers into treating the submodule as a build dependency.

### Option 3 — external repository plus a pinned behavioral reference

Keep the repository external, but document the reviewed upstream commit/revision whenever a native BoringWM conformance effort begins. Native tests can later encode the desired behavior directly in BoringOS rather than compiling the Rust/X11 project as part of BoringOS.

This provides most of the reproducibility benefit of a submodule without adding a foreign source tree to the project.

## Recommendation

**Use Option 3: keep `dennishilk/boringwm` external and treat it as a versioned behavioral reference. Do not add a submodule during the repository bootstrap.**

Reasons:

1. BoringOS currently has no native window protocol or native BoringWM implementation to compare against.
2. The Rust/X11 implementation is explicitly not a runtime or build dependency.
3. The important portable behavior is small enough to specify directly and test later.
4. Avoiding a submodule keeps the language and architecture boundaries obvious.
5. When native BoringWM development starts, this document can record a specific known-good BoringWM commit and translate the relevant behaviors into native C unit/integration tests.

A submodule can be reconsidered later only if automated cross-implementation conformance testing creates a concrete need for it.

## Bootstrap decision

For the initial BoringOS repository:

- original Rust/X11 BoringWM remains unchanged and external;
- no source is copied or vendored;
- no Git submodule is added;
- no native C BoringWM is implemented yet;
- this document is the local behavioral-reference boundary.
