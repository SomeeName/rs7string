"""Drive rs7string's in-game overlay in the background (no focus, no real input).

    uv run python tools/rsoverlay.py show | hide | toggle | save | status
    uv run python tools/rsoverlay.py tab neck
    uv run python tools/rsoverlay.py set neck.string7_v 0.5
    uv run python tools/rsoverlay.py raw "o set color.string6_color FF00FF" "o tab colour"

Each command is one line "o ..." on rs7string's DirectInput pipe \\\\.\\pipe\\rs7string (needs
rs7string.ini [window] background=1 and [overlay] enabled=1); the overlay runs it at the next frame
and logs "overlay: cmd ... -> result" in rs7string.log. Screenshots: scripts/rsdrive.py shot.
"""

from __future__ import annotations

import sys
import time

PIPE = "\\\\.\\pipe\\rs7string"
VERBS = {"show", "hide", "toggle", "save", "status"}


def lines_for(argv: list[str]) -> list[str]:
    """Command-line words -> pipe lines (raises ValueError for anything the overlay would reject)."""
    if not argv:
        raise ValueError("no command")
    verb, rest = argv[0].lower(), argv[1:]
    if verb in VERBS and not rest:
        return [f"o {verb}"]
    if verb == "tab" and len(rest) == 1:
        return [f"o tab {rest[0]}"]
    if verb == "set" and len(rest) >= 2:
        return [f"o set {rest[0]} {' '.join(rest[1:])}"]
    if verb == "raw" and rest:
        for line in rest:
            if not line.startswith("o ") or "\n" in line:
                raise ValueError(f"not an overlay line: {line!r}")
        return list(rest)
    raise ValueError(f"cannot send {' '.join(argv)!r}")


def send(lines: list[str], gap: float = 0.2) -> None:
    with open(PIPE, "w", newline="\n") as p:
        for line in lines:
            p.write(line + "\n")
            p.flush()
            time.sleep(gap)


def main(argv: list[str]) -> int:
    try:
        lines = lines_for(argv)
    except ValueError as e:
        print(e, file=sys.stderr)
        print(__doc__, file=sys.stderr)
        return 2
    send(lines)
    for line in lines:
        print("sent", line)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
