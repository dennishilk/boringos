#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "${ROOT}"
sh tests/m39-build.sh
printf '%s\n' 'Super+Return: terminal; type boring-edit /note.txt; Ctrl+S: save; Super+Q: close.'
exec qemu-system-x86_64 -M q35 -cpu qemu64,apic=off -m 128M \
    -cdrom build/boringos.iso -boot d -vga std \
    -drive file=build/m39-bundle/boringos-root.img,if=none,format=raw,id=boringdisk \
    -device virtio-blk-pci,drive=boringdisk,disable-legacy=on \
    -serial file:build/m39-interactive-serial.log -no-reboot -no-shutdown
