"""Bend stubs for string 6 (BD1-BD4, N18/N19/N20) and the 7th string ghost (N21-N23) without a shown S7."""

from __future__ import annotations

import struct

import pytest
from unicorn.x86_const import (
    UC_X86_REG_EAX,
    UC_X86_REG_EBX,
    UC_X86_REG_ECX,
    UC_X86_REG_EDI,
    UC_X86_REG_EDX,
    UC_X86_REG_ESI,
)

from emu_harness import (
    ALL_REGS,
    DATA,
    Emu,
    S7_EBP,
    S7_MEM,
    S7_REGS,
    TEXT_VA,
    game_text,
    needs_emu,
    run_s7_site,
    site_target,
    site_va,
    stack,
)

pytestmark = needs_emu


# ---- BD1-BD4: perStringMode (bend direction) guards, string 6 -> string 0's mode (issue #14) ----

BEND_MEM = DATA + 0x48000
PSM = [1, 1, 1, 0, 0, 0]  # perStringMode of the menu snapshot guitar (strings 0-2 rotated)


def bend_setup(emu: Emu):
    info = bytearray(0x40)
    for s, m in enumerate(PSM):
        struct.pack_into("<I", info, 0x20 + 4 * s, m)
    struct.pack_into("<I", info, 0x20 + 4 * 6, 0x7777)  # what an unguarded index 6 would read
    guitar = bytearray(0x40)
    struct.pack_into("<I", guitar, 0x24, BEND_MEM + 0x100)
    session = bytearray(0x400)
    struct.pack_into("<I", session, 0x37C, BEND_MEM + 0x200)
    emu.uc.mem_write(BEND_MEM + 0x100, bytes(info))
    emu.uc.mem_write(BEND_MEM + 0x200, bytes(guitar))
    emu.uc.mem_write(BEND_MEM + 0x300, bytes(session))
    emu.uc.mem_write(S7_EBP - 0x18, struct.pack("<I", 0xDEAD))


# sid -> (string register, extra regs, join address of the body, how to read the chosen mode)
BEND_SITES = {
    "BD2": (UC_X86_REG_ECX, {UC_X86_REG_EAX: BEND_MEM + 0x100}, 0x7A664E, lambda e: e.uc.reg_read(UC_X86_REG_ECX)),
    "BD3": (UC_X86_REG_EBX, {UC_X86_REG_EDI: BEND_MEM + 0x300}, 0x7A6D9C, lambda e: e.uc.reg_read(UC_X86_REG_EDI)),
    "BD4": (
        UC_X86_REG_EBX,
        {UC_X86_REG_EAX: BEND_MEM + 0x200},
        0x7A7672,
        lambda e: struct.unpack("<I", e.read(S7_EBP - 0x18, 4))[0],
    ),
}


def run_bend(emu: Emu, sid: str, string: int, until: int):
    bend_setup(emu)
    if sid == "BD1":
        reg, regs = UC_X86_REG_ECX, {UC_X86_REG_EAX: BEND_MEM + 0x100}
    else:
        reg, regs, _, _ = BEND_SITES[sid]
    emu.run(site_va(sid), until, {**S7_REGS, **regs, reg: 0xAB00 | string})
    return emu.regs(*ALL_REGS)


@pytest.mark.parametrize("string", range(7))
@pytest.mark.parametrize("sid", ["BD2", "BD3", "BD4"])
def test_bd_bend_mode(emus, sid, string):
    _, _, join, mode = BEND_SITES[sid]
    stock_e, patched_e = emus
    patched = run_bend(patched_e, sid, string, join)
    m = mode(patched_e)
    if string < 6:
        stock = run_bend(stock_e, sid, string, join)
        assert patched == stock and m == mode(stock_e) == PSM[string]
    else:
        assert m == PSM[0]  # string 0's direction; stock would take the mode-2 branch


@pytest.mark.parametrize("string", [0, 3, 6, 7])
def test_bd1_bend_notation_branch(emus, string):
    """Mode 0 continues at 0x79FEAA, any other mode (and string > 6) jumps to 0x79FEB7."""
    until = 0x79FEAA if string in (3, 4, 5) else 0x79FEB7
    stock_e, patched_e = emus
    patched = run_bend(patched_e, "BD1", string, until)
    if string != 6:
        assert patched == run_bend(stock_e, "BD1", string, until)


# ---- 7th string bend (issue #16): N18 (SetBend guard), N19 (release guard) ----
# Without a shown S7 every string takes the stock continuation with the stock registers. The S7 path
# (the stock SetBend / release on the 7th string) needs the live string object and is checked in game
# ("string7 bend" log lines, tests/shots/bend).


def test_n18_n19_join_offsets():
    """N18 joins at resume + 10h (`cmp dword [ecx+4],0`), N19 at resume + 7 (`cmp byte [eax+0BDh],0FFh`)."""
    t = game_text()
    a = site_va("N18") + 9 + 0x10
    assert t[a - TEXT_VA : a - TEXT_VA + 4] == bytes.fromhex("83 79 04 00")
    b = site_va("N19") + 5 + 7
    assert t[b - TEXT_VA : b - TEXT_VA + 7] == bytes.fromhex("80 B8 BD 00 00 00 FF")
    assert site_target("N18") == site_target("N19") == 0x7E4095
    c = site_va("N20") + 5 + 7
    assert t[c - TEXT_VA : c - TEXT_VA + 7] == bytes.fromhex("80 B8 BD 00 00 00 FF")
    assert site_target("N20") == 0x7E4443


