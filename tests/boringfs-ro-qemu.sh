#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
QEMU=${QEMU:-qemu-system-x86_64}
QEMU_CPU=${QEMU_CPU:-qemu64,apic=off}
TMPDIR_PATH=$(mktemp -d)
VALID_IMAGE="${TMPDIR_PATH}/boringfs-valid.raw"
SERIAL_BASE="${TMPDIR_PATH}/serial"
SERIAL_IN="${SERIAL_BASE}.in"
SERIAL_OUT="${SERIAL_BASE}.out"
LOG="${TMPDIR_PATH}/serial.log"
QEMU_LOG="${TMPDIR_PATH}/qemu.log"
PID=
CAT_PID=
SERIAL_FD_OPEN=0
PROMPT=0

cleanup() {
    if [ "${SERIAL_FD_OPEN}" -eq 1 ]; then
        exec 3>&-
    fi
    if [ -n "${PID}" ] && kill -0 "${PID}" 2>/dev/null; then
        kill "${PID}" 2>/dev/null || true
        wait "${PID}" 2>/dev/null || true
    fi
    if [ -n "${CAT_PID}" ] && kill -0 "${CAT_PID}" 2>/dev/null; then
        kill "${CAT_PID}" 2>/dev/null || true
        wait "${CAT_PID}" 2>/dev/null || true
    fi
    rm -rf "${TMPDIR_PATH}"
}
trap cleanup EXIT INT TERM

fail_dump() {
    message=$1
    echo "${message}" >&2
    cat "${LOG}" >&2 2>/dev/null || true
    cat "${QEMU_LOG}" >&2 2>/dev/null || true
    exit 1
}

failure_seen() {
    grep -Eiq 'BoringFS read-only acceptance FAILED|boring-shell: FAILED|boring-init: FAILED|BoringKernel syscall fatal|Fatal exception: controlled halt|triple fault|reboot' "${LOG}" 2>/dev/null
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
    printf '%s\n' "${command}" >&3
    wait_for_prompt $((PROMPT + 1))
}

make -C "${ROOT}" boringfs-fixture boringfsck
"${ROOT}/build/boringfs-fixture" "${VALID_IMAGE}" valid
"${ROOT}/build/boringfsck" "${VALID_IMAGE}" > "${TMPDIR_PATH}/preflight.log"
if ! grep -Fqx 'Status: VALID' "${TMPDIR_PATH}/preflight.log"; then
    cat "${TMPDIR_PATH}/preflight.log" >&2
    echo 'BoringFS fixture preflight failed' >&2
    exit 1
fi

make -C "${ROOT}" TEST_MODE=boringfs-ro
BEFORE_SHA=$(sha256sum "${VALID_IMAGE}" | awk '{print $1}')

mkfifo "${SERIAL_IN}" "${SERIAL_OUT}"
exec 3<> "${SERIAL_IN}"
SERIAL_FD_OPEN=1
stdbuf -o0 tr -d '\r' < "${SERIAL_OUT}" > "${LOG}" &
CAT_PID=$!

"${QEMU}" \
    -M q35 \
    -cpu "${QEMU_CPU}" \
    -m 128M \
    -cdrom "${ROOT}/build/boringos.iso" \
    -boot d \
    -drive "file=${VALID_IMAGE},if=none,format=raw,id=boringdisk" \
    -device "virtio-blk-pci,drive=boringdisk,disable-legacy=on" \
    -display none \
    -serial "pipe:${SERIAL_BASE}" \
    -monitor none \
    -no-reboot \
    -no-shutdown \
    > /dev/null 2> "${QEMU_LOG}" &
PID=$!

wait_for_prompt 1

for line in \
    'BoringKernel 0.0.60-dev' \
    '  real-virtio-volume: PASS' \
    'BoringFS over VirtIO:' \
    '  4096-to-512-mapping: PASS' \
    '  structural-validation: PASS' \
    '  mount-at-/disk: PASS' \
    'BoringFS file read:' \
    '  lookup: PASS' \
    '  extent-backed-read: PASS' \
    '  contents: PASS' \
    'BoringFS read-only:' \
    '  mkdir-denied: PASS' \
    '  create-denied: PASS' \
    '  write-denied: PASS' \
    '  truncate-denied: PASS' \
    '  rmdir-denied: PASS' \
    'BoringFS read-only mount ready.' \
    'boring-init: pid 1' \
    'boring-shell: pid 2' \
    'boring-shell ready.'
do
    if ! grep -Fqx "${line}" "${LOG}"; then
        fail_dump "missing BoringFS read-only acceptance line: ${line}"
    fi
done

DISK_BEFORE=$(grep -Fxc 'disk' "${LOG}" 2>/dev/null || true)
send_command 'ls /'
DISK_AFTER=$(grep -Fxc 'disk' "${LOG}" 2>/dev/null || true)
if [ "${DISK_AFTER}" -ne $((DISK_BEFORE + 1)) ]; then
    fail_dump 'ls / did not expose the /disk mountpoint'
fi

README_BEFORE=$(grep -Fxc 'README.txt' "${LOG}" 2>/dev/null || true)
DOCS_BEFORE=$(grep -Fxc 'docs' "${LOG}" 2>/dev/null || true)
HOME_BEFORE=$(grep -Fxc 'home' "${LOG}" 2>/dev/null || true)
SYSTEM_BEFORE=$(grep -Fxc 'system' "${LOG}" 2>/dev/null || true)
send_command 'ls /disk'
for pair in \
    "README.txt:${README_BEFORE}" \
    "docs:${DOCS_BEFORE}" \
    "home:${HOME_BEFORE}" \
    "system:${SYSTEM_BEFORE}"
