#!/bin/sh
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec qemu-system-x86_64 \
    -M q35 \
    -cpu qemu64,apic=off \
    -m 128M \
    -cdrom "${HERE}/boringos.iso" \
    -boot d \
    -drive "file=${HERE}/boringos-root.img,if=none,format=raw,id=boringdisk" \
    -device "virtio-blk-pci,drive=boringdisk,disable-legacy=on" \
    -display none \
    -serial stdio \
    -monitor none \
    -no-reboot \
    -no-shutdown
