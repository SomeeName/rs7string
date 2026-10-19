"""Native N-slot chord data: template / ChordNotes redirects (TP*, CP*), TestStringMask (TM1-TM6) and DS1."""

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

from emu_harness import ALL_REGS, NS_BEGIN, NS_SUPER, S7_ESP, S7_MEM, needs_emu, ns_set, run_s7_site, stack

pytestmark = needs_emu


# ---- native N-slot chord data (issue #18 stage 2): template / ChordNotes pointer redirects -------

# site: (id register, stock vector begin register, destination register)
NS_TP = {
    "TP1": (UC_X86_REG_EDI, UC_X86_REG_ESI, UC_X86_REG_EAX),
    "TP2": (UC_X86_REG_EDI, UC_X86_REG_EBX, UC_X86_REG_EDI),
    "TP3": (UC_X86_REG_EBX, UC_X86_REG_EDI, UC_X86_REG_EBX),
    "TP4": (UC_X86_REG_EDI, UC_X86_REG_EBX, UC_X86_REG_EDX),
    "TP5": (UC_X86_REG_EDI, UC_X86_REG_ECX, UC_X86_REG_EDI),
    "TP6": (UC_X86_REG_EDI, UC_X86_REG_EBX, UC_X86_REG_EDX),
    "TP7": (UC_X86_REG_ESI, UC_X86_REG_EBX, UC_X86_REG_EBX),
    "TP8": (UC_X86_REG_EDI, UC_X86_REG_EBX, UC_X86_REG_EDX),
    "TP11": (UC_X86_REG_ECX, UC_X86_REG_ESI, UC_X86_REG_EDX),
}
NS_CP = {
    "CP1": (UC_X86_REG_EAX, UC_X86_REG_ESI, UC_X86_REG_EAX),
    "CP2": (UC_X86_REG_ESI, UC_X86_REG_EBX, UC_X86_REG_EBX),
}


def run_ns(emus, sid: str, idx: int):
    reg_idx, reg_base, _ = {**NS_TP, **NS_CP}[sid]
    regs = {UC_X86_REG_EAX: 0xA0A0, reg_base: NS_BEGIN, reg_idx: idx}
    return run_s7_site(emus, sid, regs, {})


@pytest.mark.parametrize("sid", sorted({**NS_TP, **NS_CP}))
def test_ns_redirect_without_a_registered_song_is_stock(emus, sid):
    _, patched = emus
    ns_set(patched, [(0x1000, 50, 0x5000, 0x2000, 50, 0x6000, 1, 50)])  # another song only
    try:
        stock, ours = run_ns(emus, sid, 3)
    finally:
        ns_set(patched, [])
    assert ours == stock


@pytest.mark.parametrize("sid", sorted(NS_TP))
@pytest.mark.parametrize("idx,inside", [(0, True), (3, True), (9, True), (10, False)])
def test_ns_template_redirect_returns_the_t8_record(emus, sid, idx, inside):
    _, patched = emus
    ns_set(patched, [(0x1000, 50, 0x5000, 0, 0, 0, 1, 0), (NS_BEGIN, 10, NS_SUPER, 0, 0, 0, 2, 0)])
    try:
        stock, ours = run_ns(emus, sid, idx)
    finally:
        ns_set(patched, [])
    dst = ALL_REGS.index(NS_TP[sid][2])
    assert stock[0][dst] == NS_BEGIN + idx * 0x48
    want = list(stock[0])
    if inside:
        want[dst] = NS_SUPER + idx * 0x80
    assert list(ours[0]) == want and ours[2:] == stock[2:]


@pytest.mark.parametrize("sid", sorted(NS_CP))
@pytest.mark.parametrize("idx,inside", [(0, True), (4, True), (5, False)])
def test_ns_chordnotes_redirect_returns_the_c8_record(emus, sid, idx, inside):
    _, patched = emus
    ns_set(patched, [(NS_BEGIN, 10, NS_SUPER, NS_BEGIN, 5, NS_SUPER + 0x100000, 2, 5)])
    try:
        stock, ours = run_ns(emus, sid, idx)
    finally:
        ns_set(patched, [])
    dst = ALL_REGS.index(NS_CP[sid][2])
    assert stock[0][dst] == NS_BEGIN + idx * 0x948
    want = list(stock[0])
    if inside:
        want[dst] = NS_SUPER + 0x100000 + idx * 0x15A8
    assert list(ours[0]) == want and ours[2:] == stock[2:]


# ---- native N-slot chord rendering: TM1-TM6 (TestStringMask over C8 mask[8]), DS1 (double-stop count)


def tm_mem(masks8: list[int]) -> dict[int, bytes]:
    """One record readable both ways: stock mask[6] at +0, C8 mask[8] at +948h."""
    return {
        S7_MEM: struct.pack("<6I", *masks8[:6]),
        S7_MEM + 0x948: struct.pack("<8I", *masks8),
        S7_ESP: stack(0x2000),
    }  # the flag the caller pushed


@pytest.mark.parametrize("sid", [f"TM{i}" for i in range(1, 7)])
@pytest.mark.parametrize("string", [0, 3, 5, 6, 7, 8])
def test_tm_test_string_mask_reads_c8(emus, sid, string):
    masks = [0x2000, 0, 0x2004, 0x1000, 0x2000, 0, 0x2004, 0x2000]
    stock, ours = run_s7_site(emus, sid, {UC_X86_REG_ECX: S7_MEM, UC_X86_REG_EAX: 0xA0A00 | string}, tm_mem(masks))
    eax = ALL_REGS.index(UC_X86_REG_EAX)
    want = 1 if string < 8 and masks[string] & 0x2000 else 0
    assert ours[0][eax] & 0xFF == want
    if string < 6:
        assert stock[0][eax] & 0xFF == want
    esp = ALL_REGS.index(UC_X86_REG_ESP)
    assert ours[0][esp] == stock[0][esp] == S7_ESP + 4


@pytest.mark.parametrize("idx", [0, 2])
def test_ds1_double_stop_count_reads_t8_frets(emus, idx):
    _, patched = emus
    ns_set(patched, [(NS_BEGIN, 4, NS_SUPER, 0, 0, 0, 2, 0)])
    try:
        stock, ours = run_s7_site(emus, "DS1", {UC_X86_REG_ESI: NS_BEGIN, UC_X86_REG_ECX: idx}, {})
    finally:
        ns_set(patched, [])
    r = {k: ALL_REGS.index(k) for k in (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX)}
    assert (stock[0][r[UC_X86_REG_EAX]], stock[0][r[UC_X86_REG_ECX]], stock[0][r[UC_X86_REG_EDX]]) == (
        NS_BEGIN + idx * 0x48 + 4,
        6,
        0,
    )
    assert (ours[0][r[UC_X86_REG_EAX]], ours[0][r[UC_X86_REG_ECX]], ours[0][r[UC_X86_REG_EDX]]) == (
        NS_SUPER + idx * 0x80 + 0x48,
        7,
        0,
    )