do
    name=${pair%%:*}
    before=${pair#*:}
    after=$(grep -Fxc "${name}" "${LOG}" 2>/dev/null || true)
    if [ "${after}" -ne $((before + 1)) ]; then
        fail_dump "ls /disk did not expose ${name}"
    fi
done

send_command 'cd /disk'
README_BEFORE=$(grep -Fxc 'README.txt' "${LOG}" 2>/dev/null || true)
send_command 'ls'
README_AFTER=$(grep -Fxc 'README.txt' "${LOG}" 2>/dev/null || true)
if [ "${README_AFTER}" -ne $((README_BEFORE + 1)) ]; then
    fail_dump 'ls after cd /disk did not traverse mounted BoringFS'
fi

send_command 'cd docs'
HELLO_BEFORE=$(grep -Fxc 'hello.txt' "${LOG}" 2>/dev/null || true)
ARCH_BEFORE=$(grep -Fxc 'architecture.txt' "${LOG}" 2>/dev/null || true)
send_command 'ls'
HELLO_AFTER=$(grep -Fxc 'hello.txt' "${LOG}" 2>/dev/null || true)
ARCH_AFTER=$(grep -Fxc 'architecture.txt' "${LOG}" 2>/dev/null || true)
if [ "${HELLO_AFTER}" -ne $((HELLO_BEFORE + 1)) ] ||
   [ "${ARCH_AFTER}" -ne $((ARCH_BEFORE + 1)) ]; then
    fail_dump 'docs traversal did not expose real BoringFS entries'
fi

send_command 'cd ../home/dennis'
WELCOME_BEFORE=$(grep -Fxc 'welcome.txt' "${LOG}" 2>/dev/null || true)
send_command 'ls'
WELCOME_AFTER=$(grep -Fxc 'welcome.txt' "${LOG}" 2>/dev/null || true)
if [ "${WELCOME_AFTER}" -ne $((WELCOME_BEFORE + 1)) ]; then
    fail_dump 'nested BoringFS traversal did not expose welcome.txt'
fi

if failure_seen; then
    fail_dump 'unexpected failure after BoringFS shell traversal'
fi

if [ -n "${PID}" ] && kill -0 "${PID}" 2>/dev/null; then
    kill "${PID}" 2>/dev/null || true
    wait "${PID}" 2>/dev/null || true
fi
PID=
exec 3>&-
SERIAL_FD_OPEN=0
if [ -n "${CAT_PID}" ] && kill -0 "${CAT_PID}" 2>/dev/null; then
    kill "${CAT_PID}" 2>/dev/null || true
    wait "${CAT_PID}" 2>/dev/null || true
fi
CAT_PID=

AFTER_SHA=$(sha256sum "${VALID_IMAGE}" | awk '{print $1}')
if [ "${BEFORE_SHA}" != "${AFTER_SHA}" ]; then
    echo 'read-only BoringFS disk image changed during QEMU acceptance' >&2
    exit 1
fi
echo 'Read-only disk SHA-256 preservation: PASS'

for kind in bad-magic bad-geometry bad-bitmap bad-object bad-extent bad-directory; do
    image="${TMPDIR_PATH}/${kind}.raw"
    corrupt_log="${TMPDIR_PATH}/${kind}.log"
    "${ROOT}/build/boringfs-fixture" "${image}" "${kind}" >/dev/null
    if "${ROOT}/build/boringfsck" "${image}" >/dev/null 2>&1; then
        echo "boringfsck unexpectedly accepted ${kind}" >&2
        exit 1
    fi
    "${QEMU}" \
        -M q35 \
        -cpu "${QEMU_CPU}" \
        -m 128M \
        -cdrom "${ROOT}/build/boringos.iso" \
        -boot d \
        -drive "file=${image},if=none,format=raw,id=boringdisk" \
        -device "virtio-blk-pci,drive=boringdisk,disable-legacy=on" \
        -display none \
        -serial "file:${corrupt_log}" \
        -monitor none \
        -no-reboot \
        -no-shutdown \
        > /dev/null 2>> "${QEMU_LOG}" &
    corrupt_pid=$!
    attempt=0
    while [ "${attempt}" -lt 400 ]; do
        if grep -Fq 'BoringFS mount rejected: ' "${corrupt_log}" 2>/dev/null; then
            break
        fi
        if ! kill -0 "${corrupt_pid}" 2>/dev/null; then
            break
        fi
        attempt=$((attempt + 1))
        sleep 0.1
    done
    if ! grep -Fq 'BoringFS mount rejected: ' "${corrupt_log}" 2>/dev/null; then
        cat "${corrupt_log}" >&2 2>/dev/null || true
        echo "kernel did not reject ${kind}" >&2
        kill "${corrupt_pid}" 2>/dev/null || true
        wait "${corrupt_pid}" 2>/dev/null || true
        exit 1
    fi
    if grep -Fq 'BoringFS read-only mount ready.' "${corrupt_log}" 2>/dev/null ||
       grep -Fq 'boring-shell ready.' "${corrupt_log}" 2>/dev/null; then
        cat "${corrupt_log}" >&2
        echo "${kind} became partially visible through VFS" >&2
        kill "${corrupt_pid}" 2>/dev/null || true
        wait "${corrupt_pid}" 2>/dev/null || true
        exit 1
    fi
    kill "${corrupt_pid}" 2>/dev/null || true
    wait "${corrupt_pid}" 2>/dev/null || true
    echo "Corruption rejection ${kind}: PASS"
done

cat "${LOG}"
echo 'BoringKernel read-only BoringFS over VirtIO verification passed.'
