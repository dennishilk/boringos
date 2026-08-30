#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

LAYOUT=kernel/include/boring/m61_usb_layout.h
IMAGE=build/boringos-m61-usb.img
COMPRESSED=$IMAGE.xz
SHA_FILE=$IMAGE.sha256
META=build/m61-usb-image.txt
QEMU_IMAGE=build/boringos-m61-usb-qemu.img
QEMU_SHA_FILE=$QEMU_IMAGE.sha256
TWIN_PROOF=build/m61-qemu-twin-proof.txt
WORK=build/m61-image
ESP=$WORK/esp.fat
ROOTFS=build/m61-bundle/boringos-root.img

macro() {
    awk -v name="$1" '$1 == "#define" && $2 == name { value=$3; gsub(/[ULul]/, "", value); print value; exit }' "$LAYOUT"
}

hash_blocks() {
    dd if="$1" bs="$SECTOR" skip="$2" count="$3" status=none | sha256sum | awk '{print $1}'
}

SECTOR=$(macro M61_USB_SECTOR_SIZE)
IMAGE_SECTORS=$(macro M61_USB_IMAGE_SECTORS)
ESP_FIRST=$(macro M61_USB_ESP_FIRST_LBA)
ESP_SECTORS=$(macro M61_USB_ESP_SECTORS)
ROOT_FIRST=$(macro M61_USB_ROOT_FIRST_LBA)
ROOT_SECTORS=$(macro M61_USB_ROOT_SECTORS)

for value in "$SECTOR" "$IMAGE_SECTORS" "$ESP_FIRST" "$ESP_SECTORS" "$ROOT_FIRST" "$ROOT_SECTORS"; do
    case "$value" in ''|*[!0-9]*) echo 'M61 image layout macro parse failed' >&2; exit 1;; esac
done
[ "$SECTOR" -eq 512 ] || { echo 'M61 requires 512-byte USB sectors' >&2; exit 1; }
ESP_LAST=$((ESP_FIRST + ESP_SECTORS - 1))
ROOT_LAST=$((ROOT_FIRST + ROOT_SECTORS - 1))
[ "$ESP_LAST" -lt "$ROOT_FIRST" ] || { echo 'M61 ESP/root overlap' >&2; exit 1; }
[ "$ROOT_LAST" -lt "$IMAGE_SECTORS" ] || { echo 'M61 root exceeds image' >&2; exit 1; }
IMAGE_BYTES=$((IMAGE_SECTORS * SECTOR))
ROOT_BYTES=$((ROOT_SECTORS * SECTOR))
ESP_BYTES=$((ESP_SECTORS * SECTOR))
[ "$IMAGE_BYTES" -lt 15682240512 ] || { echo 'M61 image exceeds physical target' >&2; exit 1; }

for tool in sgdisk mkfs.fat mmd mcopy xz cmp nm sed; do
    command -v "$tool" >/dev/null 2>&1 || { echo "M61 image builder missing tool: $tool" >&2; exit 1; }
done
for file in build/kernel.elf build/user/boring-init-desktop.elf \
            build/deps/limine-binary/BOOTX64.EFI limine-m61-usb.conf "$ROOTFS"; do
    [ -f "$file" ] || { echo "M61 image builder missing: $file" >&2; exit 1; }
done
root_bytes=$(wc -c < "$ROOTFS" | tr -d ' ')
[ "$root_bytes" -le "$ROOT_BYTES" ] || { echo 'M61 BoringFS image exceeds root slice' >&2; exit 1; }
[ $((root_bytes % SECTOR)) -eq 0 ] || { echo 'M61 BoringFS image is not sector aligned' >&2; exit 1; }
ROOT_PAYLOAD_SECTORS=$((root_bytes / SECTOR))

rm -rf "$WORK"
mkdir -p "$WORK/stage/EFI/BOOT" "$WORK/stage/boot/limine" "$WORK/stage/boot/user"
cp build/deps/limine-binary/BOOTX64.EFI "$WORK/stage/EFI/BOOT/BOOTX64.EFI"
cp build/kernel.elf "$WORK/stage/boot/kernel.elf"
cp build/user/boring-init-desktop.elf "$WORK/stage/boot/user/boring-init.elf"
cp limine-m61-usb.conf "$WORK/stage/boot/limine/limine.conf"
find "$WORK/stage" -exec touch -t 198001010000 {} +

