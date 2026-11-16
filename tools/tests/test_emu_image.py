"""The built DLL image: stub exports are direct (no incremental-link thunks) and the colour-manager reference is bound."""

from __future__ import annotations

import struct

import pefile

from emu_harness import DLL, needs_emu

pytestmark = needs_emu


def test_exports_are_direct(emus):
    """No incremental-link thunks: every stub export is the stub itself (no stub starts with a jmp),
    and the image has no ILT (the linker's thunks are `jmp rel32` runs at the start of .text)."""
    _, patched = emus
    stubs = {n: a for n, a in patched.exports.items() if n.startswith("Rs7_Stub_")}
    # Stub exports besides the PUB* family. Update the count when a stub is added or removed (history: git log).
    assert len([s for s in stubs if not s.startswith("Rs7_Stub_PUB")]) == 6
    for name, addr in stubs.items():
        assert patched.read(addr, 1)[0] != 0xE9, f"{name} is an ILT thunk"
    pe = pefile.PE(str(DLL), fast_load=True)
    text = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
    assert text.get_data()[:5][0] != 0xE9 or text.get_data()[5] != 0xE9, ".text starts with an ILT"


# ---- colour manager (the X_CM reference; the stock-colour fallback C1-C5 / N11 went in 1.0) -----

MGR_PTR = 0x0135F58C


def test_colour_manager_ref_is_bound(emus):
    _, patched = emus
    assert struct.unpack("<I", patched.read(patched.exports["g_rs7_colormgr"], 4))[0] == MGR_PTR
