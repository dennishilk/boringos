#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
QEMU=${QEMU:-qemu-system-x86_64}
QEMU_CPU=${QEMU_CPU:-qemu64,apic=off}
TMPDIR_PATH=$(mktemp -d)
LOG="${TMPDIR_PATH}/serial.log"
QEMU_LOG="${TMPDIR_PATH}/qemu.log"
BUILD_LOG="${TMPDIR_PATH}/build.log"
QMP="${TMPDIR_PATH}/qmp.sock"
SCREENSHOT="${ROOT}/build/m34-display-reference.ppm"
DIAG_DIR="${ROOT}/build/m34-display-diagnostics"
PID=
FAIL_REASON=

stop_vm() {
    if [ -n "${PID}" ] && kill -0 "${PID}" 2>/dev/null; then
        kill "${PID}" 2>/dev/null || true
        wait "${PID}" 2>/dev/null || true
    fi
    PID=
}

cleanup() {
    stop_vm
    rm -rf "${TMPDIR_PATH}"
}
trap cleanup EXIT INT TERM

preserve_failure() {
    reason=$1
    mkdir -p "${DIAG_DIR}"
    printf '%s\n' "${reason}" > "${DIAG_DIR}/failure.txt"
    if [ -n "${PID}" ] && kill -0 "${PID}" 2>/dev/null; then
        printf '%s\n' 'qemu-state: running-at-failure' >> "${DIAG_DIR}/failure.txt"
    else
        printf '%s\n' 'qemu-state: exited-before-success' >> "${DIAG_DIR}/failure.txt"
    fi
    cp "${BUILD_LOG}" "${DIAG_DIR}/build.log" 2>/dev/null || true
    cp "${LOG}" "${DIAG_DIR}/serial.log" 2>/dev/null || true
    cp "${QEMU_LOG}" "${DIAG_DIR}/qemu.log" 2>/dev/null || true
    if [ -f "${SCREENSHOT}" ]; then
        cp "${SCREENSHOT}" "${DIAG_DIR}/m34-display-reference.ppm" 2>/dev/null || true
    fi
}

fail() {
    FAIL_REASON=$1
    preserve_failure "${FAIL_REASON}"
    echo "${FAIL_REASON}" >&2
    echo '--- M34 build/audit context ---' >&2
    tail -n 120 "${BUILD_LOG}" >&2 2>/dev/null || true
    echo '--- last M34 serial context ---' >&2
    tail -n 100 "${LOG}" >&2 2>/dev/null || true
    echo '--- QEMU stderr ---' >&2
    cat "${QEMU_LOG}" >&2 2>/dev/null || true
    exit 1
}

wait_for() {
    needle=$1
    attempt=0
    while [ "${attempt}" -lt 600 ]; do
        if grep -Fq "${needle}" "${LOG}" 2>/dev/null; then
            return 0
        fi
        if grep -Eiq 'M34 display acceptance FAILED|boring-display: FAILED|display-client-a: FAILED|display-client-b: FAILED|BoringKernel M34 syscall fatal|Fatal exception|M34 display acceptance unexpected exception' "${LOG}" 2>/dev/null; then
            fail "failure marker while waiting for: ${needle}"
        fi
        if [ -n "${PID}" ] && ! kill -0 "${PID}" 2>/dev/null; then
            fail "QEMU exited while waiting for: ${needle}"
        fi
        attempt=$((attempt + 1))
        sleep 0.1
    done
    fail "timeout waiting for: ${needle}"
}

qmp_input() {
    python3 "${ROOT}/tests/qmp-input.py" "${QMP}" "$@" ||
        fail "QMP input injection failed: $*"
}

qmp_move_steps() {
    dx=$1
    dy=$2
    steps=$3
    step=0
    while [ "${step}" -lt "${steps}" ]; do
        qmp_input move "${dx}" "${dy}"
        step=$((step + 1))
    done
}

require_line() {
    grep -Fqx "$1" "${LOG}" || fail "missing exact M34 witness: $1"
}

rm -rf "${DIAG_DIR}"
rm -f "${SCREENSHOT}"
: > "${BUILD_LOG}"
: > "${LOG}"
: > "${QEMU_LOG}"

if ! {
    make -C "${ROOT}" display-host-test display-audit &&
    make -C "${ROOT}" TEST_MODE=m34-display
} > "${BUILD_LOG}" 2>&1; then
    fail 'M34 display build/audit failed'
fi
cat "${BUILD_LOG}"

"${QEMU}" -M q35 -cpu "${QEMU_CPU}" -m 128M \
    -cdrom "${ROOT}/build/boringos.iso" -boot d \
    -vga std -display none \
    -serial "file:${LOG}" -monitor none \
    -qmp "unix:${QMP},server=on,wait=off" \
    -no-reboot -no-shutdown >/dev/null 2> "${QEMU_LOG}" &
PID=$!

