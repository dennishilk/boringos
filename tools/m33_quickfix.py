#!/usr/bin/env python3
from pathlib import Path

p = Path("user/ipc-test/main.c")
text = p.read_text()
marker = "int boring_main(void);"
if marker not in text:
    anchor = "_Static_assert(sizeof(struct test_message) == 16U,\n               \"M33 test message must be fixed-size\");\n\n"
    if text.count(anchor) != 1:
        raise SystemExit("ipc-test prototype anchor mismatch")
    text = text.replace(anchor, anchor + "int boring_main(void);\n\n", 1)
    p.write_text(text)
