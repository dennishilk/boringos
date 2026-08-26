#!/bin/sh
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
MODE=headless
case "${1:-}" in
    ""|--headless) ;;
    --gui) MODE=gui ;;
    *) echo "usage: $0 [--gui|--headless]" >&2; exit 2 ;;
esac

set -- qemu-system-x86_64 \
    -M q35 \
    -cpu qemu64,apic=off \
    -m 128M \
    -cdrom "${HERE}/boringos.iso" \
    -boot d \
    -drive "file=${HERE}/boringos-root.img,if=none,format=raw,id=boringdisk" \
    -device "virtio-blk-pci,drive=boringdisk,disable-legacy=on" \
    -vga std

if [ "${MODE}" = headless ]; then
    set -- "$@" -display none
fi

set -- "$@" \
    -serial stdio \
    -monitor none \
    -no-reboot \
    -no-shutdown
exec "$@"
