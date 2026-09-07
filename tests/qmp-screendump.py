#!/usr/bin/env python3
import runpy
import sys
from pathlib import Path

QMP = runpy.run_path(str(Path(__file__).with_name("qmp-connection.py")))


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: qmp-screendump.py <qmp-endpoint> <output.ppm>")
    output = Path(sys.argv[2]).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with QMP["connect"](sys.argv[1]) as stream:
        QMP["execute"](stream, "screendump", {"filename": str(output)})
    if not output.is_file() or output.stat().st_size == 0:
        raise RuntimeError("screendump did not create a non-empty PPM")
    print(f"QMP screendump saved: {output}")


if __name__ == "__main__":
    main()
