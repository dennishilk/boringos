#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
mkdir -p build

HOST_CC=${HOST_CC:-cc}
HOST_CFLAGS='-std=c11 -fno-builtin -fno-tree-loop-distribute-patterns -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes'

"$HOST_CC" \
    -Iuser/runtime/include -Ikernel/include \
    $HOST_CFLAGS \
    tests/m68-safe-higher-gop-host.c \
    kernel/core/framebuffer.c \
    user/boring-display/core.c \
    user/boringwm/core.c \
    -o build/m68-safe-higher-gop-host-test

build/m68-safe-higher-gop-host-test

# Existing production regressions that exercise the same sizing, mapping,
# region-present, cursor-damage, focus-border, and WM boundaries.
make \
    framebuffer-host-test \
    vmm-framebuffer-host-test \
    display-host-test \
    wm-host-test \
    framebuffer-present-region-host-test \
    mouse-latency-host-test \
    framebuffer-present-region-host-test-sanitized \
    mouse-latency-host-test-sanitized

# Keep the accepted physical M61 config byte-for-byte separate from M68.
grep -Fqx '    resolution: 800x600x32' limine-m61-usb.conf
grep -Fqx '    resolution: 1920x1080x32' limine-m68-safe-higher-gop.conf
grep -Fqx '    resolution: 800x600x32' limine-m68-safe-higher-gop.conf
if grep -Fq '3440x1440' limine-m68-safe-higher-gop.conf; then
    echo 'M68 safe higher GOP: forbidden native ultrawide request present' >&2
    exit 1
fi

printf '%s\n' 'M68 SAFE HIGHER GOP FOCUSED REGRESSIONS: PASS'