truncate -s "$ESP_BYTES" "$ESP"
mkfs.fat -F 32 -n BORINGEFI -i 424F5249 "$ESP" >/dev/null
mmd -i "$ESP" ::/EFI ::/EFI/BOOT ::/boot ::/boot/limine ::/boot/user
mcopy -i "$ESP" -m "$WORK/stage/EFI/BOOT/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI
mcopy -i "$ESP" -m "$WORK/stage/boot/kernel.elf" ::/boot/kernel.elf
mcopy -i "$ESP" -m "$WORK/stage/boot/user/boring-init.elf" ::/boot/user/boring-init.elf
mcopy -i "$ESP" -m "$WORK/stage/boot/limine/limine.conf" ::/boot/limine/limine.conf

rm -f "$IMAGE" "$COMPRESSED" "$SHA_FILE" "$META" \
      "$QEMU_IMAGE" "$QEMU_SHA_FILE" "$TWIN_PROOF"
truncate -s "$IMAGE_BYTES" "$IMAGE"
sgdisk --clear \
    --disk-guid=424F5249-4E47-4D36-3100-000000000061 \
    --new=1:"$ESP_FIRST":"$ESP_LAST" --typecode=1:EF00 --change-name=1:BORINGEFI \
    --partition-guid=1:424F5249-4E47-4D36-3101-000000000061 \
    --new=2:"$ROOT_FIRST":"$ROOT_LAST" --typecode=2:8300 --change-name=2:BORINGROOT \
    --partition-guid=2:424F5249-4E47-4D36-3102-000000000061 \
    "$IMAGE" >/dev/null
sgdisk --verify "$IMAGE" | tee "$WORK/gpt-verify.txt"
dd if="$ESP" of="$IMAGE" bs="$SECTOR" seek="$ESP_FIRST" conv=notrunc status=none
dd if="$ROOTFS" of="$IMAGE" bs="$SECTOR" seek="$ROOT_FIRST" conv=notrunc status=none

PHYSICAL_PROOF=$WORK/image-proof
mkdir -p "$PHYSICAL_PROOF"
IMAGE_ESP="${IMAGE}@@$((ESP_FIRST * SECTOR))"
mcopy -i "$IMAGE_ESP" ::/boot/kernel.elf "$PHYSICAL_PROOF/kernel.elf"
mcopy -i "$IMAGE_ESP" ::/boot/user/boring-init.elf "$PHYSICAL_PROOF/boring-init.elf"
mcopy -i "$IMAGE_ESP" ::/boot/limine/limine.conf "$PHYSICAL_PROOF/limine.conf"
cmp -s build/kernel.elf "$PHYSICAL_PROOF/kernel.elf" || {
    echo 'M61 image proof FAILED: embedded kernel differs from candidate kernel' >&2
    exit 1
}
cmp -s build/user/boring-init-desktop.elf "$PHYSICAL_PROOF/boring-init.elf" || {
    echo 'M61 image proof FAILED: embedded boring-init differs from candidate init' >&2
    exit 1
}
cmp -s limine-m61-usb.conf "$PHYSICAL_PROOF/limine.conf" || {
    echo 'M61 image proof FAILED: embedded Limine config differs from candidate config' >&2
    exit 1
}
nm "$PHYSICAL_PROOF/kernel.elf" | grep -Fq 'boring_m61_physical_breadcrumbs_enabled'
grep -Fqx 'timeout: 5' "$PHYSICAL_PROOF/limine.conf"
grep -Fqx 'mouse: no' "$PHYSICAL_PROOF/limine.conf"
printf '%s\n' 'M61 raw image proof: diagnostic kernel + timeout: 5 + bounded Limine autoboot: PASS'

# QEMU test twin: start from the already-proven physical raw image and change
# exactly the Limine timeout behaviour. It is never compressed or published.
QEMU_CONF=$WORK/limine-qemu.conf
awk '
    /^timeout: 5$/ { print "timeout: 0"; changed++; next }
    { print }
    END { if (changed != 1) exit 42 }
' limine-m61-usb.conf > "$QEMU_CONF"
cp "$IMAGE" "$QEMU_IMAGE"
QEMU_IMAGE_ESP="${QEMU_IMAGE}@@$((ESP_FIRST * SECTOR))"
mcopy -o -i "$QEMU_IMAGE_ESP" "$QEMU_CONF" ::/boot/limine/limine.conf
sgdisk --verify "$QEMU_IMAGE" > "$WORK/qemu-twin-gpt-verify.txt"

