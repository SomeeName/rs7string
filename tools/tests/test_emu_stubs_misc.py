"""Small stubs: nif handler AL1, virtual guitarist, tuning names (TJ/TU/TUT/TN), song-end log (SE1-SE3), XB1."""

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
    UC_X86_REG_ESP,
)

from emu_harness import (
    ALL_REGS,
    Emu,
    RET_MAGIC,
    S7_EBP,
    S7_ESP,
    S7_MEM,
    S7_REGS,
    entry_calls,
    needs_emu,
    run_s7_site,
    set_u32,
    site_va,
    stack,
)

pytestmark = needs_emu


# ---- AL1: entry of the nif memory handler 0x4A8560 (edited headstock model) ----------------


def set_asset(emu: Emu, on: int):
    emu.uc.mem_write(emu.exports["g_rs7_asset_on"], struct.pack("<I", on))


def test_al1_off_replays_the_stock_prologue(emus):
    """[assets] headstock7=0: `push ebp; mov ebp,esp; mov eax,[0x135FBFC]` exactly as stock, back at
    0x4A8568 with the same registers and stack."""
    _, patched = emus
    set_asset(patched, 0)
    args = stack(RET_MAGIC, S7_MEM, S7_MEM + 0x100, 0x200, 0)
    stock, ours = run_s7_site(emus, "AL1", {}, {S7_ESP: args})
    assert ours == stock


def test_al1_on_tail_jumps_into_the_c_wrapper(emus):
    """headstock7=1: the stub leaves the stack untouched and jumps to Rs7_NifHandler (cdecl, same
    arguments); Rs7_NifHandlerStock is the same replayed prologue as the off path."""
    _, patched = emus
    set_asset(patched, 1)
    try:
        esp = S7_ESP
        patched.uc.mem_write(esp, stack(RET_MAGIC, S7_MEM, S7_MEM + 0x100, 0x200, 0))
        va = site_va("AL1")
        patched.run(va, patched.exports["Rs7_NifHandler"], {**S7_REGS, UC_X86_REG_ESP: esp})
        assert patched.uc.reg_read(UC_X86_REG_ESP) == esp
        assert patched.read(esp, 4) == stack(RET_MAGIC)
    finally:
        set_asset(patched, 0)
    stock, _ = emus
    for emu, start in ((stock, site_va("AL1")), (patched, patched.exports["Rs7_NifHandlerStock"])):
        emu.uc.mem_write(S7_ESP, stack(RET_MAGIC, S7_MEM, S7_MEM + 0x100, 0x200, 0))
        emu.run(start, site_va("AL1") + 8, {**S7_REGS})
    assert patched.regs(*ALL_REGS) == stock.regs(*ALL_REGS)


# ---- Virtual guitarist stubs (TICK, JUD1, JUD2, AUD1; [test] vguitar=1 only, features/vguit/vguit.h)
VG_SITES = {  # site -> (C entry, registers, flags live after the site)
    "TICK": ("Rs7Vg_OnTick", {}, False),
    "JUD1": ("Rs7Vg_OnJudge", {UC_X86_REG_EBX: S7_MEM, UC_X86_REG_ESI: S7_MEM + 0x400}, False),
    "JUD2": ("Rs7Vg_OnScore", {UC_X86_REG_ECX: 0x1201, UC_X86_REG_EDX: S7_MEM + 0x400}, False),
    "AUD1": ("Rs7Vg_OnInput", {UC_X86_REG_ESI: S7_MEM, UC_X86_REG_EBX: S7_MEM + 0x200}, True),
}


def set_vg(emu: Emu, on: int):
    emu.uc.mem_write(emu.exports["g_rs7_vg_on"], struct.pack("<I", on))


