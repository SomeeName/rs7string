"""Tone wrappers preserve stock results without an active extended session.

The real callbacks run with no session (timeline/registration) or force=false (refresh), avoiding the
loader's runtime-constructed manifest cache. Owned stand-ins isolate the game callees' stack ABI.
"""

import struct

import pytest
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_EFLAGS, UC_X86_REG_ESI, UC_X86_REG_ESP

from emu_harness import RET_MAGIC, S7_ESP, S7_MEM, S7_REGS, needs_emu, run_s7_site, site_target, site_va, stack

pytestmark = needs_emu


@pytest.mark.parametrize("sid", ["ToneAuto", "ToneSetup", "ToneSeek", "ToneResume", "ToneOverride"])
@pytest.mark.parametrize("raw,result", [(0, 0xA1B2C300), (3, 0xA1B2C301), (5, 0xA1B2C301)])
def test_timeline_without_session_preserves_stock_result(emus, sid, raw, result):
    # The lookup writes the output ID and returns a bool in AL; upper EAX bits must survive too.
    callee = site_target(sid)
    code = bytes.fromhex("8B 44 24 08 C7 00") + struct.pack("<I", raw)
    code += b"\xb8" + struct.pack("<I", result) + bytes.fromhex("C2 08 00")
    saved = [emu.read(callee, len(code)) for emu in emus]
    try:
        for emu in emus:
            emu.uc.mem_write(callee, code)
            emu.uc.ctl_remove_cache(callee, callee + len(code))  # the previous case's stub may still be translated
        stock, ours = run_s7_site(
            emus,
            sid,
            {UC_X86_REG_ESI: 0, UC_X86_REG_EBX: 0, UC_X86_REG_EFLAGS: 0x2D7},
            {S7_ESP: stack(0x3F800000, S7_MEM) + bytes(0x40), S7_MEM: stack(0xFFFFFFFF)},
            flags=True,
        )
        assert ours == stock
        for emu in emus:
            assert emu.uc.reg_read(UC_X86_REG_EAX) == result
            assert emu.uc.reg_read(UC_X86_REG_ESP) == S7_ESP + 8
            assert emu.read(S7_MEM, 4) == stack(raw)
    finally:
        for emu, original in zip(emus, saved):
            emu.uc.mem_write(callee, original)
            emu.uc.ctl_remove_cache(callee, callee + len(original))


@pytest.mark.parametrize("sid,args", [("ToneRefresh", (S7_MEM, 0)), ("ToneRegister", (0,))])
def test_inactive_entry_wrap_preserves_stock_result(emus, sid, args):
    # Replay the real prologue, then an owned epilogue. The arguments keep each callback inactive.
    resume = site_va(sid) + 6
    pops = 4 * len(args)
    # TEST gives both runs the same outgoing flags despite their different internal stack addresses.
    code = bytes.fromhex("B8 78 56 34 12 85 C0 8B E5 5D C2") + struct.pack("<H", pops)
    saved = [emu.read(resume, len(code)) for emu in emus]
    states = []
    try:
        for emu in emus:
            emu.uc.mem_write(resume, code)
            emu.uc.mem_write(S7_ESP, stack(RET_MAGIC, *args) + bytes(0x40))
            emu.run(site_va(sid), RET_MAGIC, {**S7_REGS, UC_X86_REG_EFLAGS: 0x2D7})
            states.append((emu.regs(*S7_REGS), emu.uc.reg_read(UC_X86_REG_EFLAGS), emu.read(S7_ESP + 4 + pops, 0x20)))
            assert emu.uc.reg_read(UC_X86_REG_EAX) == 0x12345678
            assert emu.uc.reg_read(UC_X86_REG_ESP) == S7_ESP + 4 + pops
        assert states[0] == states[1]
    finally:
        for emu, original in zip(emus, saved):
            emu.uc.mem_write(resume, original)


def test_stock_toneplan_builder_wrap_preserves_arguments_result_and_stack(emus):
    callee = site_target("TonePlanBuild")
    # Read the page argument; Attributes is null so the real before callback keeps the builder stock.
    code = bytes.fromhex("8B 44 24 04 85 C0 C2 08 00")
    saved = [emu.read(callee, len(code)) for emu in emus]
    try:
        for emu in emus:
            emu.uc.mem_write(callee, code)
            emu.uc.ctl_remove_cache(callee, callee + len(code))
        stock, ours = run_s7_site(
            emus,
            "TonePlanBuild",
            {UC_X86_REG_EFLAGS: 0x2D7},
            {S7_ESP: stack(0x12345678, 0) + bytes(0x100)},
            flags=True,
        )
        assert ours == stock
        for emu in emus:
            assert emu.uc.reg_read(UC_X86_REG_EAX) == 0x12345678
            assert emu.uc.reg_read(UC_X86_REG_ESP) == S7_ESP + 8
    finally:
        for emu, original in zip(emus, saved):
            emu.uc.mem_write(callee, original)
            emu.uc.ctl_remove_cache(callee, callee + len(original))


@pytest.mark.parametrize("sid", ["TonePlanField", "TonePlanExtension", "TonePlanBound", "TonePlanEdit"])
def test_inactive_toneplan_sites_replay_stock_instructions(emus, sid):
    stock, ours = run_s7_site(
        emus,
        sid,
        {UC_X86_REG_EAX: 5, UC_X86_REG_EFLAGS: 0x2D7},  # the bound takes its fallthrough
        {S7_ESP: bytes(0x300)},  # an empty selection string keeps the edit logger inactive
        flags=True,
    )
    assert ours == stock