QEMU_PROOF=$WORK/qemu-twin-proof
mkdir -p "$QEMU_PROOF"
mcopy -i "$QEMU_IMAGE_ESP" ::/boot/kernel.elf "$QEMU_PROOF/kernel.elf"
mcopy -i "$QEMU_IMAGE_ESP" ::/boot/user/boring-init.elf "$QEMU_PROOF/boring-init.elf"
mcopy -i "$QEMU_IMAGE_ESP" ::/boot/limine/limine.conf "$QEMU_PROOF/limine.conf"
cmp -s "$PHYSICAL_PROOF/kernel.elf" "$QEMU_PROOF/kernel.elf" || {
    echo 'M61 QEMU twin proof FAILED: kernel differs from physical image' >&2
    exit 1
}
cmp -s "$PHYSICAL_PROOF/boring-init.elf" "$QEMU_PROOF/boring-init.elf" || {
    echo 'M61 QEMU twin proof FAILED: boring-init differs from physical image' >&2
    exit 1
}
cmp -s "$QEMU_CONF" "$QEMU_PROOF/limine.conf" || {
    echo 'M61 QEMU twin proof FAILED: embedded twin config differs from generated config' >&2
    exit 1
}
grep -Fqx 'timeout: 0' "$QEMU_PROOF/limine.conf"
grep -Fqx 'mouse: no' "$QEMU_PROOF/limine.conf"
sed 's/^timeout: [0-9][0-9]*$/timeout: TEST-TWIN/' "$PHYSICAL_PROOF/limine.conf" > "$WORK/physical-normalized.conf"
sed 's/^timeout: [0-9][0-9]*$/timeout: TEST-TWIN/' "$QEMU_PROOF/limine.conf" > "$WORK/qemu-normalized.conf"
cmp -s "$WORK/physical-normalized.conf" "$WORK/qemu-normalized.conf" || {
    echo 'M61 QEMU twin proof FAILED: Limine config differs beyond timeout value' >&2
    exit 1
}

physical_kernel_sha=$(sha256sum "$PHYSICAL_PROOF/kernel.elf" | awk '{print $1}')
qemu_kernel_sha=$(sha256sum "$QEMU_PROOF/kernel.elf" | awk '{print $1}')
physical_init_sha=$(sha256sum "$PHYSICAL_PROOF/boring-init.elf" | awk '{print $1}')
qemu_init_sha=$(sha256sum "$QEMU_PROOF/boring-init.elf" | awk '{print $1}')
root_source_sha=$(sha256sum "$ROOTFS" | awk '{print $1}')
physical_root_payload_sha=$(hash_blocks "$IMAGE" "$ROOT_FIRST" "$ROOT_PAYLOAD_SECTORS")
qemu_root_payload_sha=$(hash_blocks "$QEMU_IMAGE" "$ROOT_FIRST" "$ROOT_PAYLOAD_SECTORS")
physical_root_slice_sha=$(hash_blocks "$IMAGE" "$ROOT_FIRST" "$ROOT_SECTORS")
qemu_root_slice_sha=$(hash_blocks "$QEMU_IMAGE" "$ROOT_FIRST" "$ROOT_SECTORS")
physical_gpt_head_sha=$(hash_blocks "$IMAGE" 0 34)
qemu_gpt_head_sha=$(hash_blocks "$QEMU_IMAGE" 0 34)
GPT_TAIL_FIRST=$((IMAGE_SECTORS - 33))
physical_gpt_tail_sha=$(hash_blocks "$IMAGE" "$GPT_TAIL_FIRST" 33)
qemu_gpt_tail_sha=$(hash_blocks "$QEMU_IMAGE" "$GPT_TAIL_FIRST" 33)

