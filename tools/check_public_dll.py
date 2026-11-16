#!/usr/bin/env python3
"""Release guard: the public DLL must not contain the private code. Scans the DLL bytes (ASCII and UTF-16LE) for the
markers listed in tools/private_extras.py (DLL_MARKERS). That file is private-only, so without it the check
is skipped with a message (exit 0). Usage: check_public_dll.py [dll]"""

from __future__ import annotations

import sys
from pathlib import Path

DLL = Path(__file__).resolve().parents[1] / "bin" / "Release" / "dinput8.dll"


def find_markers(data: bytes, markers) -> list[str]:
    """The markers present in data, as ASCII or UTF-16LE."""
    found = []
    for m in markers:
        if m.encode("ascii") in data or m.encode("utf-16-le") in data:
            found.append(m)
    return found


def load_markers():
    try:
        import private_extras
    except ModuleNotFoundError as e:
        if e.name != "private_extras":
            raise
        return None
    return tuple(getattr(private_extras, "DLL_MARKERS", ()))


def main(argv: list[str]) -> int:
    dll = Path(argv[1]) if len(argv) > 1 else DLL
    markers = load_markers()
    if markers is None:
        print("check_public_dll: SKIPPED (tools/private_extras.py is not present in this tree)")
        return 0
    if not markers:
        print("check_public_dll: FAILED: private_extras.DLL_MARKERS is empty, nothing would be checked")
        return 1
    found = find_markers(dll.read_bytes(), markers)
    if found:
        print(f"check_public_dll: FAILED: {dll} contains private-only strings: {', '.join(found)}")
        print("Build the shipped DLL with build.ps1 -Public.")
        return 1
    print(f"check_public_dll: OK ({len(markers)} markers absent from {dll.name})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