wait_for 'M34 native boring-display acceptance:'
wait_for 'display-test: real PS/2 mouse path online'
wait_for 'display-test: three distinct processes ready'
wait_for 'boring-display: service boring.display registered'
wait_for 'boring-display: framebuffer claim passed'
wait_for 'boring-display: scanout XRGB8888 validated'
wait_for 'boring-display: cursor initial center'
wait_for 'boring-display: M31 input claimed'
wait_for 'boring-display: client A connected via M33'
wait_for 'display-client-a: shared surface granted'
wait_for 'boring-display: live shared-buffer COMMIT passed'
wait_for 'display-client-a: live COMMIT acknowledged'
wait_for 'boring-display: client B connected via M33'
wait_for 'display-client-b: shared surface granted'
wait_for 'display-client-b: foreign token rejected'
wait_for 'boring-display: cross-client authority isolation passed'
wait_for 'display-client-b: own COMMIT acknowledged'
wait_for 'boring-display: deterministic stacking passed'

# Stay within a single signed PS/2 packet delta. Sixteen 127-pixel steps span
# 2032 pixels, enough to cross the full M34 maximum width (1920) and height
# (1080) from any legal cursor position without relying on oversized QMP
# relative-motion handling.
wait_for 'boring-display: waiting for cursor clip top-left'
qmp_move_steps -127 -127 16
wait_for 'boring-display: cursor clipped top-left and presented'

wait_for 'boring-display: waiting for cursor clip bottom-right'
qmp_move_steps 127 127 16
wait_for 'boring-display: cursor clipped bottom-right and presented'

wait_for 'boring-display: waiting for cursor witness position'
qmp_move_steps -127 -127 16
qmp_input move 40 30
wait_for 'boring-display: visual witness ready cursor=40,30 surfaces=2'
wait_for 'boring-display: framebuffer present witness complete'
wait_for 'boring-display: waiting for visual witness capture release'

if grep -Fq 'display-client-a: exiting without destroy' "${LOG}"; then
    fail 'Client A exited before framebuffer witness capture'
fi
if grep -Fq 'display-client-b: destroy acknowledged' "${LOG}"; then
    fail 'Client B destroyed its surface before framebuffer witness capture'
fi

python3 "${ROOT}/tests/qmp-screendump.py" "${QMP}" "${SCREENSHOT}" ||
    fail 'QMP framebuffer screendump failed'
python3 "${ROOT}/tests/validate-display-screenshot.py" "${SCREENSHOT}" "${LOG}" ||
    fail 'deterministic M34 framebuffer validation failed'

# The screenshot is now safely captured. Release the userspace barrier with a
# second real mouse event through the same QMP -> PS/2 -> M31 path.
qmp_input move 1 0
wait_for 'boring-display: visual witness capture released by real input'
wait_for 'display-client-a: exiting without destroy'
wait_for 'boring-display: client A death cleanup passed'
wait_for 'display-client-b: destroy acknowledged'
wait_for 'boring-display: client B surface destroyed'
wait_for 'boring-display: exiting with live service and device claims'
wait_for 'display-test: IPC/input/framebuffer/M32 resources reclaimed'
wait_for 'display-test: process-exit cleanup passed'
wait_for 'M34 native boring-display acceptance passed.'

for line in \
    'boring-display: client A surface created from granted M32 buffer' \
    'boring-display: client B surface created from granted M32 buffer' \
    'boring-display: live shared-buffer COMMIT passed' \
    'boring-display: cross-client authority isolation passed' \
    'boring-display: deterministic stacking passed' \
    'boring-display: cursor clipped top-left and presented' \
    'boring-display: cursor clipped bottom-right and presented' \
    'boring-display: visual witness ready cursor=40,30 surfaces=2' \
    'boring-display: framebuffer present witness complete' \
    'boring-display: client A death cleanup passed' \
    'boring-display: client B surface destroyed' \
    'display-test: IPC/input/framebuffer/M32 resources reclaimed' \
    'display-test: process-exit cleanup passed' \
    'M34 native boring-display acceptance passed.'; do
    require_line "${line}"
done

CPL3_COUNT=$(grep -c '^display-test: enter CPL3 pid ' "${LOG}" || true)
[ "${CPL3_COUNT}" -eq 3 ] || fail "expected exactly three CPL3 entry witnesses, got ${CPL3_COUNT}"
CR3_COUNT=$(grep '^display-test: enter CPL3 pid ' "${LOG}" | sed -n 's/.* cr3 //p' | sort -u | wc -l | tr -d ' ')
[ "${CR3_COUNT}" -eq 3 ] || fail "expected three distinct process address spaces, got ${CR3_COUNT} CR3 values"

if grep -Eiq 'acceptance FAILED|boring-display: FAILED|display-client-a: FAILED|display-client-b: FAILED|Fatal exception|BoringKernel M34 syscall fatal' "${LOG}"; then
    fail 'failure marker present after nominal M34 completion'
fi

stop_vm
printf '%s\n' 'Real three-process native boring-display, QMP mouse and framebuffer acceptance passed.'
printf 'M34 framebuffer reference: %s\n' "${SCREENSHOT}"
