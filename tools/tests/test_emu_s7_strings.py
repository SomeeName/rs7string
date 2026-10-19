"""Seventh-string line requests, per-guitar update, SNG string mask (N12/N13) and fret bar (F1) without a 7-string guitar."""

from __future__ import annotations

import struct

import pytest
from unicorn.x86_const import (
    UC_X86_REG_EAX,
    UC_X86_REG_EBX,
    UC_X86_REG_ECX,
    UC_X86_REG_EDI,
    UC_X86_REG_ESI,
    UC_X86_REG_ESP,
)

from emu_harness import (
    ALL_REGS,
    DATA,
    Emu,
    RET_MAGIC,
    S7_ESP,
    S7_MEM,
    S7_REGS,
    needs_emu,
    run_s7_site,
    set_s7,
    site_target,
    site_va,
    stack,
)

pytestmark = needs_emu


@pytest.mark.parametrize("count", [4, 6])
def test_n1_string_line_request_count(emus, s7_state, count):
    profile = bytearray(0x40)
    profile[0x38] = count
    stock, patched = run_s7_site(emus, "N1", {UC_X86_REG_EAX: S7_MEM}, {S7_MEM: bytes(profile)})
    on = s7_state[0]
    assert stock[0][0] == count
    if count == 6 and on:  # the one intended difference: a spare 7th instance
        assert patched[0][0] == 7 and patched[0][1:] == stock[0][1:] and patched[2:] == stock[2:]
    else:
        assert patched == stock
    assert stock[3][:8] == stack(1, 0x0C0C)  # push ecx; push 1


@pytest.mark.parametrize("sid,reg,idx", [("N1", UC_X86_REG_EAX, 0), ("N21", UC_X86_REG_ECX, 2)])
@pytest.mark.parametrize("ports", [0, 1])
@pytest.mark.parametrize("count", [4, 6, 7, 8])
def test_n1_n21_request_capacity_with_publish_ports(emus, sid, reg, idx, ports, count):
    """native_publish ports: profile counts 6 and 7 ([neck] clite builds 7) request the 8-slot capacity
    (a pool of 7 left string slot 7 without an entity: detail 15, the guitar never finished loading)."""
    _, patched = emus
    addr = patched.exports["g_rs7_publish_ports"]
    saved = patched.read(addr, 4)
    patched.uc.mem_write(addr, struct.pack("<I", ports))
    try:
        set_s7(patched, 1, 0)
        profile = bytearray(0x40)
        profile[0x38] = count
        regs = {reg: S7_MEM} if sid == "N1" else {UC_X86_REG_ECX: S7_MEM, UC_X86_REG_EAX: 0xA0A0}
        stock, out = run_s7_site(emus, sid, regs, {S7_MEM: bytes(profile)})
        want = 8 if ports and count in (6, 7) else 7 if count == 6 else count
        assert stock[0][idx] == count and out[0][idx] == want
    finally:
        patched.uc.mem_write(addr, saved)


@pytest.mark.parametrize("next_ptr", [0, S7_MEM + 0x600], ids=["je", "jne"])
@pytest.mark.parametrize("gs", [0, S7_MEM + 0x180, S7_MEM + 0x700], ids=["no-strings", "recorded", "unknown"])
def test_n5_update_without_a_7th_string_is_stock(emus, s7_state, gs, next_ptr):
    guitar = bytearray(0x40)
    struct.pack_into("<I", guitar, 0x0C, gs)
    struct.pack_into("<I", guitar, 0x18, next_ptr)
    stock, patched = run_s7_site(
        emus, "N5", {UC_X86_REG_EBX: S7_MEM + 0x500}, {S7_MEM + 0x500: bytes(guitar)}, flags=True
    )
    assert patched == stock
    assert stock[0][0] == next_ptr


# ---- 7th string highlight: N12/N13 (SNG string mask) ----


def run_mask_site(emu: Emu, sid: str, until: int, regs: dict[int, int]):
    emu.run(site_va(sid), until, {**S7_REGS, UC_X86_REG_EAX: 0xA0A0, **regs})
    return emu.regs(*ALL_REGS), emu.fpu_top()


@pytest.mark.parametrize("string", list(range(8)) + [0xFF])
def test_n12_string_mask_forward(emus, s7_state, string):
    """0x55CC71 (`cmp cl,6; jae skip` in PostProcessNotes): strings 0-5 and > 6 as stock; string 6
    goes on to the stock mask write (va + 5) only with [neck] string7_line on."""
    stock, patched = emus
    va, skip = site_va("N12"), site_target("N12")
    regs = {UC_X86_REG_ECX: 0x1200 | string}
    want_stock = va + 5 if string < 6 else skip
    want_patched = va + 5 if string < 6 or (string == 6 and s7_state[0]) else skip
    a = run_mask_site(stock, "N12", want_stock, regs)
    b = run_mask_site(patched, "N12", want_patched, regs)
    assert a == b


