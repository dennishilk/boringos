#!/bin/sh
set -eu

SOURCE=build/boringos.iso
IMAGE=build/boringos-m59-cthulhu-smoke.img
META=build/m59-usb-image.txt

[ -f "$SOURCE" ] || {
    echo 'm59-build-usb-image: build/boringos.iso is missing; run the M59 smoke build first' >&2
    exit 1
}

cp "$SOURCE" "$IMAGE"
bytes=$(wc -c < "$IMAGE" | tr -d ' ')
sha=$(sha256sum "$IMAGE" | awk '{print $1}')
commit=${GITHUB_SHA:-}
if [ -z "$commit" ] && command -v git >/dev/null 2>&1; then
    commit=$(git rev-parse HEAD 2>/dev/null || true)
fi
[ -n "$commit" ] || commit=unknown

{
    printf 'filename=%s\n' "$(basename "$IMAGE")"
    printf 'bytes=%s\n' "$bytes"
    printf 'sha256=%s\n' "$sha"
    printf 'build_commit=%s\n' "$commit"
    printf 'kernel_version=%s\n' 'BoringKernel 0.0.59-dev'
    printf '%s\n' 'flashing=MANUAL USER-VERIFIED TARGET ONLY'
    printf '%s\n' 'warning=THE TARGET USB DEVICE WILL BE DESTROYED'
} > "$META"

[ "$bytes" -lt 12000000000 ] || {
    echo "m59-build-usb-image: image is unexpectedly large: $bytes bytes" >&2
    exit 1
}

cat "$META"
echo 'M59 USB IMAGE PACKAGING PASSED'
