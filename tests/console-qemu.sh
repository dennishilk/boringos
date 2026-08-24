#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
QEMU=${QEMU:-qemu-system-x86_64}
QEMU_CPU=${QEMU_CPU:-qemu64,apic=off}
CC=${CC:-gcc}
LD=${LD:-ld}
CONSOLE_DIR="${ROOT}/build/user/console-smoke"
CONSOLE_OBJECT="${CONSOLE_DIR}/main.o"
CONSOLE_ELF="${ROOT}/build/user/console-smoke.elf"
LOG=$(mktemp)
INPUT=$(mktemp)
PID=

cleanup() {
    if [ -n "${PID}" ] && kill -0 "${PID}" 2>/dev/null; then
        kill "${PID}" 2>/dev/null || true
        wait "${PID}" 2>/dev/null || true
    fi
    rm -f "${LOG}" "${INPUT}"
}
trap cleanup EXIT INT TERM

make -C "${ROOT}" user-runtime
mkdir -p "${CONSOLE_DIR}"

"${CC}" \
    -I"${ROOT}/user/runtime/include" -I"${ROOT}/kernel/include" \
    -std=c11 -ffreestanding -fno-stack-protector \
    -fno-pic -fno-pie -fno-builtin -fno-asynchronous-unwind-tables \
    -fno-unwind-tables -m64 -mno-red-zone -mno-80387 -mno-mmx \
    -mno-sse -mno-sse2 -O2 -Wall -Wextra -Wpedantic -Werror \
    -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes \
    -c "${ROOT}/user/console-smoke/main.c" -o "${CONSOLE_OBJECT}"

"${LD}" -nostdlib -static --build-id=none -z max-page-size=0x1000 \
    -T "${ROOT}/user/runtime-smoke/linker.ld" \
    "${ROOT}/build/user/runtime/entry.o" \
    "${ROOT}/build/user/runtime/syscall.o" \
    "${ROOT}/build/user/runtime/memory.o" \
    "${ROOT}/build/user/runtime/string.o" \
    "${CONSOLE_OBJECT}" \
    -o "${CONSOLE_ELF}"

HEADER=$(readelf -hW "${CONSOLE_ELF}")
PROGRAMS=$(readelf -lW "${CONSOLE_ELF}")
DYNAMIC=$(readelf -dW "${CONSOLE_ELF}")
RELOCS=$(readelf -rW "${CONSOLE_ELF}")
SYMBOLS=$(nm -n "${CONSOLE_ELF}")
UNDEFINED=$(nm -u "${CONSOLE_ELF}" || true)

printf '%s\n' "${HEADER}" | grep -Fq 'Type:                              EXEC (Executable file)'
printf '%s\n' "${HEADER}" | grep -Fq 'Machine:                           Advanced Micro Devices X86-64'
printf '%s\n' "${HEADER}" | grep -Fq 'Entry point address:               0x40000000'

LOAD_COUNT=$(printf '%s\n' "${PROGRAMS}" | grep -c '^  LOAD')
if [ "${LOAD_COUNT}" -ne 3 ]; then
    echo "expected exactly 3 PT_LOAD entries, got ${LOAD_COUNT}" >&2
    exit 1
fi
if printf '%s\n' "${PROGRAMS}" | grep -Eq '^  (INTERP|DYNAMIC|TLS)'; then
    echo 'unexpected PT_INTERP/PT_DYNAMIC/PT_TLS in console smoke executable' >&2
    exit 1
fi
if printf '%s\n' "${PROGRAMS}" | grep '^  LOAD' | grep -Eq ' W E '; then
    echo 'console smoke contains a writable+executable PT_LOAD' >&2
    exit 1
fi
printf '%s\n' "${DYNAMIC}" | grep -Fq 'There is no dynamic section in this file.'
printf '%s\n' "${RELOCS}" | grep -Fq 'There are no relocations in this file.'
if [ -n "${UNDEFINED}" ]; then
    echo 'console smoke has unresolved external symbols:' >&2
    printf '%s\n' "${UNDEFINED}" >&2
    exit 1