def vg_live(sid: str, emus, ours, stock):
    """TICK / JUD2 are function entries: `and esp,-40h` points ESP below the caller's stack, at bytes
    the function has not written yet (whatever earlier runs left there): dead. Their live stack is the
    pushed EBP and the caller's frame above it; that is checked here and the dead bytes are dropped."""
    if sid not in ("TICK", "JUD2"):
        return ours, stock
    for emu in emus:
        assert emu.read(S7_ESP - 4, 4) == stack(S7_EBP)
        assert emu.read(S7_ESP, 12) == vg_mem()[S7_ESP][:12]
    return ours[:3] + ours[4:], stock[:3] + stock[4:]


def vg_mem():
    return {S7_ESP: stack(RET_MAGIC, S7_MEM + 0x400, 0x41700000, 0), S7_MEM: bytes(0x100), S7_MEM + 0x200: bytes(0x20)}


@pytest.mark.parametrize("sid", list(VG_SITES))
def test_vg_stub_off_replays_stock(emus, sid):
    _, patched = emus
    set_vg(patched, 0)
    entry, regs, flags = VG_SITES[sid]
    with entry_calls(patched, entry) as calls:
        stock, ours = run_s7_site(emus, sid, regs, vg_mem(), flags=flags)
    ours, stock = vg_live(sid, emus, ours, stock)
    assert ours == stock
    assert calls == []


@pytest.mark.parametrize("sid", list(VG_SITES))
def test_vg_stub_on_calls_in_and_is_transparent(emus, sid):
    """vguitar=1: one call with the documented arguments; registers, flags, FPU top, stack and the
    replayed instructions' effects equal stock."""
    _, patched = emus
    set_vg(patched, 1)
    entry, regs, flags = VG_SITES[sid]
    try:
        with entry_calls(patched, entry) as calls:
            stock, ours = run_s7_site(emus, sid, regs, vg_mem(), flags=flags)
    finally:
        set_vg(patched, 0)
    ours, stock = vg_live(sid, emus, ours, stock)
    assert ours == stock
    assert len(calls) == 1
    esp, args = calls[0]
    assert (esp + 4) % 16 in (0, 4, 8, 12)  # any; the stack below holds the 16-byte aligned fxsave area
    want = {
        "TICK": (S7_MEM + 0x400, 0x41700000),  # session, float 15.0
        "JUD1": (S7_MEM, S7_MEM + 0x400),  # note (EBX), session (ESI)
        "JUD2": (1, S7_MEM + 0x400, S7_MEM + 0x400),  # CL, EDX session, [esp+4] note
        "AUD1": (S7_MEM, S7_MEM + 0x200),
    }[sid]  # device (ESI), AkAudioBuffer (EBX)
    assert args[: len(want)] == want


# ---- TJ / TU / TUT / TN: seven-string tuning names (issue #15) ---------------------------------
# The C side (JSON reads, game string calls, GetCurrentThreadId) needs the game and Win32; it is
# covered by tests/tuner/test_tuningname.cpp (the name table) and in game. Here: TUT's stock path; TJ / TU / TN are
# Wrap mid-hooks, compared with their frozen stubs and the stock bytes in test_midhook_wrap_sites.py.


def set_tn(emu: Emu, on: int):
    set_u32(emu, "g_rs7_tn_on", on)


def test_tut_off_replays_the_three_instructions(emus):
    """names=0: `mov edx,[edi]; mov eax,[edx]; mov ecx,edi` (the Tuning object's AddRef setup)."""
    _, patched = emus
    set_tn(patched, 0)
    obj = stack(S7_MEM + 0x40) + bytes(0x3C) + stack(0xABCD1234)
    stock, ours = run_s7_site(emus, "TUT", {UC_X86_REG_EDI: S7_MEM}, {S7_MEM: obj})
    assert ours == stock
    assert stock[0][:3] == (0xABCD1234, 0x0B0B, S7_MEM)  # EAX, EBX, ECX


# ---- SE1 / SE2 / SE3: song end log (issue #13, features/songend/songend.h; log only) -------------------------
SE_SITES = {
    "SE1": (1, {UC_X86_REG_ECX: S7_MEM + 0x400}),  # OnEndSongEvent: ECX session
    "SE2": (2, {UC_X86_REG_ESI: S7_MEM + 0x400}),  # leave-state: ESI session
    "SE3": (3, {UC_X86_REG_ESI: S7_MEM + 0x400}),
}  # UpdateState, song +78h NULL: ESI session


