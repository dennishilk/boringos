#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
QEMU=${QEMU:-qemu-system-x86_64}
QEMU_CPU=${QEMU_CPU:-qemu64,apic=off}
TMPDIR_PATH=$(mktemp -d)
DIAG_DIR="${ROOT}/build/m33-ipc-diagnostics"
LOG="${DIAG_DIR}/serial.log"
QEMU_LOG="${DIAG_DIR}/qemu.stderr.log"
EXPECTED="${DIAG_DIR}/expected-witnesses.txt"
KEYWORD_LOG="${DIAG_DIR}/keyword-context.log"
REPORT="${DIAG_DIR}/diagnostic-report.txt"
PID=
QEMU_EXIT_STATUS='not observed'
QEMU_OUTCOME='not started'

cleanup() {
    if [ -n "${PID}" ] && kill -0 "${PID}" 2>/dev/null; then
        kill "${PID}" 2>/dev/null || true
        wait "${PID}" 2>/dev/null || true
    fi
    rm -rf "${TMPDIR_PATH}"
}
trap cleanup EXIT INT TERM

qemu_is_running() {
    [ -n "${PID}" ] || return 1
    kill -0 "${PID}" 2>/dev/null || return 1
    if [ -r "/proc/${PID}/stat" ]; then
        state=$(awk '{ print $3 }' "/proc/${PID}/stat" 2>/dev/null || true)
        [ "${state}" != 'Z' ] || return 1
    fi
    return 0
}

witness_seen() {
    witness=$1
    grep -Fqx "${witness}" "${LOG}" 2>/dev/null ||
        grep -Fqx "Syscall DEBUG_WRITE: ${witness}" "${LOG}" 2>/dev/null
}

record_qemu_exit() {
    [ -n "${PID}" ] || return 0
    if qemu_is_running; then
        return 1
    fi
    set +e
    wait "${PID}" 2>/dev/null
    QEMU_EXIT_STATUS=$?
    set -e
    PID=
    if [ "${QEMU_EXIT_STATUS}" -eq 0 ]; then
        QEMU_OUTCOME='exited normally before acceptance'
    else
        QEMU_OUTCOME='crashed or exited with error before acceptance'
    fi
    return 0
}

write_diagnostics() {
    reason=$1
    first_missing=
    last_success=

    if [ -f "${LOG}" ]; then
        grep -Ein -C 3 'M33|ipc|service|pid|process|syscall|blocked|block|wake|switch|fault|exception|panic|page|gp|stack' "${LOG}" > "${KEYWORD_LOG}" 2>/dev/null || true
    else
        : > "${KEYWORD_LOG}"
    fi

    if [ -f "${EXPECTED}" ]; then
        while IFS= read -r witness; do
            [ -n "${witness}" ] || continue
            if witness_seen "${witness}"; then
                if [ -z "${first_missing}" ]; then
                    last_success=${witness}
                fi
            elif [ -z "${first_missing}" ]; then
                first_missing=${witness}
            fi
        done < "${EXPECTED}"
    fi

    {
        echo '=== M33 three-process QEMU diagnostic report ==='
        echo "failure: ${reason}"
        echo "QEMU outcome: ${QEMU_OUTCOME}"
        echo "QEMU exit status: ${QEMU_EXIT_STATUS}"
        echo
        echo '=== ordered expected M33 witnesses ==='
        nl -ba "${EXPECTED}" 2>/dev/null || true
        echo
        echo "last successful ordered M33 witness: ${last_success:-<none>}"
        echo "first missing ordered M33 witness: ${first_missing:-<none>}"
        echo
        echo '=== serial context around last successful M33 witness ==='
        if [ -n "${last_success}" ] && [ -f "${LOG}" ]; then
            line_no=$(grep -Fnx "${last_success}" "${LOG}" 2>/dev/null | tail -n 1 | cut -d: -f1 || true)
            if [ -z "${line_no}" ]; then
                line_no=$(grep -Fnx "Syscall DEBUG_WRITE: ${last_success}" "${LOG}" 2>/dev/null | tail -n 1 | cut -d: -f1 || true)
            fi
            if [ -z "${line_no}" ]; then
                line_no=$(grep -Fnx "Syscall DEBUG_WRITE: ${last_success}" "${LOG}" 2>/dev/null | tail -n 1 | cut -d: -f1 || true)
            fi
            if [ -n "${line_no}" ]; then
                start=$((line_no > 8 ? line_no - 8 : 1))
                end=$((line_no + 8))
                sed -n "${start},${end}p" "${LOG}" || true
            else
                echo '<witness line number unavailable>'
            fi
        else
            echo '<no successful ordered M33 witness>'
        fi
        echo
        echo '=== M33/IPC/scheduler/fault keyword context (final 400 lines) ==='
        tail -n 400 "${KEYWORD_LOG}" 2>/dev/null || true
        echo
        echo '=== serial output (final 800 lines; full raw log is preserved as artifact) ==='
        tail -n 800 "${LOG}" 2>/dev/null || true
        echo
        echo '=== QEMU stderr (final 200 lines; full raw log is preserved as artifact) ==='
        tail -n 200 "${QEMU_LOG}" 2>/dev/null || true
    } > "${REPORT}"

    cat "${REPORT}" >&2
}