fi
for symbol in _start boring_main boring_getpid boring_console_write boring_console_read; do
    if ! printf '%s\n' "${SYMBOLS}" | grep -Eq " [Tt] ${symbol}$"; then
        echo "missing required console symbol: ${symbol}" >&2
        exit 1
    fi
done
if printf '%s\n' "${SYMBOLS}" | grep -Eq '(__libc_start_main|__stack_chk_fail| printf$| snprintf$| malloc$| free$)'; then
    echo 'unexpected host CRT/libc-style symbol in console smoke executable' >&2
    exit 1
fi

echo 'Userspace serial console build audit passed.'

make -C "${ROOT}" \
    TEST_MODE=runtime \
    TEST_HARNESS_C='kernel/core/console_test.c kernel/core/console_test_adapter.c' \
    BOOT_USER_ELF=build/user/console-smoke.elf \
    BOOT_USER_NAME=runtime-smoke.elf

printf 'K' > "${INPUT}"

"${QEMU}" \
    -M q35 \
    -cpu "${QEMU_CPU}" \
    -m 128M \
    -cdrom "${ROOT}/build/boringos.iso" \
    -boot d \
    -display none \
    -serial stdio \
    -monitor none \
    -no-reboot \
    -no-shutdown \
    < "${INPUT}" > "${LOG}" 2>&1 &
PID=$!

attempt=0
while [ "${attempt}" -lt 150 ]; do
    if grep -Fqx 'BoringKernel userspace serial console test passed.' "${LOG}" 2>/dev/null; then
        break
    fi
    if ! kill -0 "${PID}" 2>/dev/null; then
        break
    fi
    attempt=$((attempt + 1))
    sleep 0.1
done

status=0
for line in \
    'BoringKernel 0.0.13-dev' \
    'BoringKernel physical memory test passed.' \
    'BoringKernel virtual memory test passed.' \
    'BoringKernel heap test passed.' \
    'IDT: loaded' \
    'Exceptions: online' \
    'BoringKernel exception infrastructure test passed.' \
    'Userspace serial console test:' \
    '  boot-module-found: PASS' \
    '  elf64-console-image: PASS' \
    '  nx-enabled: PASS' \
    '  process-created: PASS' \
    '  load-console-image: PASS' \
    '  segment-permissions: PASS' \
    '  entry-executable: PASS' \
    '  bss-loader-zeroed: PASS' \
    '  user-stack-mapped: PASS' \
    '  higher-half-supervisor-only: PASS' \
    'Console process PID: 1' \
    'Entering BoringOS serial console client at CPL3.' \
    'console write from BoringOS userspace' \
    'K' \
    'Userspace serial console:' \
    '  c-entry: PASS' \
    '  initialized-data: PASS' \
    '  bss: PASS' \
    '  getpid: PASS' \
    '  console-write: PASS' \
    '  console-read: PASS' \
    '  console-echo: PASS' \
    '  sysret-resume: PASS' \
    '  boring-main-return: PASS' \
    '  final-cpl3-proof: PASS' \
    '  final-tss-rsp0: PASS' \
    'Console input byte: 75' \
    'Console boring_main return: 43' \
    'Console syscall dispatches: 4' \
    '  cleanup: PASS' \
    'BoringKernel userspace serial console test passed.'
do
    if ! grep -Fqx "${line}" "${LOG}"; then
        echo "missing serial console acceptance line: ${line}" >&2
        status=1
    fi
done

if grep -Eiq 'Userspace serial console self-test FAILED|BoringKernel syscall fatal|triple fault|reboot' "${LOG}"; then
    echo 'unexpected userspace serial console failure path' >&2
    status=1
fi

cat "${LOG}"

if [ "${status}" -ne 0 ]; then
    echo 'BoringKernel userspace serial console verification FAILED.' >&2
    exit "${status}"
fi

echo 'BoringKernel userspace serial console verification passed.'
