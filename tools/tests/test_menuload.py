"""Menu retry site contracts and stock prologue replay; no game data is stored here."""

import struct

import pefile
import pytest
from unicorn.x86_const import UC_X86_REG_EFLAGS

from emu_common import EXE, SITES, needs_exe
from emu_harness import needs_emu
import sitecheck
from sitetable import load


IDS = {
    "MenuLoadUpdate": 0x59DB80,
    "MenuMovieSetup": 0xC167A0,
    "MenuMovieResult": 0xC19335,
    "MenuIndexPublish": 0x49DF80,
    "MenuMovieRequest": 0xC167C7,
}


def menu_sites():
    return {s.id: s for s in load(SITES)[1] if s.id in IDS}


def test_menu_retry_sites_have_full_prologues_and_read_only_request():
    sites = menu_sites()
    assert sites.keys() == IDS.keys()
    for sid in ("MenuLoadUpdate", "MenuMovieSetup"):
        assert sites[sid].kind == "jmp" and sites[sid].length == 6
        assert sites[sid].expect == bytes.fromhex("55 8B EC 83 E4 F8")
    assert sites["MenuMovieResult"].kind == "jmp" and sites["MenuMovieResult"].length == 5
    assert sites["MenuMovieResult"].expect == bytes.fromhex("5F 5E 8B E5 5D")
    assert sites["MenuIndexPublish"].kind == "jmp" and sites["MenuIndexPublish"].length == 8
    assert sites["MenuIndexPublish"].expect[:4] == bytes.fromhex("8D 4D C8 E8")
    assert sites["MenuMovieRequest"].kind == "callref"


@needs_exe
@pytest.mark.parametrize("sid", IDS)
def test_menu_retry_sites_resolve_and_reject_existing_detours(sid):
    pe = pefile.PE(str(EXE), fast_load=True)
    text, va = sitecheck.text_section(pe)
    site = menu_sites()[sid]
    ok, msg, address, target = sitecheck.check_site(text, va, site)
    assert ok, msg
    assert address == IDS[sid]
    if site.kind == "callref":
        assert target == 0xC192A0
    else:
        assert sitecheck.instruction_span_end(text, va, address, site.length) == address + site.length
        assert not sitecheck.inbound_branches(text, va, [(address, address + site.length, sid)])
    patched = bytearray(text)
    # The request's pattern is deliberately anchored at its callee entry, detecting a mod there as well.
    at = (0xC192A0 if site.kind == "callref" else address) - va
    patched[at : at + 5] = bytes.fromhex("E9 00 00 00 00")
    assert not sitecheck.check_site(bytes(patched), va, site)[0]


@needs_emu
@pytest.mark.parametrize("sid", ["MenuLoadUpdate", "MenuMovieSetup", "MenuMovieResult"])
@pytest.mark.parametrize("flags", [0x202, 0x2D7, 0xED7])
@pytest.mark.parametrize("alignment", [0, 4])
def test_menu_retry_noop_mid_hook_preserves_stock_prologue(sid, flags, alignment):
    from emu_harness import ALL_REGS, CODE, DATA, STACK, Emu
    from test_midhook_emu import build

    emu = Emu(False)
    site = menu_sites()[sid]
    address = IDS[sid]
    entry, _ = build(emu, site.expect, source=address)
    esp = STACK + 0x80000 + alignment
    regs = dict(zip(ALL_REGS, [1, 2, 3, 4, DATA + 0x100, DATA + 0x200, STACK + 0x81000, esp]))
    regs[UC_X86_REG_EFLAGS] = flags
    results = []
    for start in (address, entry):
        emu.uc.mem_write(esp - 32, struct.pack("<24I", *range(24)))
        emu.run(start, address + site.length, regs)
        top = emu.uc.reg_read(ALL_REGS[-1])
        # and esp,-8 may leave an undefined gap slot below the saved EBP; compare only what the code defines.
        start_at = max(top, esp - 4)
        results.append((emu.regs(*ALL_REGS), emu.uc.reg_read(UC_X86_REG_EFLAGS), emu.read(start_at, 64)))
    assert results[0] == results[1]