fail() {
    reason=$1
    write_diagnostics "${reason}"
    exit 1
}

make -C "${ROOT}" TEST_MODE=m33-ipc
rm -rf "${DIAG_DIR}"
mkdir -p "${DIAG_DIR}"
cat > "${EXPECTED}" <<'EOF'
ipc-test: three distinct processes ready
ipc-test: service registered
ipc-test: blocking accept wake passed
ipc-test: negative syscall paths passed
ipc-test: M32 shared-buffer grant passed
ipc-test: queued buffer lifetime passed
ipc-test: FIFO and queue-full transaction passed
ipc-test: sender retains capability and alias passed
ipc-test: peer close passed
ipc-test: blocking receive wake passed
ipc-test: process-exit service removal passed
ipc-test: same-name re-registration passed
ipc-test: IPC and M32 resources reclaimed
ipc-test: process-local handle isolation passed
ipc-test: process-exit cleanup passed
M33 native IPC/service acceptance passed.
EOF
: > "${LOG}"
: > "${QEMU_LOG}"
QEMU_OUTCOME='running'
"${QEMU}" -M q35 -cpu "${QEMU_CPU}" -m 128M \
    -cdrom "${ROOT}/build/boringos.iso" -boot d \
    -display none -serial "file:${LOG}" -monitor none \
    -no-reboot -no-shutdown >/dev/null 2> "${QEMU_LOG}" & PID=$!

attempt=0
while [ "${attempt}" -lt 600 ]; do
    grep -Fq 'M33 native IPC/service acceptance passed.' "${LOG}" 2>/dev/null && break
    if grep -Eiq 'M33 IPC acceptance FAILED|ipc-test: FAILED|BoringKernel M33 syscall fatal|Fatal exception' "${LOG}" 2>/dev/null; then
        QEMU_OUTCOME='still running when deterministic failure marker was observed'
        fail 'M33 IPC QEMU failure marker'
    fi
    if ! qemu_is_running; then
        record_qemu_exit || true
        fail 'QEMU exited before M33 IPC acceptance'
    fi
    attempt=$((attempt + 1))
    sleep 0.1
done
if ! grep -Fq 'M33 native IPC/service acceptance passed.' "${LOG}"; then
    if qemu_is_running; then
        QEMU_OUTCOME='timed out while still running'
        QEMU_EXIT_STATUS='not exited (still running at timeout)'
    else
        record_qemu_exit || true
    fi
    fail 'M33 IPC acceptance timeout'
fi

for line in \
    'ipc-test: three distinct processes ready' \
    'ipc-test: service registered' \
    'ipc-test: blocking accept wake passed' \
    'ipc-test: negative syscall paths passed' \
    'ipc-test: M32 shared-buffer grant passed' \
    'ipc-test: queued buffer lifetime passed' \
    'ipc-test: FIFO and queue-full transaction passed' \
    'ipc-test: sender retains capability and alias passed' \
    'ipc-test: peer close passed' \
    'ipc-test: blocking receive wake passed' \
    'ipc-test: process-exit service removal passed' \
    'ipc-test: same-name re-registration passed' \
    'ipc-test: IPC and M32 resources reclaimed' \
    'ipc-test: process-local handle isolation passed' \
    'ipc-test: process-exit cleanup passed'; do
    witness_seen "${line}" || fail "missing M33 witness: ${line}"
done

for pid in 1 2 3; do
    grep -Eq "^ipc-test: enter CPL3 pid ${pid} cr3 0x[0-9a-f]+$" "${LOG}" || fail "missing CPL3/CR3 witness for pid ${pid}"
done
CR3_COUNT=$(grep -E '^ipc-test: enter CPL3 pid [123] cr3 0x[0-9a-f]+$' "${LOG}" | sed -E 's/.* cr3 //' | sort -u | wc -l)
[ "${CR3_COUNT}" -eq 3 ] || fail 'M33 processes did not use three distinct CR3 roots'

ACCOUNTING=$(grep -E '^ipc-test: pmm before=[0-9]+ during=[0-9]+ after=[0-9]+$' "${LOG}" | tail -n 1 || true)
[ -n "${ACCOUNTING}" ] || fail 'missing M33 PMM accounting'
set -- $(printf '%s\n' "${ACCOUNTING}" | sed -E 's/.*before=([0-9]+) during=([0-9]+) after=([0-9]+).*/\1 \2 \3/')
[ "$2" -lt "$1" ] || fail "M33 PMM did not decrease: ${ACCOUNTING}"
[ "$3" -gt "$2" ] || fail "M33 PMM did not recover: ${ACCOUNTING}"

echo "M33 PMM witness: ${ACCOUNTING}"
echo 'Real three-process Ring3 M33 IPC acceptance passed.'
