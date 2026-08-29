# M54 — USB-only graphical desktop

M54 is the bounded integration proof that the existing M48–M53 xHCI/USB-HID path can be the sole input source for the established native BoringOS graphical desktop session.

## Required path

The focused acceptance uses `q35,i8042=off` with `qemu-xhci`, `usb-kbd`, and `usb-tablet` and preserves the existing path:

QEMU USB HID → xHCI → real Interrupt-IN Transfer Event → M52 HID decoder → M53 canonical input producers → existing kernel input queue → existing `INPUT_READ` / `EVENT_WAIT` semantics → `boring-display` / BoringWM.

There is no PS/2 fallback, parallel synthetic input backend, second queue, second HID state machine, or new input ABI in the M54 focused run.

## Acceptance boundary

The focused M54 gate must preserve the established PID 1 desktop-session path and prove, with real USB input and existing userspace behavior:

- native desktop startup through the existing persistent BoringFS/VFS session bundle;
- `/bin/boring-display` and `/bin/boringwm` startup;
- a USB keyboard Super+Return shortcut spawning the existing graphical BoringTerminal;
- preserved modifier ordering;
- real USB-tablet cursor movement and left-button down/up reaching the desktop;
- existing focus/window behavior;
- semantic framebuffer evidence showing the desktop and launched client;
- the existing clean session drain, including input ownership, processes, shared memory, IPC, PTYs, and PID 1 supervision.

M54 may add only the minimum runtime wiring required to keep the already-bounded M48–M53 HID report path serviced while the established Ring 3 desktop waits for input.

## Non-goals

M54 does not add a new HID decoder, input ABI, queue type, USB hub/storage support, AHCI, NVMe, networking, audio, login management, or a new desktop application. M55 storage work is explicitly out of scope until M54 is fully merged-main green.
