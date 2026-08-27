# Run the M36 graphical terminal in QEMU

Requirements: QEMU x86_64 with a graphical backend. Extract the complete bundle,
run `sha256sum -c SHA256SUMS`, then `./run-boringos.sh --gui`.

The ISO starts the native display service and BoringWM as scheduler tasks in
Ring 3. It mounts the attached, read-only BoringFS image. No PID 1 session
supervision or M37 integration is included. The separate historical M35 bundle
and its writable serial-shell session are preserved.

| Binding | Action |
| --- | --- |
| Super + Return | Spawn `/bin/boring-terminal` from BoringFS |
| Super + J/L/Down/Right | Focus the next window |
| Super + H/K/Left/Up | Focus the previous window |
| Super + Q | Close the focused terminal and its PTY shell |
| Normal keyboard input | Send bytes to the focused terminal's PTY |

In the terminal, type `boringfetch` and Enter. The real shell spawns the real
program and displays its stdout in the graphical window. Open a second
terminal to use a separate shell and PTY. Each terminal owns a different
process address space, M32 pixel buffer, display surface and WM token.

The minimum session budget is six user processes: display, WM, two terminals
and two shells. One foreground command raises this to seven. The existing
eight-process/eight-task limits are unchanged. PTYs remain bounded to eight
pairs, each with two 4096-byte rings. This milestone proves two terminals;
additional windows are subject to these limits.

The root image contains only the measured executable contents needed by this
session: `boring-terminal`, `boring-shell` and `boringfetch`. See
`M36-BORINGFS-GEOMETRY.txt` for the measured block count and rejected lower
bound. Root mutations are intentionally denied. `exit` closes the shell;
terminal HUP cleanup removes its window. Closing every window drains the
session and leaves a kernel acceptance message; restart QEMU for a new session.

Your host desktop may intercept Super shortcuts. Capture keyboard input in
the QEMU window first. `--headless` only provides serial diagnostics and hides
the graphical session. This production bundle never contains the F12 death
test executable. Stop QEMU by closing its window or terminating it.