@pytest.mark.parametrize("count", [4, 6])
@pytest.mark.parametrize("string", [0, 3, 5, 6, 7, 0xFF])
@pytest.mark.parametrize("fret", [5, 0xFF])
def test_n18_bend_without_a_7th_string_is_stock(emus, s7_state, string, count, fret):
    info = bytearray(0x40)
    info[0x0B] = count
    va = site_va("N18")
    until = va + 9 if string < count else site_target("N18")
    out = []
    for emu in emus:
        emu.uc.mem_write(S7_MEM + 0x400, bytes(info))
        emu.uc.mem_write(S7_EBP - 0x64, struct.pack("<f", 0.5))
        emu.run(
            va,
            until,
            {
                **S7_REGS,
                UC_X86_REG_EAX: 0xAB00 | string,
                UC_X86_REG_EDX: 0xCD00 | fret,
                UC_X86_REG_ESI: S7_MEM + 0x400,
                UC_X86_REG_ECX: S7_MEM + 0x180,
            },
        )
        out.append((emu.regs(*ALL_REGS), emu.fpu_top()))
    assert out[1] == out[0]


@pytest.mark.parametrize("sid", ["N19", "N20"])
@pytest.mark.parametrize("count", [4, 6])
@pytest.mark.parametrize("string", [0, 3, 5, 6, 7])
def test_n19_n20_release_without_a_7th_string_is_stock(emus, s7_state, sid, string, count):
    info = bytearray(0x40)
    info[0x0B] = count
    va = site_va(sid)
    until = va + 5 if string < count else site_target(sid)
    out = []
    for emu in emus:
        emu.uc.mem_write(S7_MEM + 0x400, bytes(info))
        emu.run(
            va,
            until,
            {
                **S7_REGS,
                UC_X86_REG_EAX: 0xAB00 | string,
                UC_X86_REG_EDX: S7_MEM + 0x400,
                UC_X86_REG_ECX: S7_MEM + 0x180,
            },
        )
        out.append((emu.regs(*ALL_REGS), emu.fpu_top()))
    assert out[1] == out[0]


# ---- the 7th string's ghost (issue #16): N21 (type-2 request count), N22/N23 (ghost bend guards) ----


@pytest.mark.parametrize("count", [4, 6])
def test_n21_ghost_request_count(emus, s7_state, count):
    profile = bytearray(0x40)
    profile[0x38] = count
    stock, patched = run_s7_site(
        emus, "N21", {UC_X86_REG_ECX: S7_MEM, UC_X86_REG_EAX: 0xA0A0}, {S7_MEM: bytes(profile)}
    )
    on = s7_state[0]
    assert stock[0][2] == count
    if count == 6 and on:  # one spare ghost instance
        assert patched[0][2] == 7 and patched[0][:2] == stock[0][:2] and patched[0][3:] == stock[0][3:]
        assert patched[2:] == stock[2:]
    else:
        assert patched == stock
    assert stock[3][:8] == stack(1, 0xA0A0)  # push eax; push 1


def test_n22_n23_join_offsets():
    t = game_text()
    a = site_va("N22") + 5 + 0xC
    assert t[a - TEXT_VA : a - TEXT_VA + 4] == bytes.fromhex("83 79 04 00")
    b = site_va("N23") + 5 + 7
    assert t[b - TEXT_VA : b - TEXT_VA + 7] == bytes.fromhex("80 B9 BD 00 00 00 FF")
    assert (site_target("N22"), site_target("N23")) == (0x7E3F14, 0x7E4373)


@pytest.mark.parametrize("sid", ["N22", "N23"])
@pytest.mark.parametrize("count", [4, 6])
@pytest.mark.parametrize("string", [0, 3, 5, 6, 7])
def test_n22_n23_ghost_without_a_7th_string_is_stock(emus, s7_state, sid, string, count):
    info = bytearray(0x40)
    info[0x0B] = count
    va = site_va(sid)
    until = va + 5 if string < count else site_target(sid)
    reg = (
        {UC_X86_REG_EAX: 0xAB00 | string, UC_X86_REG_EDX: 0xCD05, UC_X86_REG_ECX: S7_MEM + 0x180}
        if sid == "N22"
        else {UC_X86_REG_ECX: 0xAB00 | string, UC_X86_REG_EDX: S7_MEM + 0x180}
    )
    out = []
    for emu in emus:
        emu.uc.mem_write(S7_MEM + 0x400, bytes(info))
        emu.uc.mem_write(S7_EBP - 0x60, struct.pack("<f", 0.75))
        emu.run(va, until, {**S7_REGS, **reg, UC_X86_REG_ESI: S7_MEM + 0x400})
        out.append((emu.regs(*ALL_REGS), emu.fpu_top()))
    assert out[1] == out[0]
