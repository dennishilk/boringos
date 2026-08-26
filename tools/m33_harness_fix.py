#!/usr/bin/env python3
from pathlib import Path

p = Path("tests/ipc-qemu.sh")
text = p.read_text()

if "witness_seen()" not in text:
    anchor = '''record_qemu_exit() {
'''
    helper = '''witness_seen() {
    witness=$1
    grep -Fqx "${witness}" "${LOG}" 2>/dev/null ||
        grep -Fqx "Syscall DEBUG_WRITE: ${witness}" "${LOG}" 2>/dev/null
}

'''
    if text.count(anchor) != 1:
        raise SystemExit("ipc-qemu witness helper anchor mismatch")
    text = text.replace(anchor, helper + anchor, 1)

old = '''            if grep -Fqx "${witness}" "${LOG}" 2>/dev/null; then
'''
new = '''            if witness_seen "${witness}"; then
'''
if old in text:
    text = text.replace(old, new, 1)
elif new not in text:
    raise SystemExit("diagnostic witness check anchor mismatch")

old = '''            line_no=$(grep -Fnx "${last_success}" "${LOG}" 2>/dev/null | tail -n 1 | cut -d: -f1 || true)
'''
new = '''            line_no=$(grep -Fnx "${last_success}" "${LOG}" 2>/dev/null | tail -n 1 | cut -d: -f1 || true)
            if [ -z "${line_no}" ]; then
                line_no=$(grep -Fnx "Syscall DEBUG_WRITE: ${last_success}" "${LOG}" 2>/dev/null | tail -n 1 | cut -d: -f1 || true)
            fi
'''
if old in text:
    text = text.replace(old, new, 1)
elif new not in text:
    raise SystemExit("diagnostic witness context anchor mismatch")

old = '''    grep -Fqx "${line}" "${LOG}" || fail "missing M33 witness: ${line}"
'''
new = '''    witness_seen "${line}" || fail "missing M33 witness: ${line}"
'''
if old in text:
    text = text.replace(old, new, 1)
elif new not in text:
    raise SystemExit("acceptance witness anchor mismatch")

p.write_text(text)
