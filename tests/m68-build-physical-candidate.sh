#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

BASE_CONFIG=limine-m61-usb.conf
HIGHER_CONFIG=limine-m68-safe-higher-gop.conf
BACKUP=build/.m68-limine-m61-usb.conf.backup
PROOF_DIR=build/m68-safe-higher-gop
OLD_IMAGE=build/boringos-m61-usb.img
NEW_IMAGE=build/boringos-m68-safe-higher-gop.img
OLD_META=build/m61-usb-image.txt
NEW_META=build/m68-safe-higher-gop-image.txt

mkdir -p build "$PROOF_DIR"
[ -f "$BASE_CONFIG" ] || { echo "missing $BASE_CONFIG" >&2; exit 1; }
[ -f "$HIGHER_CONFIG" ] || { echo "missing $HIGHER_CONFIG" >&2; exit 1; }

grep -Fqx '    resolution: 1920x1080x32' "$HIGHER_CONFIG"
grep -Fqx '    resolution: 800x600x32' "$HIGHER_CONFIG"
if grep -Fq '3440x1440' "$HIGHER_CONFIG"; then
    echo 'refusing forbidden 3440x1440 M68 candidate' >&2
    exit 1
fi

baseline_sha=$(sha256sum "$BASE_CONFIG" | awk '{print $1}')
rm -f "$BACKUP"
cp "$BASE_CONFIG" "$BACKUP"

restore_config() {
    if [ -f "$BACKUP" ]; then
        cp "$BACKUP" "$BASE_CONFIG"
        rm -f "$BACKUP"
    fi
}
trap restore_config EXIT INT TERM

# The preceding M61 handoff bisector proves the diagnostic build. Rebuild the
# same accepted runtime without successful-boot graphics or the generated
# scanout-witness wrapper before constructing the single physical candidate.
M61_EXTRA_TEST_CPPFLAGS='-DBORING_M68_PHYSICAL_RELEASE=1' \
    sh tests/m61-build.sh
nm build/kernel.elf | grep -Fq 'boring_m68_physical_release_ui_enabled'
if nm build/kernel.elf | grep -Fq '__wrap_boring_boot_console_desktop_handoff'; then
    echo 'M68 candidate retained the diagnostic scanout-witness wrapper' >&2
    exit 1
fi

# Reuse the accepted M61 image constructor without changing its source contract:
# substitute only the staged Limine config inside this process, then restore it.
cp "$HIGHER_CONFIG" "$BASE_CONFIG"
sh tests/m61-build-usb-image.sh

# Preserve the exact embedded candidate config/proof before restoring the source.
cp build/m61-image/image-proof/limine.conf "$PROOF_DIR/embedded-limine.conf"
cmp -s "$HIGHER_CONFIG" "$PROOF_DIR/embedded-limine.conf"
if [ -f build/m61-qemu-twin-proof.txt ]; then
    cp build/m61-qemu-twin-proof.txt "$PROOF_DIR/qemu-twin-proof.txt"
fi

restore_config
trap - EXIT INT TERM
restored_sha=$(sha256sum "$BASE_CONFIG" | awk '{print $1}')
[ "$restored_sha" = "$baseline_sha" ] || {
    echo 'M68 candidate builder failed to restore accepted M61 config' >&2
    exit 1
}

[ -f "$OLD_IMAGE" ] || { echo 'M68 candidate raw missing after image build' >&2; exit 1; }
[ -f "$OLD_IMAGE.xz" ] || { echo 'M68 candidate xz missing after image build' >&2; exit 1; }
[ -f "$OLD_META" ] || { echo 'M68 candidate metadata missing after image build' >&2; exit 1; }

rm -f "$NEW_IMAGE" "$NEW_IMAGE.xz" "$NEW_IMAGE.sha256" "$NEW_META"
mv "$OLD_IMAGE" "$NEW_IMAGE"
mv "$OLD_IMAGE.xz" "$NEW_IMAGE.xz"
sed 's/boringos-m61-usb\.img/boringos-m68-safe-higher-gop.img/g' \
    "$OLD_META" > "$NEW_META"
rm -f "$OLD_META" build/boringos-m61-usb.img.sha256

# The generated timeout-zero image is a non-publishable test twin, not a second
# physical candidate. Keep only its textual equivalence proof.
rm -f build/boringos-m61-usb-qemu.img build/boringos-m61-usb-qemu.img.sha256 \
      build/m61-qemu-twin-proof.txt

raw_size=$(wc -c < "$NEW_IMAGE" | tr -d ' ')
raw_sha=$(sha256sum "$NEW_IMAGE" | awk '{print $1}')
xz_size=$(wc -c < "$NEW_IMAGE.xz" | tr -d ' ')
xz_sha=$(sha256sum "$NEW_IMAGE.xz" | awk '{print $1}')
printf '%s  %s\n' "$raw_sha" "$(basename "$NEW_IMAGE")" > "$NEW_IMAGE.sha256"

{
    printf 'task=M68 safe higher GOP\n'
    printf 'target_request=1920x1080x32\n'
    printf 'request_semantics=preferred Limine firmware framebuffer request\n'
    printf 'known_safe_explicit_fallback=800x600x32\n'
    printf 'limine_unavailable_request_fallback=preserved\n'
    printf 'accepted_m61_config_preserved=YES\n'
    printf 'successful_boot_dashboard_visible=NO\n'
    printf 'graphical_boot_console_diagnostic=disabled\n'
    printf 'magenta_scanout_witness_wrapper=absent\n'
    printf 'physical_support_proven=NO_PENDING_CTHULHU\n'
    printf 'raw_filename=%s\n' "$(basename "$NEW_IMAGE")"
    printf 'raw_size=%s\n' "$raw_size"
    printf 'raw_sha256=%s\n' "$raw_sha"
    printf 'xz_filename=%s\n' "$(basename "$NEW_IMAGE.xz")"
    printf 'xz_size=%s\n' "$xz_size"
    printf 'xz_sha256=%s\n' "$xz_sha"
    printf '3440x1440_attempted=NO\n'
} > "$PROOF_DIR/candidate-proof.txt"

{
    printf 'm68_target_request=1920x1080x32\n'
    printf 'm68_known_safe_fallback=800x600x32\n'
    printf 'm68_physical_support_proven=NO_PENDING_CTHULHU\n'
    printf 'm68_raw_sha256=%s\n' "$raw_sha"
    printf 'm68_xz_sha256=%s\n' "$xz_sha"
    printf 'm68_3440x1440_attempted=NO\n'
} >> "$NEW_META"

cat "$PROOF_DIR/candidate-proof.txt"
printf '%s\n' 'M68 SAFE HIGHER GOP PHYSICAL CANDIDATE BUILT; PHYSICAL SUCCESS NOT CLAIMED'