def set_endlog(emu: Emu, on: int):
    emu.uc.mem_write(emu.exports["g_rs7_endlog"], struct.pack("<I", on))


def se_mem():
    # [esp] return, [esp+4] event / state; session: vtable -> +0B0h a marker the SE3 replay loads
    return {
        S7_ESP: stack(RET_MAGIC, S7_MEM + 0x300, 3, 0),
        S7_MEM + 0x400: stack(S7_MEM + 0x200) + bytes(0x40),
        S7_MEM + 0x200: bytes(0xB0) + stack(0x00C0FFEE),
    }


def se_live(sid: str, ours, stock):
    """SE2 replays `and esp,-8`: the bytes at the new ESP may lie below the caller's stack (dead)."""
    return (ours[:3] + ours[4:], stock[:3] + stock[4:]) if sid == "SE2" else (ours, stock)


@pytest.mark.parametrize("sid", list(SE_SITES))
def test_se_stub_off_replays_stock(emus, sid):
    _, patched = emus
    set_endlog(patched, 0)
    try:
        with entry_calls(patched, "Rs7End_OnContext") as calls:
            stock, ours = run_s7_site(emus, sid, SE_SITES[sid][1], se_mem())
    finally:
        set_endlog(patched, 1)
    ours, stock = se_live(sid, ours, stock)
    assert ours == stock
    assert calls == []


@pytest.mark.parametrize("sid", list(SE_SITES))
def test_se_stub_on_calls_in_and_is_transparent(emus, sid):
    """song_end_log=1 (default): one call Rs7End_OnContext(site, context); the context points to the
    original game stack; registers, FPU top and memory equal stock."""
    _, patched = emus
    set_endlog(patched, 1)
    site, regs = SE_SITES[sid]
    with entry_calls(patched, "Rs7End_OnContext") as calls:
        stock, ours = run_s7_site(emus, sid, regs, se_mem())
    ours, stock = se_live(sid, ours, stock)
    assert ours == stock
    assert len(calls) == 1
    args = calls[0][1]
    assert args[0] == site
    assert struct.unpack("<I", patched.read(args[1] + 12, 4))[0] == S7_ESP
    if sid == "SE3":
        assert stock[0][0] == 0x00C0FFEE and stock[0][3] == S7_MEM + 0x200  # EAX, EDX of the replay


# ---- XB1: RS7X extension block at the end of SNG_Song_Load (issue #18, core/extblock.h) -----------
def test_xb1_hands_over_song_and_stream_then_replays_the_destructor(emus):
    """One call Rs7Ext_OnSngLoaded(song = [EBP+8], stream = site ESP+14h), then the stock
    `lea ecx,[esp+14h]; call 0x9D8B10` (the stream's destructor; +20h set = buffer not owned, so it
    only rewrites the vtables): registers, flags, FPU top and memory equal stock."""
    _, patched = emus
    stream = bytearray(0x24)
    stream[0x10:0x1C] = stack(S7_MEM + 0x600, 0x40, 0x48)  # buffer, position, size
    stream[0x20] = 1
    mem = {
        S7_ESP: stack(0x11, 0x22, 0x33, 0x44, 0x55) + bytes(stream) + stack(0x66, 0x77),
        S7_EBP + 8: stack(S7_MEM + 0x400),
    }
    with entry_calls(patched, "Rs7Ext_OnSngLoaded") as calls:
        stock, ours = run_s7_site(emus, "XB1", {}, mem, flags=True)
    assert ours == stock
    assert len(calls) == 1 and calls[0][1][:2] == (S7_MEM + 0x400, S7_ESP + 0x14)
    assert stock[4][S7_ESP][0x14:0x18] == stack(0x011DD9A8)  # the destructor ran (base vtable)
