#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

make -C "${ROOT}" user-boringwm

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
    if printf '%s\n' "${programs}" | awk '
        /^  LOAD/ {
            flags = ""
            for (field = 7; field < NF; ++field) {
                flags = flags $field
            }
            if ((index(flags, "W") != 0) && (index(flags, "E") != 0)) {
                bad = 1
            }
        }
        END { exit bad ? 0 : 1 }
    '; then
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


for name in boringwm boringwm-death boring-display-wm wm-client-a wm-client-b wm-client-c; do
    elf="${ROOT}/build/user/${name}.elf"
    audit_common "${elf}" "${name}"
    for symbol in _start boring_main boring_ipc_send boring_ipc_receive boring_service_connect boring_exit; do
        nm -n "${elf}" | grep -Eq " [Tt] ${symbol}$" || fail "${name}: missing ${symbol}"
    done
    printf '%s size: %s bytes\n' "${name}" "$(wc -c < "${elf}")"
done
WM="${ROOT}/build/user/boringwm.elf"
for symbol in wm_layout wm_focus_step wm_reorder wm_key boring_event_wait; do
    objdump -d "${WM}" | grep -Eq "call[q]?[[:space:]].*<${symbol}>" || fail "WM does not call ${symbol}"
done
if objdump -d "${WM}" | grep -Eq 'call[q]?[[:space:]].*<(boring_buffer_map|boring_buffer_create|boring_framebuffer_claim|boring_framebuffer_present)>'; then
    fail 'WM acquires pixel or framebuffer authority'
fi
for name in wm-client-a wm-client-b wm-client-c; do
    for symbol in boring_buffer_create boring_buffer_map boring_service_connect boring_ipc_send; do
        objdump -d "${ROOT}/build/user/${name}.elf" | grep -Eq "call[q]?[[:space:]].*<${symbol}>" || fail "${name} lacks real ${symbol} call"
    done
done
printf '%s\n' 'M35 native WM, display extension and client ELF audits passed.'
