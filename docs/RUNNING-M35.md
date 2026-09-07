# Run native BoringWM in QEMU

Requirements: QEMU x86_64 with a graphical display backend. Verify the extracted
bundle with `sha256sum -c SHA256SUMS`, then run `./run-boringos.sh --gui`.

The default `boringos.iso` starts five real CPL3 processes from Limine modules:
the display service, native C BoringWM, and clients A, B, C. This is the M35
desktop session; PID 1 desktop supervision and root-backed session startup
remain outside this milestone. No terminal exists. The GUI is live and accepts
real emulated PS/2 input. `--headless` keeps serial diagnostics but hides it.

| Binding | Action |
| --- | --- |
| Super + H/K/Left/Up | Previous focus, wrapping |
| Super + J/L/Down/Right | Next focus, wrapping |
| Super + Shift + the same keys | Swap with adjacent tile; no wrap |
| Super + Q | Request focused app's graceful close |
| Super + Return | Log terminal unavailable; launch nothing |
| Plain X | Acceptance client exits with live resources to test cleanup |
| Real mouse movement | Focus the tile beneath the cursor |

Your host desktop may intercept Super combinations; ensure QEMU has captured
keyboard input. New clients take focus, so C is initially focused. Closing all
clients drains the session and leaves a kernel acceptance message. Restart
QEMU to start a new session. Stop QEMU by closing its window or terminating it.

## Preserved persistent serial shell

`./run-boringos-shell.sh --headless` boots `boringos-shell.iso`, attaches the
writable 96-block `boringos-root.img`, and starts `boring-init` plus the native
serial shell. `--gui` additionally shows the historical kernel dashboard;
it does not start the WM. See RUNNING-SHELL.md for the unchanged shell commands.

The root contains all 13 verified `/bin` programs. Historical `/bin/boring-display`
is unchanged. `/bin/boring-display-wm` is the additive M35 service executable;
both serve the same `boring.display` protocol v1, and only the latter provides
the explicit v2 management extension. Do not start both services in one session.
The root is not needed by the module-based WM ISO. No M36 terminal or M37 init
integration is included.
