#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
QEMU=${QEMU:-qemu-system-x86_64}
QEMU_CPU=${QEMU_CPU:-qemu64,apic=off}
TMPDIR_PATH=$(mktemp -d)
SERIAL_BASE="${TMPDIR_PATH}/serial"
SERIAL_IN="${SERIAL_BASE}.in"
SERIAL_OUT="${SERIAL_BASE}.out"
LOG="${TMPDIR_PATH}/serial.log"
QEMU_LOG="${TMPDIR_PATH}/qemu.log"
PID=
CAT_PID=
SERIAL_FD_OPEN=0

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
    echo "$1" >&2
    cat "${LOG}" >&2 2>/dev/null || true
    cat "${QEMU_LOG}" >&2 2>/dev/null || true
    exit 1
}

prompt_count() {
    (grep -Eo 'boring@boringos:/[^$]*\$ ' "${LOG}" 2>/dev/null || true) |
        wc -l | tr -d ' '
}

wait_for_prompt() {
    target=$1
    attempt=0
    while [ "${attempt}" -lt 400 ]; do
        count=$(prompt_count)
        if [ "${count}" -ge "${target}" ]; then
            return 0
        fi
        if ! kill -0 "${PID}" 2>/dev/null; then
            fail_dump "QEMU exited while waiting for prompt ${target}"
        fi
        if grep -Eiq 'BoringKernel syscall fatal|Fatal exception: controlled halt|triple fault' "${LOG}" 2>/dev/null; then
            fail_dump "kernel failure while waiting for prompt ${target}"
        fi
        attempt=$((attempt + 1))
        sleep 0.1
    done
    fail_dump "timed out waiting for prompt ${target}"
}

make -C "${ROOT}" TEST_MODE=m69-network

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
    -display none \
    -serial "pipe:${SERIAL_BASE}" \
    -monitor none \
    -nic user,model=e1000 \
    -no-reboot \
    -no-shutdown \
    > /dev/null 2> "${QEMU_LOG}" &
PID=$!

wait_for_prompt 1
printf '%s\n' 'ping 10.0.2.2' >&3
wait_for_prompt 2

grep -Fqx 'PING 10.0.2.2 (10.0.2.2) from 10.0.2.15' "${LOG}" ||
    fail_dump 'M69 DHCP/IPv4 identity mismatch'
grep -Fqx '4 packets transmitted, 4 received, 0% packet loss' "${LOG}" ||
    fail_dump 'M69 ICMP echo acceptance failed'

for seq in 1 2 3 4; do
    grep -Eq "^64 bytes from 10\.0\.2\.2: seq=${seq} time=[0-9]+ ms$" "${LOG}" ||
        fail_dump "missing M69 ICMP reply sequence ${seq}"
done

printf '%s\n' 'M69 E1000 DHCP/ARP/IPv4/ICMP QEMU acceptance passed.'
