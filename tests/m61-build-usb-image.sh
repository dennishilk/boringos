#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

LAYOUT=kernel/include/boring/m61_usb_layout.h
IMAGE=build/boringos-m61-usb.img
COMPRESSED=$IMAGE.xz
SHA_FILE=$IMAGE.sha256
META=build/m61-usb-image.txt
WORK=build/m61-image
ESP=$WORK/esp.fat
ROOTFS=build/m61-bundle/boringos-root.img

macro() {
    awk -v name="$1" '$1 == "#define" && $2 == name { value=$3; gsub(/[ULul]/, "", value); print value; exit }' "$LAYOUT"
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

for tool in sgdisk mkfs.fat mmd mcopy xz; do
    command -v "$tool" >/dev/null 2>&1 || { echo "M61 image builder missing tool: $tool" >&2; exit 1; }
done
for file in build/kernel.elf build/user/boring-init-desktop.elf \
            build/deps/limine-binary/BOOTX64.EFI limine-m61-usb.conf "$ROOTFS"; do
    [ -f "$file" ] || { echo "M61 image builder missing: $file" >&2; exit 1; }
done
root_bytes=$(wc -c < "$ROOTFS" | tr -d ' ')
[ "$root_bytes" -le "$ROOT_BYTES" ] || { echo 'M61 BoringFS image exceeds root slice' >&2; exit 1; }

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

rm -f "$IMAGE" "$COMPRESSED" "$SHA_FILE" "$META"
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
} > "$META"
cat "$META"
printf '%s\n' 'M61 FLASHABLE USB IMAGE BUILT'
