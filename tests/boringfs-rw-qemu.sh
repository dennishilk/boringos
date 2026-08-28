#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
QEMU=${QEMU:-qemu-system-x86_64}
QEMU_CPU=${QEMU_CPU:-qemu64,apic=off}
TMPDIR_PATH=$(mktemp -d)
MAIN_IMAGE="${TMPDIR_PATH}/boringfs-main.raw"
OBJECT_IMAGE="${TMPDIR_PATH}/boringfs-full-objects.raw"
VOLUME_IMAGE="${TMPDIR_PATH}/boringfs-full-volume.raw"
PID=
CAT_PID=
SERIAL_FD_OPEN=0
LOG=
QEMU_LOG=
PROMPT=0

stop_vm() {
    if [ -n "${PID}" ] && kill -0 "${PID}" 2>/dev/null; then
        kill "${PID}" 2>/dev/null || true
        wait "${PID}" 2>/dev/null || true
    fi
    PID=
    if [ "${SERIAL_FD_OPEN}" -eq 1 ]; then
        exec 3>&-
        SERIAL_FD_OPEN=0
    fi
    if [ -n "${CAT_PID}" ] && kill -0 "${CAT_PID}" 2>/dev/null; then
        kill "${CAT_PID}" 2>/dev/null || true
        wait "${CAT_PID}" 2>/dev/null || true
    fi
    CAT_PID=
}

cleanup() {
    stop_vm
    rm -rf "${TMPDIR_PATH}"
}
trap cleanup EXIT INT TERM

fail_dump() {
    message=$1
    echo "${message}" >&2
    if [ -n "${LOG}" ]; then
        cat "${LOG}" >&2 2>/dev/null || true
    fi
    if [ -n "${QEMU_LOG}" ]; then
        cat "${QEMU_LOG}" >&2 2>/dev/null || true
    fi
    exit 1
}

failure_seen() {
    grep -Eiq 'BoringFS (read-only|writable) acceptance FAILED|boring-shell: FAILED|boring-init: FAILED|BoringKernel syscall fatal|Fatal exception: controlled halt|triple fault|reboot' "${LOG}" 2>/dev/null
}

prompt_count() {
    count=$(grep -Ec '^boring@boringos:/[^$]*\$ ' "${LOG}" 2>/dev/null || true)
    printf '%s\n' "${count:-0}"
}

wait_for_prompt() {
    target=$1
    attempt=0
    while [ "${attempt}" -lt 400 ]; do
        if failure_seen; then
            fail_dump "failure marker while waiting for prompt ${target}"
        fi
        count=$(prompt_count)
        if [ "${count}" -ge "${target}" ]; then
            PROMPT=${target}
            return 0
        fi
        if ! kill -0 "${PID}" 2>/dev/null; then
            fail_dump "QEMU exited while waiting for prompt ${target}"
        fi
        attempt=$((attempt + 1))
        sleep 0.1
    done
    fail_dump "timed out waiting for prompt ${target}"
}

send_command() {
    command=$1
    next=$((PROMPT + 1))
    printf '%s\n' "${command}" >&3
    wait_for_prompt "${next}"
}

start_vm() {
    image=$1
    phase=$2
    serial_base="${TMPDIR_PATH}/serial-${phase}"
    serial_in="${serial_base}.in"
    serial_out="${serial_base}.out"
    LOG="${TMPDIR_PATH}/serial-${phase}.log"
    QEMU_LOG="${TMPDIR_PATH}/qemu-${phase}.log"
    PROMPT=0

    mkfifo "${serial_in}" "${serial_out}"
    exec 3<> "${serial_in}"
    SERIAL_FD_OPEN=1
    stdbuf -o0 tr -d '\r' < "${serial_out}" > "${LOG}" &
    CAT_PID=$!

    "${QEMU}" \
        -M q35 \
        -cpu "${QEMU_CPU}" \
        -m 128M \
        -cdrom "${ROOT}/build/boringos.iso" \
        -boot d \
        -drive "file=${image},if=none,format=raw,id=boringdisk" \
        -device "virtio-blk-pci,drive=boringdisk,disable-legacy=on" \
        -display none \
        -serial "pipe:${serial_base}" \
        -monitor none \
        -no-reboot \
        -no-shutdown \
        > /dev/null 2> "${QEMU_LOG}" &
    PID=$!
    wait_for_prompt 1
}

validate_image() {
    image=$1
    label=$2
    output="${TMPDIR_PATH}/${label}.check"

    "${ROOT}/build/boringfsck" "${image}" > "${output}"
    if ! grep -Fqx 'Status: VALID' "${output}"; then
        cat "${output}" >&2
        fail_dump "${label}: host validator rejected image"
    fi
}