[ "$physical_kernel_sha" = "$qemu_kernel_sha" ]
[ "$physical_init_sha" = "$qemu_init_sha" ]
[ "$root_source_sha" = "$physical_root_payload_sha" ]
[ "$root_source_sha" = "$qemu_root_payload_sha" ]
[ "$physical_root_slice_sha" = "$qemu_root_slice_sha" ]
[ "$physical_gpt_head_sha" = "$qemu_gpt_head_sha" ]
[ "$physical_gpt_tail_sha" = "$qemu_gpt_tail_sha" ]
qemu_bytes=$(wc -c < "$QEMU_IMAGE" | tr -d ' ')
[ "$qemu_bytes" -eq "$IMAGE_BYTES" ]
qemu_sha=$(sha256sum "$QEMU_IMAGE" | awk '{print $1}')
printf '%s  %s\n' "$qemu_sha" "$(basename "$QEMU_IMAGE")" > "$QEMU_SHA_FILE"
{
    printf 'physical_timeout_seconds=5\n'
    printf 'qemu_twin_timeout_seconds=0\n'
    printf 'sole_logical_difference=Limine timeout value 5 -> 0\n'
    printf 'physical_bytes=%s\n' "$IMAGE_BYTES"
    printf 'qemu_twin_bytes=%s\n' "$qemu_bytes"
    printf 'sector_size=%s\n' "$SECTOR"
    printf 'image_sectors=%s\n' "$IMAGE_SECTORS"
    printf 'esp_first_lba=%s\n' "$ESP_FIRST"
    printf 'esp_sectors=%s\n' "$ESP_SECTORS"
    printf 'root_first_lba=%s\n' "$ROOT_FIRST"
    printf 'root_sectors=%s\n' "$ROOT_SECTORS"
    printf 'physical_kernel_sha256=%s\n' "$physical_kernel_sha"
    printf 'qemu_twin_kernel_sha256=%s\n' "$qemu_kernel_sha"
    printf 'physical_boring_init_sha256=%s\n' "$physical_init_sha"
    printf 'qemu_twin_boring_init_sha256=%s\n' "$qemu_init_sha"
    printf 'source_boringroot_payload_sha256=%s\n' "$root_source_sha"
    printf 'physical_boringroot_payload_sha256=%s\n' "$physical_root_payload_sha"
    printf 'qemu_twin_boringroot_payload_sha256=%s\n' "$qemu_root_payload_sha"
    printf 'physical_boringroot_slice_sha256=%s\n' "$physical_root_slice_sha"
    printf 'qemu_twin_boringroot_slice_sha256=%s\n' "$qemu_root_slice_sha"
    printf 'physical_gpt_head_sha256=%s\n' "$physical_gpt_head_sha"
    printf 'qemu_twin_gpt_head_sha256=%s\n' "$qemu_gpt_head_sha"
    printf 'physical_gpt_tail_sha256=%s\n' "$physical_gpt_tail_sha"
    printf 'qemu_twin_gpt_tail_sha256=%s\n' "$qemu_gpt_tail_sha"
    printf 'qemu_twin_raw_sha256=%s\n' "$qemu_sha"
    printf 'qemu_twin_publishable=no\n'
} > "$TWIN_PROOF"
cat "$TWIN_PROOF"
printf '%s\n' 'M61 QEMU TEST-TWIN EQUIVALENCE PROOF: PASS'

actual_bytes=$(wc -c < "$IMAGE" | tr -d ' ')
[ "$actual_bytes" -eq "$IMAGE_BYTES" ] || { echo 'M61 raw image size drift' >&2; exit 1; }
sha=$(sha256sum "$IMAGE" | awk '{print $1}')
printf '%s  %s\n' "$sha" "$(basename "$IMAGE")" > "$SHA_FILE"
xz -T1 -9e -c "$IMAGE" > "$COMPRESSED"
compressed_bytes=$(wc -c < "$COMPRESSED" | tr -d ' ')
version=$(grep -Eo 'BoringKernel 0\.0\.[0-9]+-dev' kernel/core/entry.c | head -n 1 || true)
[ -n "$version" ] || version='BoringKernel unknown'
commit=${GITHUB_SHA:-}
if [ -z "$commit" ] && command -v git >/dev/null 2>&1; then commit=$(git rev-parse HEAD 2>/dev/null || true); fi
[ -n "$commit" ] || commit=unknown
{
    printf 'filename=%s\n' "$(basename "$IMAGE")"
    printf 'bytes=%s\n' "$actual_bytes"
    printf 'sha256=%s\n' "$sha"
    printf 'compressed_filename=%s\n' "$(basename "$COMPRESSED")"
    printf 'compressed_bytes=%s\n' "$compressed_bytes"
    printf 'sector_size=%s\n' "$SECTOR"
    printf 'image_sectors=%s\n' "$IMAGE_SECTORS"
    printf 'esp_first_lba=%s\n' "$ESP_FIRST"
    printf 'esp_sectors=%s\n' "$ESP_SECTORS"
    printf 'root_first_lba=%s\n' "$ROOT_FIRST"
    printf 'root_sectors=%s\n' "$ROOT_SECTORS"
    printf 'root_bytes=%s\n' "$ROOT_BYTES"
    printf 'boringfs_payload_bytes=%s\n' "$root_bytes"
    printf 'build_commit=%s\n' "$commit"
    printf 'kernel_version=%s\n' "$version"
    printf 'physical_breadcrumbs=enabled\n'
    printf 'limine_timeout_seconds=5\n'
    printf 'limine_mouse_countdown_cancel=disabled\n'
    printf 'qemu_test_twin=generated-not-publishable\n'
} > "$META"
cat "$META"
printf '%s\n' 'M61 FLASHABLE PHYSICAL USB IMAGE BUILT; QEMU TEST TWIN NOT FOR RELEASE'
