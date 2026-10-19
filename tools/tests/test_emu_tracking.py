"""Native N-slot chord tracking: child queries AG1-AG3 and the loop-latch predicates PD2-PD5 against stock."""

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

from emu_harness import DATA, Emu, RET_MAGIC, STACK, needs_emu

pytestmark = needs_emu


# ---- native N-slot chord tracking: the child queries AG1-AG3 against stock 0x875780/0x875A10/0x875C90

AGG = {"AG1": 0x875780, "AG2": 0x875A10, "AG3": 0x875C90}
TRK = DATA + 0x60000  # tracker: evals at +8, 0xC28 each


def agg_setup(emu: Emu, rng, whole: int, s67=(0xFF, 0xFF)):
    evals = bytearray(8 + 12 * 0xC28)
    parent = 8 + 10 * 0xC28
    evals[parent + 0xC1C] = 1
    evals[parent + 0xC1D] = whole
    for k in (0x54, 0x55, 0x56, 0x64, 0x74, 0x75, 0x76, 0x84):
        evals[parent + k] = rng.choice([0, 1, 1, 3])
    for s in range(6):
        c = rng.choice([-1, -1, s, s + 4])
        struct.pack_into("<i", evals, parent + 0xC04 + 4 * s, c)
    evals[parent + 0xC1E], evals[parent + 0xC1F] = s67
    for c in range(10):
        e = 8 + c * 0xC28
        for k in (0x54, 0x55, 0x56, 0x74, 0x75, 0x76):
            evals[e + k] = rng.choice([0, 1, 1, 2, 3])
        evals[e + 0x64] = rng.choice([0, 0, 1])
        evals[e + 0x84] = rng.choice([0, 0, 1])
    emu.uc.mem_write(TRK, bytes(evals))
    emu.uc.mem_write(TRK + 0x10000, bytes([rng.randint(0, 7)]))  # string for the string query
    return TRK + parent


def run_agg(emu: Emu, fn: int, eval_: int, string_query: bool) -> int:
    esp = STACK + 0x80000
    emu.uc.mem_write(esp, struct.pack("<I", RET_MAGIC))
    emu.run(
        fn,
        RET_MAGIC,
        {
            UC_X86_REG_ESP: esp,
            UC_X86_REG_ESI: eval_,
            UC_X86_REG_EDI: TRK,
            UC_X86_REG_EDX: TRK + 0x10000 if string_query else 0,
            UC_X86_REG_EAX: 0x12345600,
        },
    )
    return emu.uc.reg_read(UC_X86_REG_EAX)


@pytest.mark.parametrize("sid", sorted(AGG))
@pytest.mark.parametrize("seed", range(25))
def test_ag_child_queries_equal_stock_for_strings_0_to_5(emus, sid, seed):
    import random

    rng = random.Random(seed * 7 + int(sid[-1]))
    stock, patched = emus
    for case in range(6):
        whole = 1 if case == 0 else 0
        string_query = sid != "AG1" and case % 2 == 1
        for emu in (stock, patched):
            st = rng.getstate()
            ev = agg_setup(emu, rng, whole)
        rng.setstate(st)
        ev = agg_setup(stock, rng, whole)
        if string_query and stock.read(TRK + 0x10000, 1)[0] >= 6:
            continue  # stock answers 0 for strings 6/7 by its guard; ours reads their children
        want = run_agg(stock, AGG[sid], ev, string_query)
        got = run_agg(patched, AGG[sid], ev, string_query)
        assert got == want, (sid, seed, case)


def test_ag_children_of_strings_6_and_7_take_part(emus):
    import random

    rng = random.Random(5)
    _, patched = emus
    ev = agg_setup(patched, rng, 0, s67=(2, 0xFF))
    for s in range(6):
        patched.uc.mem_write(ev + 0xC04 + 4 * s, struct.pack("<i", -1))
    child = TRK + 8 + 2 * 0xC28
    patched.uc.mem_write(child + 0x64, b"\x01")
    patched.uc.mem_write(child + 0x84, b"\x00")
    patched.uc.mem_write(child + 0x56, b"\x00")
    assert run_agg(patched, AGG["AG1"], ev, False) & 0xFF == 0  # string 6's child failed
    patched.uc.mem_write(child + 0x56, b"\x01")
    assert run_agg(patched, AGG["AG1"], ev, False) & 0xFF == 1