protected_sha() {
    image=$1
    {
        dd if="${image}" bs=4096 skip=0 count=1 2>/dev/null
        dd if="${image}" bs=4096 skip=3 count=1 2>/dev/null
        dd if="${image}" bs=4096 skip=5 count=10 2>/dev/null
    } | sha256sum | awk '{print $1}'
}

make -C "${ROOT}" boringfs-fixture boringfsck
"${ROOT}/build/boringfs-fixture" "${MAIN_IMAGE}" valid >/dev/null
validate_image "${MAIN_IMAGE}" preflight
BEFORE_PROTECTED=$(protected_sha "${MAIN_IMAGE}")

make -C "${ROOT}" TEST_MODE=boringfs-rw

start_vm "${MAIN_IMAGE}" first
for line in \
    'BoringKernel 0.0.44-dev' \
    '  real-virtio-volume: PASS' \
    'BoringFS writable:' \
    '  synchronous-mutations-enabled: PASS' \
    'BoringFS writable mount ready.' \
    'boring-init: pid 1' \
    'boring-shell: pid 2' \
    'boring-shell ready.'
do
    if ! grep -Fqx "${line}" "${LOG}"; then
        fail_dump "missing writable acceptance line: ${line}"
    fi
done
send_command 'cd /disk'
send_command 'ls'
send_command 'touch hello.txt'
send_command 'touch hello.txt'
if ! grep -Fqx 'touch: already exists' "${LOG}"; then
    fail_dump 'duplicate file creation was not rejected'
fi
send_command 'write hello.txt BoringOS-persistence-test'
HELLO_BEFORE=$(grep -Fxc 'hello.txt' "${LOG}" 2>/dev/null || true)
send_command 'ls'
HELLO_AFTER=$(grep -Fxc 'hello.txt' "${LOG}" 2>/dev/null || true)
[ "${HELLO_AFTER}" -eq $((HELLO_BEFORE + 1)) ] ||
    fail_dump 'first boot did not expose the newly written file'
stop_vm

validate_image "${MAIN_IMAGE}" after-first-boot
PERSISTED=$("${ROOT}/build/boringfsck" --cat /hello.txt "${MAIN_IMAGE}")
if [ "${PERSISTED}" != 'BoringOS-persistence-test' ]; then
    fail_dump 'host reader did not find exact persisted bytes after first boot'
fi
PERSISTED_SHA=$("${ROOT}/build/boringfsck" --cat /hello.txt "${MAIN_IMAGE}" |
    sha256sum | awk '{print $1}')
EXPECTED_SHA=$(printf 'BoringOS-persistence-test\n' | sha256sum | awk '{print $1}')
[ "${PERSISTED_SHA}" = "${EXPECTED_SHA}" ] ||
    fail_dump 'default write did not persist exactly one trailing newline'

start_vm "${MAIN_IMAGE}" second
send_command 'cd /disk'
HELLO_BEFORE=$(grep -Fxc 'hello.txt' "${LOG}" 2>/dev/null || true)
send_command 'ls'
HELLO_PRESENT=$(grep -Fxc 'hello.txt' "${LOG}" 2>/dev/null || true)
[ "${HELLO_PRESENT}" -eq $((HELLO_BEFORE + 1)) ] ||
    fail_dump 'second boot did not preserve the file namespace'
send_command 'rm hello.txt'
REMOVED_BEFORE=$(grep -Fxc 'hello.txt' "${LOG}" 2>/dev/null || true)
send_command 'ls'
REMOVED_AFTER=$(grep -Fxc 'hello.txt' "${LOG}" 2>/dev/null || true)
[ "${REMOVED_AFTER}" -eq "${REMOVED_BEFORE}" ] ||
    fail_dump 'removed file remained visible in the second boot'
stop_vm

validate_image "${MAIN_IMAGE}" after-remove
if "${ROOT}/build/boringfsck" --cat /hello.txt "${MAIN_IMAGE}" \
    > /dev/null 2>&1; then
    fail_dump 'host reader found a removed file'
fi

start_vm "${MAIN_IMAGE}" third
send_command 'cd /disk'
MISSING_BEFORE=$(grep -Fxc 'hello.txt' "${LOG}" 2>/dev/null || true)
send_command 'ls'
MISSING_AFTER=$(grep -Fxc 'hello.txt' "${LOG}" 2>/dev/null || true)
[ "${MISSING_AFTER}" -eq "${MISSING_BEFORE}" ] ||
    fail_dump 'removed file reappeared after reboot'
