#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

make -C "${ROOT}" user-boring-display user-display-clients

fail() {
    echo "$1" >&2
    exit 1
}

audit_common() {
    elf=$1
    label=$2
    header=$(readelf -hW "${elf}")
    programs=$(readelf -lW "${elf}")
    dynamic=$(readelf -dW "${elf}")
    relocs=$(readelf -rW "${elf}")
    symbols=$(nm -n "${elf}")
    undefined=$(nm -u "${elf}" || true)

    printf '%s\n' "${header}" | grep -Fq 'Class:                             ELF64' || fail "${label}: not ELF64"
    printf '%s\n' "${header}" | grep -Fq "Data:                              2's complement, little endian" || fail "${label}: not little endian"
    printf '%s\n' "${header}" | grep -Fq 'Type:                              EXEC (Executable file)' || fail "${label}: not ET_EXEC"
    printf '%s\n' "${header}" | grep -Fq 'Machine:                           Advanced Micro Devices X86-64' || fail "${label}: wrong machine"
    printf '%s\n' "${header}" | grep -Fq 'Entry point address:               0x40000000' || fail "${label}: wrong entry point"

    load_count=$(printf '%s\n' "${programs}" | grep -c '^  LOAD')
    [ "${load_count}" -eq 3 ] || fail "${label}: expected 3 PT_LOAD entries, got ${load_count}"
    if printf '%s\n' "${programs}" | grep -Eq '^  (INTERP|DYNAMIC|TLS)'; then
        fail "${label}: unexpected PT_INTERP/PT_DYNAMIC/PT_TLS"
    fi
    if printf '%s\n' "${programs}" | grep '^  LOAD' | grep -Eq ' W E '; then
        fail "${label}: writable+executable PT_LOAD"
    fi
    printf '%s\n' "${dynamic}" | grep -Fq 'There is no dynamic section in this file.' || fail "${label}: dynamic section"
    printf '%s\n' "${relocs}" | grep -Fq 'There are no relocations in this file.' || fail "${label}: relocations"
    [ -z "${undefined}" ] || { echo "${label}: unresolved symbols:" >&2; printf '%s\n' "${undefined}" >&2; exit 1; }
    if printf '%s\n' "${symbols}" | grep -Eq '(__libc_start_main|__stack_chk_fail| printf$| snprintf$| malloc$| calloc$| realloc$| free$| fopen$| open$| close$)'; then
        fail "${label}: host CRT/libc dependency"
    fi
    size=$(wc -c < "${elf}")
    [ "${size}" -le 65536 ] || fail "${label}: exceeds one 16-block BoringFS program slot (${size} bytes)"
}

SERVICE="${ROOT}/build/user/boring-display.elf"
CLIENT_A="${ROOT}/build/user/display-client-a.elf"
CLIENT_B="${ROOT}/build/user/display-client-b.elf"

audit_common "${SERVICE}" boring-display
audit_common "${CLIENT_A}" display-client-a
audit_common "${CLIENT_B}" display-client-b

SERVICE_SYMBOLS=$(nm -n "${SERVICE}")
SERVICE_DISASSEMBLY=$(objdump -d "${SERVICE}")
for symbol in _start boring_main boring_service_register boring_service_accept boring_ipc_send boring_ipc_receive boring_buffer_create boring_buffer_map boring_buffer_info boring_framebuffer_claim boring_framebuffer_present boring_input_claim boring_input_read boring_display_compose; do
    printf '%s\n' "${SERVICE_SYMBOLS}" | grep -Eq " [Tt] ${symbol}$" || fail "boring-display: missing symbol ${symbol}"
done
for symbol in boring_service_register boring_service_accept boring_ipc_receive boring_buffer_info boring_framebuffer_claim boring_framebuffer_present boring_input_claim boring_input_read; do
    printf '%s\n' "${SERVICE_DISASSEMBLY}" | grep -Eq "call[q]?[[:space:]].*<${symbol}>" || fail "boring-display: does not call ${symbol}"
done

for entry in "display-client-a:${CLIENT_A}" "display-client-b:${CLIENT_B}"; do
    label=${entry%%:*}
    elf=${entry#*:}
    symbols=$(nm -n "${elf}")
    disassembly=$(objdump -d "${elf}")
    for symbol in _start boring_main boring_service_connect boring_ipc_send boring_ipc_receive boring_buffer_create boring_buffer_map boring_buffer_unmap boring_buffer_close boring_exit; do
        printf '%s\n' "${symbols}" | grep -Eq " [Tt] ${symbol}$" || fail "${label}: missing symbol ${symbol}"
    done
    for symbol in boring_service_connect boring_ipc_send boring_ipc_receive boring_buffer_create boring_buffer_map; do
        printf '%s\n' "${disassembly}" | grep -Eq "call[q]?[[:space:]].*<${symbol}>" || fail "${label}: does not call ${symbol}"
    done
done

echo 'M34 boring-display and display-client ELF audits passed.'
printf 'boring-display size: %s bytes\n' "$(wc -c < "${SERVICE}")"
printf 'display-client-a size: %s bytes\n' "$(wc -c < "${CLIENT_A}")"
printf 'display-client-b size: %s bytes\n' "$(wc -c < "${CLIENT_B}")"
