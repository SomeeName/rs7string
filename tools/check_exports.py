#!/usr/bin/env python3
"""Check the built dinput8.dll: the exports the game and the emulator tests need, the image base, and
that it does not import d3d9.dll (Rs7_Startup waits for d3d9.dll to appear as the "exe unpacked"
signal; an import would load it before the wait and the DLL would talk to D3D9 outside COM vtables)."""

from __future__ import annotations

import sys
from pathlib import Path

import pefile

DLL = Path(__file__).resolve().parents[1] / "bin" / "Release" / "dinput8.dll"
REQUIRED = [
    "DirectInput8Create",
    "DllCanUnloadNow",
    "DllGetClassObject",
    "DllRegisterServer",
    "DllUnregisterServer",
    "GetdfDIJoystick",
]
# Colour data and stubs the emulator tests (test_emu.py) and the in-game log rely on.
REQUIRED_DATA = [
    "g_rs7_v6",
    "g_rs7_fx_rgb",
    "g_rs7_colormgr",
    "Rs7_S7LogMask",
    "g_rs7_pegsets",
    "Rs7_Stub_PP_TOC",
    "Rs7_Stub_PP_SNGF",
    "Rs7_Stub_PP_SNGD",
    "g_rs7_skip_PP_TOC",
    "g_rs7_plain_psarc",
]
FORBIDDEN_IMPORTS = ["d3d9.dll"]
IMAGE_BASE = 0x5E000000


def exports(pe: pefile.PE) -> set[str]:
    return {e.name.decode() for e in pe.DIRECTORY_ENTRY_EXPORT.symbols if e.name}


def imports(pe: pefile.PE) -> set[str]:
    return {e.dll.decode().lower() for e in getattr(pe, "DIRECTORY_ENTRY_IMPORT", [])}


def problems(path: Path) -> list[str]:
    """Every problem with the DLL at `path`, one line each (empty: fine)."""
    pe = pefile.PE(str(path))
    have, deps = exports(pe), imports(pe)
    out = []
    missing = [n for n in REQUIRED + REQUIRED_DATA if n not in have]
    if missing:
        out.append("missing: " + ", ".join(missing))
    if pe.OPTIONAL_HEADER.ImageBase != IMAGE_BASE:
        out.append(f"image base is not {IMAGE_BASE:#x}")
    for dll in FORBIDDEN_IMPORTS:
        if dll in deps:
            out.append(f"imports {dll} (must not: it would load d3d9 before the unpack wait)")
    return out


def main(argv: list[str]) -> int:
    path = Path(argv[0]) if argv else DLL
    pe = pefile.PE(str(path), fast_load=False)
    print(
        f"{path.name}: {len(exports(pe))} exports, image base {pe.OPTIONAL_HEADER.ImageBase:#x}, "
        f"imports {', '.join(sorted(imports(pe)))}"
    )
    found = problems(path)
    for p in found:
        print(p)
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
