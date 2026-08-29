#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
sh tests/m61-build.sh
sh tests/m61-build-usb-image.sh