# ---- loop-latch predicates PD2-PD5: stock for strings 0-5, strings 6/7 from the eval tails ------

PRED = {0x877D20: (UC_X86_REG_EDX, 0x800), 0x877D60: (UC_X86_REG_ECX, 0x401810), 0x877DD0: (UC_X86_REG_EDX, 0x10)}
EVP = DATA + 0x70000


def pred_eval(emu: Emu, rng, tails: bool):
    ev = bytearray(0xC28)
    for s in range(6):
        struct.pack_into("<i", ev, 0x284 + 4 * s, rng.choice([-1, 40 + s]))
        struct.pack_into("<I", ev, 0x2BC + 4 * s, rng.choice([0, 0x800, 0x10, 0x1000, 0x400000, 0x810]))
        ev[0x278 + s] = rng.choice([0xFF, 0, 3, 5])
        ev[0xBEC + s] = rng.choice([0xFF, 7, 2])
    if tails:
        struct.pack_into("<ii", ev, 0x29C, rng.choice([-1, 35]), -1)
        struct.pack_into("<II", ev, 0x2A4, rng.choice([0, 0x800, 0x10]), 0)
        ev[0x2AC:0x2B4] = bytes([rng.choice([0xFF, 2]), 0xFF, 0xFF, 0xFF, rng.choice([0xFF, 6]), 0xFF, 0xFF, 0xFF])
    else:
        struct.pack_into("<ii", ev, 0x29C, -1, -1)
        ev[0x2A4:0x2AC] = bytes(8)
        ev[0x2AC:0x2B4] = b"\xff" * 8
    emu.uc.mem_write(EVP, bytes(ev))
    return ev


def run_pred(emu: Emu, fn: int, reg: int) -> int:
    esp = STACK + 0x80000
    emu.uc.mem_write(esp, struct.pack("<I", RET_MAGIC))
    emu.run(
        fn,
        RET_MAGIC,
        {UC_X86_REG_ESP: esp, reg: EVP, UC_X86_REG_EAX: 0x55555500, UC_X86_REG_EBX: 0x0B0B, UC_X86_REG_ESI: 0x5151},
    )
    return emu.uc.reg_read(UC_X86_REG_EAX)


@pytest.mark.parametrize("fn", sorted(PRED) + [0x877E10, 0x877CF0])
@pytest.mark.parametrize("seed", range(20))
def test_pd_predicates_without_string_6_are_stock(emus, fn, seed):
    import random

    stock, patched = emus
    reg = PRED.get(fn, (UC_X86_REG_ECX, 0))[0]
    rng = random.Random(seed)
    st = rng.getstate()
    pred_eval(stock, rng, False)
    rng.setstate(st)
    pred_eval(patched, rng, False)
    assert run_pred(patched, fn, reg) == run_pred(stock, fn, reg)


def test_pd_string_6_takes_part(emus):
    _, patched = emus
    import random

    ev = bytearray(0xC28)
    struct.pack_into("<8i", ev, 0x284, 40, -1, -1, -1, -1, -1, 35, -1)
    struct.pack_into("<I", ev, 0x2BC, 0x800)
    struct.pack_into("<II", ev, 0x2A4, 0, 0)
    ev[0x278:0x27E] = bytes([0xFF] * 6)
    ev[0x2AC:0x2B4] = b"\xff" * 8
    patched.uc.mem_write(EVP, bytes(ev))
    assert run_pred(patched, 0x877D20, UC_X86_REG_EDX) & 0xFF == 0  # string 6 sounds without a slide
    patched.uc.mem_write(EVP + 0x2A4, struct.pack("<I", 0x800))
    assert run_pred(patched, 0x877D20, UC_X86_REG_EDX) & 0xFF == 1
    patched.uc.mem_write(EVP + 0x284, struct.pack("<i", -1))
    assert run_pred(patched, 0x877CF0, UC_X86_REG_ECX) == 35  # first sounding MIDI: string 6's
