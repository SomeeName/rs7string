#!/usr/bin/env python3
"""Check the built dinput8.dll: the exports the game and the emulator tests need, the image base, and
that it does not import d3d9.dll (Rsxr_Startup waits for d3d9.dll to appear as the "exe unpacked"
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
# Colour data and stubs the emulator tests (test_emu_*.py) and the in-game log rely on.
REQUIRED_DATA = [
    "g_rsxr_v6",
    "g_rsxr_fx_rgb",
    "g_rsxr_colormgr",
    "Rsxr_S7LogMask",
    "g_rsxr_pegsets",
    "Rsxr_Mid_PP_TOC",
    "Rsxr_Mid_PP_SNGF",
    "Rsxr_Mid_GIP1",
    "Rsxr_Mid_SC1",  # song-camera floor compensation (test_song_camera.py)
    "Rsxr_Stub_PP_SNGD",
    "g_rsxr_skip_PP_TOC",
    "g_rsxr_plain_psarc",
    # mid-hook builders and thunks (test_midhook_emu.py, test_midhook_call_emu.py, emu_harness.py)
    "RsxrMid_Build",
    "RsxrMid_BuildLight",
    "RsxrMid_BuildSse",
    "RsxrMid_BuildCall",
    "RsxrMid_BuildWrap",
    "RsxrMid_BuildCallIn",
    "RsxrMid_Thunk",
    "RsxrMid_ThunkLight",
    "RsxrMid_ThunkSse",
]
FORBIDDEN_IMPORTS = ["d3d9.dll"]
IMAGE_BASE = 0x5E000000


def exports(pe: pefile.PE) -> set[str]:
    table = getattr(pe, "DIRECTORY_ENTRY_EXPORT", None)  # absent: no exports at all, reported as missing
    return {e.name.decode() for e in table.symbols if e.name} if table else set()


def imports(pe: pefile.PE) -> set[str]:
    return {e.dll.decode().lower() for e in getattr(pe, "DIRECTORY_ENTRY_IMPORT", [])}


def problems(pe: pefile.PE) -> list[str]:
    """Every problem with the parsed DLL, one line each (empty: fine)."""
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
    if not path.is_file():
        print(f"check_exports: {path} not found; build the Release configuration first")
        return 1
    pe = pefile.PE(str(path))
    print(
        f"{path.name}: {len(exports(pe))} exports, image base {pe.OPTIONAL_HEADER.ImageBase:#x}, "
        f"imports {', '.join(sorted(imports(pe)))}"
    )
    found = problems(pe)
    for p in found:
        print(p)
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
