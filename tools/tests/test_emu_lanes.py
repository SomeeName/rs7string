"""String-6 lane hooks (R2-R6) and the diagnostic stubs (D2, D3, D5): stock for strings 0-5, derived lane for string 6."""

from __future__ import annotations

import struct

import pytest
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import (
    UC_X86_REG_EAX,
    UC_X86_REG_EBP,
    UC_X86_REG_EBX,
    UC_X86_REG_ECX,
    UC_X86_REG_EDI,
    UC_X86_REG_EDX,
    UC_X86_REG_EFLAGS,
    UC_X86_REG_ESI,
    UC_X86_REG_ESP,
)

from emu_harness import ALL_REGS, DATA, Emu, FLAGS_MASK, RET_MAGIC, STACK, needs_emu, site_va

pytestmark = needs_emu


# ---- lane hooks: 2 * cell0 - cell1 for string 6, stock otherwise ---------------------------

GRID = DATA + 0x1000
CELL0 = (10.0, 20.0, 30.0)
CELL1 = (7.0, 16.0, 25.0)  # one string further in: lane 6 = (13, 24, 35)


def write_row(emu: Emu, row: int):
    """Six cells of 0x4C bytes from `row`; worldPts[1] (x,y,z) at cell + 0x3C."""
    for s in range(6):
        vals = CELL0 if s == 0 else CELL1 if s == 1 else (1.0 + s, 2.0 + s, 3.0 + s)
        emu.uc.mem_write(row + s * 0x4C + 0x3C, struct.pack("<3f", *vals))
    emu.uc.mem_write(row + 6 * 0x4C + 0x3C, struct.pack("<3f", -99.0, -99.0, -99.0))  # stock string-6 read


def run_r2(emu: Emu, string: int):
    va = site_va("R2")
    write_row(emu, GRID)
    ebp = STACK + 0x90000
    emu.uc.mem_write(ebp + 0x1C, struct.pack("<I", GRID - 8))  # grid base; EDX row offset below
    emu.uc.mem_write(ebp - 0x30, struct.pack("<f", -4.99))  # floor z (read by D1)
    emu.uc.mem_write(ebp + 0x10, struct.pack("<I", 0))  # open flag (D1)
    emu.uc.mem_write(ebp + 0x14, struct.pack("<I", string))  # string (D1)
    emu.uc.mem_write(ebp + 0x18, struct.pack("<I", 7))  # fret (D1)
    emu.run(
        va,
        va + 7,
        {
            UC_X86_REG_EBP: ebp,
            UC_X86_REG_EDX: 8,
            UC_X86_REG_EBX: string * 0x4C,
            UC_X86_REG_EAX: 0xA1A1,
            UC_X86_REG_ECX: 0xC1C1,
            UC_X86_REG_ESI: 0x5151,
            UC_X86_REG_EDI: 0xD1D1,
            UC_X86_REG_ESP: STACK + 0x80000,
            UC_X86_REG_EFLAGS: 0x202,
        },
    )
    top = emu.fpu_top()
    return emu.st0(), emu.regs(*ALL_REGS), emu.regs(UC_X86_REG_EFLAGS)[0] & FLAGS_MASK, top


@pytest.mark.parametrize("string", range(7))
def test_r2_note_head_lane(emus, diag, string):
    stock, patched = emus
    results = [run_r2(emu, string) for emu in (stock, patched)]
    if string < 6:
        assert results[1] == results[0]
    else:
        assert results[1][0] == pytest.approx(2 * CELL0[2] - CELL1[2])
        assert results[1][1:] == results[0][1:]


@pytest.mark.parametrize("sid,length", [("R3a", 6), ("R3b", 6), ("R3c", 6), ("R4", 7)])
@pytest.mark.parametrize("string", range(7))
def test_r3_r4_notefx_and_feedback_position(emus, sid, length, string):
    stock, patched = emus
    va = site_va(sid)
    results = []
    for emu in (stock, patched):
        write_row(emu, GRID)
        esp = STACK + 0x80000
        emu.uc.mem_write(esp, struct.pack("<I", 0x12345678))  # value for R4's pop esi
        emu.run(
            va,
            va + length,
            {
                UC_X86_REG_EAX: GRID,
                UC_X86_REG_ECX: string * 0x4C,
                UC_X86_REG_ESP: esp,
                UC_X86_REG_ESI: 0,
                UC_X86_REG_EFLAGS: 0x246,
            },
        )
        vec = struct.unpack("<3f", emu.read(emu.uc.reg_read(UC_X86_REG_EAX), 12))
        top = emu.fpu_top()
        results.append(
            (
                vec,
                emu.st0(),
                emu.regs(UC_X86_REG_ESI, UC_X86_REG_ESP)[0:2],
                emu.regs(UC_X86_REG_EFLAGS)[0] & FLAGS_MASK,
                top,
            )
        )
    if string < 6:
        assert results[1] == results[0]
    else:
        lane = tuple(2 * a - b for a, b in zip(CELL0, CELL1))
        assert results[1][0] == pytest.approx(lane)
        assert results[1][1] == pytest.approx(lane[0])
        assert results[1][2:] == results[0][2:]


@pytest.mark.parametrize("string", range(7))
def test_r6_lane_offset(emus, string):
    stock, patched = emus
    va = site_va("R6")
    results = []
    for emu in (stock, patched):
        write_row(emu, GRID)
        emu.run(
            va,
            va + 6,
            {
                UC_X86_REG_EAX: GRID - 0x10,
                UC_X86_REG_EDI: 0x10,
                UC_X86_REG_ECX: string * 0x4C,
                UC_X86_REG_EFLAGS: 0x202,
            },
        )
        results.append((emu.st0(), emu.regs(UC_X86_REG_EAX)[0], emu.regs(UC_X86_REG_EFLAGS)[0] & FLAGS_MASK))
    if string < 6:
        assert results[1] == results[0]
    else:
        assert results[1][0] == pytest.approx(2 * CELL0[1] - CELL1[1])
        assert results[1][1:] == results[0][1:]


