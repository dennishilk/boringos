#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
make -C "${ROOT}" wm-host-test wm-audit
python3 "${ROOT}/tests/wm-qemu.py" m35-wm
python3 "${ROOT}/tests/wm-qemu.py" m35-wm-death
