# Run BoringOS in QEMU

This bundle boots BoringKernel, mounts the included writable BoringFS image as
the native root filesystem, starts `boring-init`, and hands control to the
native PID 2 `boring-shell`.

The interactive prompt is built from kernel-owned single-user identity and
the process's real current working directory:

```text
boring@boringos:/$
boring@boringos:/test$
```

The identity is real BoringOS process/system metadata, but it is not a claim
of authentication or Unix permissions; neither exists yet.

Requirements: `qemu-system-x86_64` on an x86_64 Linux host.

Run from the extracted bundle. The default remains headless and keeps the
serial console as the interactive interface:

```sh
chmod +x run-boringos.sh
./run-boringos.sh --headless
```

To see the native kernel-rendered BoringOS framebuffer dashboard while keeping
the same serial shell in the terminal, use:

```sh
./run-boringos.sh --gui
```

The graphical dashboard remains informational. Milestone 31 adds native PS/2
keyboard and mouse events, but the shell's stdin/stdout remain on the serial
console. From the shell, `/bin/input-test` claims the native input stream and
blocks until real keyboard/mouse events arrive; its `--teardown` mode is used by
the lifecycle acceptance. There is still no cursor, graphical shell, display
server or window system.

The root image is attached through modern VirtIO PCI. Writes are persistent;
do not add QEMU snapshot options. Try:

```text
help
ls
mkdir test
cd test
touch hello.txt
write hello.txt Hallo-von-BoringOS
cat hello.txt
boringfetch
ps
history
```

`write` stores one trailing newline by default, so `cat` leaves the next
prompt on a clean line. Use `write -n <path> <text>` only when exact
no-newline content is intended. Left/Right/Home/End, Backspace/Delete,
Up/Down history, command completion (`boringf<TAB>`) and real directory-entry
completion (`cd te<TAB>`, `cat hel<TAB>`) are supported within bounded shell
buffers.

`exit` and `logout` both terminate the current shell session. PID 1 observes
and reaps that exact child, releases its resources and starts a fresh shell.
`logout` does not imply an authentication logout because there is no login
manager.

Stop QEMU, start the same script again, and `cat /test/hello.txt` to verify
persistence. BoringFS v0 uses synchronous metadata updates but has no journal
and does not yet promise crash consistency.
