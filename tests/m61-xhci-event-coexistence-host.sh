#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
mkdir -p build
cc -Ikernel/include \
   -std=c11 -fno-builtin -fno-tree-loop-distribute-patterns \
   -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow \
   -Wstrict-prototypes -Wmissing-prototypes \
   tests/m61-xhci-event-coexistence-host.c kernel/core/usb_hid.c \
   -o build/m61-xhci-event-coexistence-host
build/m61-xhci-event-coexistence-host