SONG = DATA + 0xD000  # fake SNG song: sections vector at +0F4h/+0F8h
SECTIONS = DATA + 0xD200


def write_song(emu: Emu, masks: list[int], level: int):
    sec = bytearray(0x58 * len(masks))
    for i, m in enumerate(masks):
        sec[i * 0x58 + 0x34 + level] = m
    emu.uc.mem_write(SECTIONS, bytes(sec))
    song = bytearray(0x100)
    struct.pack_into("<II", song, 0xF4, SECTIONS, SECTIONS + len(sec))
    emu.uc.mem_write(SONG, bytes(song))


@pytest.mark.parametrize(
    "masks, filled",
    [
        ([0x01, 0x00, 0x05, 0x00], [0x01, 0x00, 0x05, 0x00]),  # stock song: no bit 6
        ([0x00, 0x00, 0x40, 0x00, 0x40], [0x00, 0x40, 0x40, 0x40, 0x40]),  # string 6 lit one section early
        ([0x40, 0x00, 0x00], [0x40, 0x00, 0x00]),
    ],
    ids=["stock", "two", "first"],
)
@pytest.mark.parametrize("string", [0, 3, 5, 6])
def test_n13_string_mask_backfill(emus, s7_state, masks, filled, string):
    """0x55CE3A (bound of the back-fill loop, CL already incremented): CL < 6 loops on (target); after
    string 5 (CL = 6) the stock continuation va + 9, and the patched DLL first back-fills bit 6 for
    level EDI. Registers and FPU depth as stock."""
    stock, patched = emus
    va = site_va("N13")
    level = 3
    until = site_target("N13") if string < 6 else va + 9
    regs = {UC_X86_REG_ECX: 0x1300 | string, UC_X86_REG_ESI: SONG, UC_X86_REG_EDI: level}
    out = []
    for emu in emus:
        write_song(emu, masks, level)
        out.append(
            (
                run_mask_site(emu, "N13", until, regs),
                [emu.read(SECTIONS + i * 0x58 + 0x34 + level, 1)[0] for i in range(len(masks))],
            )
        )
    assert out[1][0] == out[0][0]
    assert out[0][1] == masks
    on = s7_state[0]
    assert out[1][1] == (filled if string == 6 and on else masks)


FRETS_MEM = DATA + 0x20000  # guitar / info / FretGrid for the F1 test (the grid is 0x72A0 bytes)


def set_frets(emu: Emu, on: int):
    emu.uc.mem_write(emu.exports["g_rs7_frets_on"], struct.pack("<I", on))


@pytest.mark.parametrize(
    "on,built",
    [(1, 0), (1, 4), (1, 6), (1, 5), (1, 9), (0, 0), (0, 7), (0, 8)],
    ids=["fret_extend-unknown", "bass", "six", "five", "nine", "off", "off-7", "off-8"],
)
def test_f1_fret_bar_transforms_without_a_7_string_guitar_are_stock(emus, on, built):
    """No guitar was built with 7/8 strings (info +0Bh; every 6-string / bass guitar, or fret_extend off): F1
    runs the stock body of 0x58FB20 as a subroutine and Rs7_FretsAfterApply returns at once. Zero fret bars
    (info +0Dh = 0) keep the stock body away from the game's entity setters; it still runs its prologue, the
    count check and the epilogue, so registers, stack and memory must match stock after the `ret`."""
    va = site_va("F1")
    guitar, info, grid = FRETS_MEM, FRETS_MEM + 0x100, FRETS_MEM + 0x1000
    g = bytearray(0x40)
    struct.pack_into("<I", g, 0x10, grid)
    struct.pack_into("<I", g, 0x24, info)
    inf = bytearray(0x40)
    inf[0x0B] = built
    mem = {
        guitar: bytes(g),
        info: bytes(inf),
        grid: stack(guitar) + bytes(0x7300 - 4),
        S7_ESP: stack(RET_MAGIC, 0x5A5A5A5A),
    }
    _, patched = emus
    set_frets(patched, on)
    out = []
    try:
        for emu in emus:
            for a, b in mem.items():
                emu.uc.mem_write(a, b)
            emu.run(va, RET_MAGIC, {**S7_REGS, UC_X86_REG_EAX: grid, UC_X86_REG_ECX: guitar})
            esp = emu.uc.reg_read(UC_X86_REG_ESP)
            out.append(
                (
                    emu.regs(*ALL_REGS),
                    emu.fpu_top(),
                    emu.read(esp - 0x10, 0x20),
                    {a: emu.read(a, len(b)) for a, b in mem.items() if a != S7_ESP},
                )
            )
    finally:
        set_frets(patched, 1)
    stock, patched_out = out
    assert patched_out[0] == stock[0] and patched_out[1] == stock[1] and patched_out[3] == stock[3]
    assert stock[0][0] == guitar and stock[0][7] == S7_ESP + 4  # EAX = [grid]; ret popped RET_MAGIC