send_command 'touch reused.txt'
send_command 'write reused.txt reused-allocation'
REUSED_BEFORE=$(grep -Fxc 'reused.txt' "${LOG}" 2>/dev/null || true)
send_command 'ls'
REUSED_AFTER=$(grep -Fxc 'reused.txt' "${LOG}" 2>/dev/null || true)
[ "${REUSED_AFTER}" -eq $((REUSED_BEFORE + 1)) ] ||
    fail_dump 'reused file was not visible before host byte verification'
stop_vm

validate_image "${MAIN_IMAGE}" after-reuse
REUSED=$("${ROOT}/build/boringfsck" --cat /reused.txt "${MAIN_IMAGE}")
if [ "${REUSED}" != 'reused-allocation' ]; then
    fail_dump 'host reader did not find reused allocation contents'
fi
REUSED_SHA=$("${ROOT}/build/boringfsck" --cat /reused.txt "${MAIN_IMAGE}" |
    sha256sum | awk '{print $1}')
EXPECTED_REUSED_SHA=$(printf 'reused-allocation\n' | sha256sum | awk '{print $1}')
[ "${REUSED_SHA}" = "${EXPECTED_REUSED_SHA}" ] ||
    fail_dump 'reused file did not preserve exact newline-terminated bytes'
OBJECT11_STATE=$(od -An -tu1 -j $((2 * 4096 + 10 * 128)) -N 1 \
    "${MAIN_IMAGE}" | tr -d ' ')
BITMAP_BYTE=$(od -An -tu1 -j $((4096 + 1)) -N 1 \
    "${MAIN_IMAGE}" | tr -d ' ')
DATA15=$(dd if="${MAIN_IMAGE}" bs=1 skip=$((15 * 4096)) \
    count=17 2>/dev/null)
if [ "${OBJECT11_STATE}" -ne 1 ] ||
   [ $((BITMAP_BYTE & 128)) -eq 0 ] ||
   [ "${DATA15}" != 'reused-allocation' ]; then
    fail_dump 'deleted object/data allocation was not reused deterministically'
fi

MAX_NAME=$(printf '%240s' '' | tr ' ' a)
TOO_LONG_NAME=$(printf '%241s' '' | tr ' ' b)
start_vm "${MAIN_IMAGE}" bounds
send_command 'cd /disk'
send_command 'rm reused.txt'
send_command "touch ${MAX_NAME}"
send_command "touch ${MAX_NAME}"
if ! grep -Fqx 'touch: already exists' "${LOG}"; then
    fail_dump 'duplicate maximum-length name was not rejected'
fi
send_command "touch ${TOO_LONG_NAME}"
if ! grep -Fqx 'touch: name too long' "${LOG}"; then
    fail_dump 'overlong BoringFS name was not rejected'
fi
send_command "rm ${MAX_NAME}"
stop_vm
validate_image "${MAIN_IMAGE}" after-bounds

AFTER_PROTECTED=$(protected_sha "${MAIN_IMAGE}")
if [ "${BEFORE_PROTECTED}" != "${AFTER_PROTECTED}" ]; then
    fail_dump 'neighboring metadata/data blocks changed during mutations'
fi
echo 'Neighboring BoringFS blocks preserved: PASS'

"${ROOT}/build/boringfs-fixture" "${OBJECT_IMAGE}" valid >/dev/null
start_vm "${OBJECT_IMAGE}" full-objects
send_command 'cd /disk'
index=1
while [ "${index}" -le 54 ]; do
    name=$(printf 'o%02d' "${index}")
    send_command "touch ${name}"
    index=$((index + 1))
done
send_command 'touch object-overflow'
if ! grep -Fqx 'touch: no space' "${LOG}"; then
    fail_dump 'full object table did not report no space'
fi
stop_vm
validate_image "${OBJECT_IMAGE}" full-object-table
echo 'Full BoringFS object table remains consistent: PASS'

"${ROOT}/build/boringfs-fixture" "${VOLUME_IMAGE}" valid >/dev/null
start_vm "${VOLUME_IMAGE}" full-volume
send_command 'cd /disk'
index=1
volume_full=0
while [ "${index}" -le 54 ]; do
    name=$(printf 'v%02d' "${index}")
    send_command "touch ${name}"
    before=$(grep -Fxc 'write: no space' "${LOG}" 2>/dev/null || true)
    send_command "write ${name} x"
    after=$(grep -Fxc 'write: no space' "${LOG}" 2>/dev/null || true)
    if [ "${after}" -gt "${before}" ]; then
        volume_full=1
        break
    fi
    index=$((index + 1))
done
if [ "${volume_full}" -ne 1 ]; then
    fail_dump 'full BoringFS volume did not report no space'
fi
stop_vm
validate_image "${VOLUME_IMAGE}" full-volume
echo 'Full BoringFS volume failure path remains consistent: PASS'
echo 'BoringFS writable reboot persistence acceptance passed.'
