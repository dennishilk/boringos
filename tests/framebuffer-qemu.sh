#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
QEMU=${QEMU:-qemu-system-x86_64}
QEMU_CPU=${QEMU_CPU:-qemu64,apic=off}
TMPDIR_PATH=$(mktemp -d)
IMAGE="${TMPDIR_PATH}/boringos-root.img"
LOG="${TMPDIR_PATH}/serial.log"
QEMU_LOG="${TMPDIR_PATH}/qemu.log"
PIPE_BASE="${TMPDIR_PATH}/serial"
QMP_SOCKET="${TMPDIR_PATH}/qmp.sock"
SCREENSHOT="${ROOT}/build/framebuffer-reference.ppm"
PID=
CAT_PID=
FD_OPEN=0
PROMPT=0

stop_vm() {
    if [ -n "${PID}" ] && kill -0 "${PID}" 2>/dev/null; then
        kill "${PID}" 2>/dev/null || true
        wait "${PID}" 2>/dev/null || true
    fi
    PID=
    if [ "${FD_OPEN}" -eq 1 ]; then exec 3>&-; FD_OPEN=0; fi
    if [ -n "${CAT_PID}" ] && kill -0 "${CAT_PID}" 2>/dev/null; then
        kill "${CAT_PID}" 2>/dev/null || true
        wait "${CAT_PID}" 2>/dev/null || true
    fi
    CAT_PID=
}
cleanup() { stop_vm; rm -rf "${TMPDIR_PATH}"; }
trap cleanup EXIT INT TERM
fail() {
    echo "$1" >&2
    cat "${LOG}" >&2 2>/dev/null || true
    cat "${QEMU_LOG}" >&2 2>/dev/null || true
    exit 1
}
prompt_count() {
    count=$(grep -Ec '^boring@boringos:/[^$]*\$ ' "${LOG}" 2>/dev/null || true)
    printf '%s\n' "${count:-0}"
}
wait_prompt() {
    target=$1
    attempt=0
    while [ "${attempt}" -lt 400 ]; do
        if grep -Eiq 'acceptance FAILED|boring-shell: FAILED|boring-init: FAILED|Fatal exception|BoringKernel syscall fatal' "${LOG}" 2>/dev/null; then
            fail 'failure marker during framebuffer acceptance'
        fi
        if [ "$(prompt_count)" -ge "${target}" ]; then
            PROMPT=${target}
            return 0
        fi
        kill -0 "${PID}" 2>/dev/null || fail 'QEMU exited before shell prompt'
        attempt=$((attempt + 1))
        sleep 0.1
    done
    fail 'shell prompt timeout'
}
send() {
    printf '%s\n' "$1" >&3
    wait_prompt $((PROMPT + 1))
}

rm -f "${SCREENSHOT}"
make -C "${ROOT}" boringfs-fixture boringfsck user-boringfetch user-cat
"${ROOT}/build/boringfs-fixture" "${IMAGE}" valid \
    "${ROOT}/build/user/boringfetch.elf" \
    "${ROOT}/build/user/cat.elf" >/dev/null
"${ROOT}/build/boringfsck" "${IMAGE}" | grep -Fqx 'Status: VALID' ||
    fail 'seeded framebuffer acceptance image is invalid'
make -C "${ROOT}" TEST_MODE=persistent-root

mkfifo "${PIPE_BASE}.in" "${PIPE_BASE}.out"
QMP_BACKEND=$(sh "${ROOT}/tests/qmp-backend.sh" "${QMP_SOCKET}")
exec 3<> "${PIPE_BASE}.in"; FD_OPEN=1
stdbuf -o0 tr -d '\r' < "${PIPE_BASE}.out" > "${LOG}" & CAT_PID=$!
"${QEMU}" -M q35 -cpu "${QEMU_CPU}" -m 128M \
    -cdrom "${ROOT}/build/boringos.iso" -boot d \
    -drive "file=${IMAGE},if=none,format=raw,id=boringdisk" \
    -device "virtio-blk-pci,drive=boringdisk,disable-legacy=on" \
    -vga std -display none \
    -serial "pipe:${PIPE_BASE}" -monitor none \
    -qmp "${QMP_BACKEND}" \
    -no-reboot -no-shutdown >/dev/null 2> "${QEMU_LOG}" & PID=$!

wait_prompt 1
for line in \
    'BoringKernel 0.0.39-dev' \
    'boring-framebuffer: detected' \
    'boring-framebuffer: rgb validated' \
    'boring-graphics: primitives online' \
    'boring-graphics: pixel font online' \
    'BoringFS root mounted.' \
    'boring-graphics: dashboard rendered' \
    'boring-init: pid 1' \
    'boring-shell: pid 2' \
    'boring-shell ready.'; do
    grep -Fqx "${line}" "${LOG}" || fail "missing framebuffer boot marker: ${line}"
done
grep -Eq '^boring-framebuffer: [0-9]+x[0-9]+x(24|32)$' "${LOG}" ||
    fail 'missing supported framebuffer geometry marker'
grep -Eq '^boring-framebuffer: pitch [1-9][0-9]*$' "${LOG}" ||
    fail 'missing framebuffer pitch marker'

send 'boringfetch'
grep -Fqx 'boring-launch: VFS executable source /bin/boringfetch' "${LOG}" ||
    fail 'boringfetch did not execute from persistent VFS after dashboard render'
grep -Fq 'Root FS: BoringFS' "${LOG}" || fail 'boringfetch root identity regressed'
grep -Fq 'Root device: virtio-blk' "${LOG}" || fail 'boringfetch block identity regressed'

send 'cat /README.txt'
grep -Fqx 'Welcome to BoringOS.' "${LOG}" ||
    fail 'standalone cat did not read persistent-root README'
grep -Eq '^fd-open: pid [0-9]+ path /README.txt fd 3$' "${LOG}" ||
    fail 'descriptor-backed cat open marker is missing'
grep -Eq '^fd-write: pid [0-9]+ fd 1 bytes [1-9][0-9]*$' "${LOG}" ||
    fail 'descriptor-backed cat stdout marker is missing'

python3 "${ROOT}/tests/qmp-screendump.py" "${QMP_SOCKET}" "${SCREENSHOT}"
python3 "${ROOT}/tests/validate-framebuffer-screenshot.py" "${SCREENSHOT}" "${LOG}"
"${ROOT}/build/boringfsck" "${IMAGE}" | grep -Fqx 'Status: VALID' ||
    fail 'post-framebuffer BoringFS image is invalid'

stop_vm
echo 'Real Limine framebuffer and BoringOS dashboard verification passed.'