# ---- [debug] diagnostics switch: R2 (D1) calls its logger only when it is 1 ----


@pytest.mark.parametrize("string", [0, 3, 6])
def test_r2_logger_calls_follow_the_diagnostics_switch(emus, diag, string):
    _, patched = emus
    want = 1 if diag and string in (0, 6) else 0
    logger = patched.exports["Rs7_DiagNoteHead"]  # R2's D1 logger (a `ret` in the emulator)
    hits = []
    hook = patched.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, u: hits.append(a), begin=logger, end=logger)
    try:
        run_r2(patched, string)
    finally:
        patched.uc.hook_del(hook)
    assert len(hits) == want


# ---- diagnostics D2, D3, D5: the stubs must be invisible to the game -----------------------


@pytest.mark.parametrize("string", [0, 3, 6, 0xFF])
def test_d2_gem_create_entry_is_transparent(emus, string):
    stock, patched = emus
    va = site_va("D2")
    esp = STACK + 0x80000
    frame = struct.pack(
        "<7I",
        RET_MAGIC,
        0x0BADF00D,
        0x22222222,
        7,
        0xABCDEF00 | string,
        struct.unpack("<I", struct.pack("<f", 12.5))[0],
        2,
    )
    results = []
    for emu in (stock, patched):
        emu.uc.mem_write(esp, frame)
        emu.run(
            va,
            va + 5,
            {
                UC_X86_REG_ESP: esp,
                UC_X86_REG_EAX: 0x569A10B6,
                UC_X86_REG_ECX: 0xC1C1,
                UC_X86_REG_EDX: 0xD0D0,
                UC_X86_REG_EBX: 0xB1B1,
                UC_X86_REG_ESI: 0x5151,
                UC_X86_REG_EDI: 0xD1D1,
                UC_X86_REG_EBP: 0x1111,
                UC_X86_REG_EFLAGS: 0x8D7,
            },
        )
        top = emu.uc.reg_read(UC_X86_REG_ESP)
        results.append(
            (emu.regs(*ALL_REGS), emu.regs(UC_X86_REG_EFLAGS)[0] & FLAGS_MASK, emu.fpu_top(), emu.read(top, 0x28))
        )
    assert results[1] == results[0]


@pytest.mark.parametrize("mode", [0, 2, 4, 5])
@pytest.mark.parametrize("string", [0, 3, 6])
def test_d3_geom_build_mode_switch_is_transparent(emus, string, mode):
    stock, patched = emus
    va = site_va("D3")
    geom, gem = DATA + 0x5000, DATA + 0x5100
    results = []
    for emu in (stock, patched):
        emu.uc.mem_write(gem, bytes(0x0C) + bytes([string, 7, 0, 0]) + struct.pack("<I", 2) + bytes(0x6C))
        emu.uc.mem_write(geom, bytes(8) + struct.pack("<I", gem) + bytes(4) + struct.pack("<II", 1, mode) + bytes(0x30))
        emu.run(
            va,
            va + 6,
            {
                UC_X86_REG_ESP: STACK + 0x80000,
                UC_X86_REG_EDI: geom,
                UC_X86_REG_EAX: 0xA1A1,
                UC_X86_REG_ECX: 0xC1C1,
                UC_X86_REG_EDX: 0xD0D0,
                UC_X86_REG_EBX: 0xB1B1,
                UC_X86_REG_ESI: 0x5151,
                UC_X86_REG_EBP: 0x1111,
                UC_X86_REG_EFLAGS: 0x8D7,
            },
        )
        results.append((emu.regs(*ALL_REGS), emu.regs(UC_X86_REG_EFLAGS)[0] & FLAGS_MASK, emu.fpu_top()))
    assert results[1] == results[0]
    assert results[0][0][0] == mode


@pytest.mark.parametrize("string", [0, 3, 6, 0xFF])
def test_d5_gem_position_join_is_transparent(emus, string):
    stock, patched = emus
    va = site_va("D5")
    gem = DATA + 0x6000
    esp = STACK + 0x80000
    results = []
    for emu in (stock, patched):
        emu.uc.mem_write(
            gem, bytes(0x0C) + bytes([string, 7, 0, 0]) + struct.pack("<I", 2) + struct.pack("<3f", 0.5, 1.5, 2.5)
        )
        emu.uc.mem_write(esp, struct.pack("<10I", *range(0x100, 0x10A)))
        emu.uc.mem_write(esp + 0x1C, struct.pack("<3f", 10.0, 20.0, 30.0))
        emu.run(
            va,
            va + 7,
            {
                UC_X86_REG_ESP: esp,
                UC_X86_REG_EBX: gem,
                UC_X86_REG_EAX: 0xA1A1,
                UC_X86_REG_ECX: 0xC1C1,
                UC_X86_REG_EDX: 0xD0D0,
                UC_X86_REG_ESI: 0x5151,
                UC_X86_REG_EDI: 0xD1D1,
                UC_X86_REG_EBP: 0x1111,
                UC_X86_REG_EFLAGS: 0x8D7,
            },
        )
        top = emu.fpu_top()
        results.append(
            (emu.st0(), emu.regs(*ALL_REGS), emu.regs(UC_X86_REG_EFLAGS)[0] & FLAGS_MASK, top, emu.read(esp, 0x28))
        )
    assert results[1] == results[0]
    assert results[0][0] == 0.5
    assert results[0][1][0] == 0x104  # EAX = [esp+10h]
