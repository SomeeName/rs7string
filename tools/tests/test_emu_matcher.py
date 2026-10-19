"""Native N-slot chord matcher: Rs7Ns_MatchPort against the stock 0x4E7B30, MD1, EV1-EV5, CS1, BT1, LU3."""

from __future__ import annotations

import struct
from contextlib import contextmanager

import pytest
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import (
    UC_X86_REG_EAX,
    UC_X86_REG_EBP,
    UC_X86_REG_EBX,
    UC_X86_REG_ECX,
    UC_X86_REG_EDI,
    UC_X86_REG_EDX,
    UC_X86_REG_EIP,
    UC_X86_REG_ESI,
    UC_X86_REG_ESP,
)

from emu_harness import (
    DATA,
    Emu,
    NS_BEGIN,
    RET_MAGIC,
    S7_EBP,
    S7_MEM,
    STACK,
    needs_emu,
    ns_set,
    run_s7_site,
    site_target,
    stack,
)

pytestmark = needs_emu


# ---- native N-slot chord matcher: the port (Rs7Ns_MatchPort) against the stock 0x4E7B30 ---------

MATCH_FN = 0x4E7B30
MT = DATA + 0x30000  # matcher, input, history, detector, frame, targets


def matcher_setup(
    emu: Emu, peaks: list[tuple[int, float]], level: float = -20.0, bass: bool = False, octave_flag: int = 0
):
    m, inp, hist, det, frames = MT, MT + 0x100, MT + 0x200, MT + 0x2000, MT + 0x4000
    emu.uc.mem_write(m, struct.pack("<II", inp, det) + b"\0" * 0x60)
    emu.uc.mem_write(m + 0x54, bytes([octave_flag]))
    emu.uc.mem_write(inp, struct.pack("<II", 0, hist))
    blob = bytearray(0x1000)
    struct.pack_into("<IIIII", blob, 0xDB8, frames, 1, 2, 0, 2)  # base, index 1, capacity 2, -, available 2
    emu.uc.mem_write(hist, bytes(blob))
    d = bytearray(0x1400)
    struct.pack_into("<I", d, 0x11EC, 2 if bass else 0)
    emu.uc.mem_write(det, bytes(d))
    fr = bytearray(4000)
    for f in (0, 1):  # frame 1 = current (history 0), frame 0 = one back
        base = f * 2000
        struct.pack_into("<f", fr, base + 0x700, level)
        struct.pack_into("<I", fr, base + 0xBC, len(peaks))
        for k, (b, s) in enumerate(peaks):
            struct.pack_into("<If", fr, base + 0x1C + 8 * k, b, s)
    emu.uc.mem_write(frames, bytes(fr))


MATCH_ALLOC = 0x8FB9C0  # the game allocator the stock matcher's vector uses
MATCH_FREES = (0x4E7FB9, 0x4E83B5, 0x4E8574)  # its `call eax` frees (memory manager vfunc +8, 3 args)


@contextmanager
def match_heap(emu: Emu):
    """The stock matcher's heap calls without the game's memory manager (TLS, locks): the allocator
    returns a bump buffer, the frees are skipped."""
    nxt = [MT + 0x10000]

    def alloc(uc, addr, size, user):
        n = uc.reg_read(UC_X86_REG_EAX)
        uc.reg_write(UC_X86_REG_EAX, nxt[0])
        nxt[0] += (n + 15) & ~15
        esp = uc.reg_read(UC_X86_REG_ESP)
        uc.reg_write(UC_X86_REG_EIP, struct.unpack("<I", uc.mem_read(esp, 4))[0])
        uc.reg_write(UC_X86_REG_ESP, esp + 4)

    def free(uc, addr, size, user):
        uc.reg_write(UC_X86_REG_ESP, uc.reg_read(UC_X86_REG_ESP) + 12)
        uc.reg_write(UC_X86_REG_EIP, addr + 2)

    hooks = [emu.uc.hook_add(UC_HOOK_CODE, alloc, begin=MATCH_ALLOC, end=MATCH_ALLOC)]
    hooks += [emu.uc.hook_add(UC_HOOK_CODE, free, begin=a, end=a) for a in MATCH_FREES]
    try:
        yield
    finally:
        for h in hooks:
            emu.uc.hook_del(h)


def run_long(emu: Emu, start: int, esp: int):
    emu.uc.reg_write(UC_X86_REG_ESP, esp)
    emu.uc.reg_write(UC_X86_REG_EBP, STACK + 0x81000)
    emu.uc.emu_start(start, RET_MAGIC, count=3_000_000)
    assert emu.uc.reg_read(UC_X86_REG_EIP) == RET_MAGIC, f"stopped at {emu.uc.reg_read(UC_X86_REG_EIP):#x}"
    return emu.uc.reg_read(UC_X86_REG_EAX) & 0xFF


def stock_match(emu: Emu, targets6: list[int], tol: int = 0, hist: int = 0) -> int:
    tg = MT + 0x6000
    emu.uc.mem_write(tg, struct.pack("<6i", *targets6))
    esp = STACK + 0x80000
    emu.uc.mem_write(esp, struct.pack("<IIIII", RET_MAGIC, MT, tg, tol, hist))
    with match_heap(emu):
        return run_long(emu, MATCH_FN, esp)


def port_match(emu: Emu, targets: list[int], tol: int = 0, hist: int = 0) -> int:
    tg = MT + 0x6000
    t8 = (targets + [-1] * 8)[:8]
    emu.uc.mem_write(tg, struct.pack("<8i", *t8))
    esp = STACK + 0x80000
    emu.uc.mem_write(esp, struct.pack("<IIIiII", RET_MAGIC, MT, tg, 8, tol, hist))
    return run_long(emu, emu.exports["Rs7Ns_MatchPort"], esp)


def harmonics(midi: int, strength: float) -> list[tuple[int, float]]:
    # fundamental and the bins of partials 2-5 (+12, +19, +24, +28), decaying
    return [
        (midi + o, strength * w) for o, w in ((0, 1.0), (12, 0.6), (19, 0.4), (24, 0.3), (28, 0.2)) if midi + o < 128
    ]


def random_case(rng):
    n = rng.randint(1, 6)
    targets = [rng.randint(30, 76) for _ in range(n)]
    if rng.random() < 0.3 and n > 1:
        targets[rng.randrange(n)] = targets[0]  # duplicate
    slots = targets + [-1] * (6 - n)
    rng.shuffle(slots)
    peaks: dict[int, float] = {}
    for t in targets:
        if rng.random() < 0.8:  # played
            for b, s in harmonics(t, rng.uniform(0.2, 3.0)):
                peaks[b] = peaks.get(b, 0.0) + s
    for _ in range(rng.randint(0, 6)):  # noise
        b = rng.randint(20, 100)
        peaks[b] = peaks.get(b, 0.0) + rng.uniform(0.05, 1.5)
    plist = list(peaks.items())[:20]
    return slots, plist, rng.choice([0, 0, 1]), rng.choice([0, 1])


@pytest.mark.parametrize("seed", range(40))
def test_matcher_port_equals_stock_for_six_or_fewer_targets(emus, seed):
    import random

    rng = random.Random(seed)
    stock, patched = emus
    for case in range(10):
        slots, peaks, tol, hist = random_case(rng)
        level = rng.choice([-20.0, -20.0, -60.0]) if case else -20.0
        for emu in (stock, patched):
            matcher_setup(emu, peaks, level=level)
        want = stock_match(stock, slots, tol, hist)
        got = port_match(patched, slots, tol, hist)
        assert got == want, (seed, case, slots, peaks, tol, hist)


@pytest.mark.parametrize(
    "chord", [[40, 47, 52, 55, 59, 64, 35], [40, 47, 52, 55, 59, 64, 35, 30]], ids=["7 notes", "8 notes"]
)
@pytest.mark.parametrize("missing,want", [(0, 1), (1, 1), (2, 1), (3, 0)])
def test_matcher_port_wide_chords_may_miss_two(emus, chord, missing, want):
    """[chords] wide_misses=2 (default): a 7/8-note chord passes with up to two notes missing (any
    string), not with three. The missing notes are ones no other chord note's partials cover."""
    _, patched = emus
    gone = [55, 35, 40][:missing]
    peaks = {}
    for t_ in chord:
        if t_ in gone:
            continue
        for b, s in harmonics(t_, 1.0)[:2]:
            peaks[b] = peaks.get(b, 0.0) + s
    matcher_setup(patched, list(peaks.items()))
    assert port_match(patched, chord) == want


def test_matcher_port_wide_misses_setting(emus):
    _, patched = emus
    chord = [40, 47, 52, 55, 59, 64, 35]
    peaks = {}
    for t_ in chord:
        for b, s in harmonics(t_, 1.0)[:2]:
            peaks[b] = peaks.get(b, 0.0) + s
    del peaks[35]  # the low B's fundamental missing (its octave 47 is also the chord's B2)
    var = patched.exports["g_rs7_match_misses"]
    try:
        patched.uc.mem_write(var, struct.pack("<I", 0))  # every note required
        matcher_setup(patched, list(peaks.items()))
        assert port_match(patched, chord) == 0
        patched.uc.mem_write(var, struct.pack("<I", 1))
        assert port_match(patched, chord) == 1
    finally:
        patched.uc.mem_write(var, struct.pack("<I", 2))
    assert port_match(patched, chord[:6]) == 1


@pytest.mark.parametrize("seed", range(12))
def test_md1_up_to_six_targets_run_the_stock_body_on_the_compacted_array(emus, seed):
    """MD1 at 0x4E7B30 with an int32[8] holding at most six targets (holes anywhere) = stock 0x4E7B30
    with those targets compacted to int32[6]."""
    import random

    rng = random.Random(1000 + seed)
    stock, patched = emus
    for case in range(4):
        slots, peaks, tol, hist = random_case(rng)
        eight = slots + [-1, -1]
        rng.shuffle(eight)
        compact = [x for x in eight if x >= 0] + [-1] * (6 - sum(x >= 0 for x in eight))
        for emu in (stock, patched):
            matcher_setup(emu, peaks)
        want = stock_match(stock, compact, tol, hist)
        tg = MT + 0x6000
        patched.uc.mem_write(tg, struct.pack("<8i", *eight))
        esp = STACK + 0x80000
        patched.uc.mem_write(esp, struct.pack("<IIIII", RET_MAGIC, MT, tg, tol, hist))
        with match_heap(patched):
            got = run_long(patched, MATCH_FN, esp)
        assert got == want, (seed, case, eight, tol, hist)
        assert patched.uc.reg_read(UC_X86_REG_ESP) == esp + 4 + 16  # ret 10h


@pytest.mark.parametrize("sid", ["EV1", "EV2", "EV3", "EV4", "EV5"])
def test_ev_thunks_move_the_eval_targets_to_the_int32_8(emus, sid):
    _, patched = emus
    esp = STACK + 0x80000
    patched.uc.mem_write(esp, struct.pack("<II", 0x11111111, S7_MEM + 0x2C))
    patched.run_to_callee(patched.hook_entry(sid), site_target(sid), {UC_X86_REG_ESP: esp})
    assert patched.uc.reg_read(UC_X86_REG_ESP) == esp
    assert struct.unpack("<II", patched.read(esp, 8)) == (0x11111111, S7_MEM + 0x284)


def test_cs1_clears_the_eval_tails(emus):
    ev = bytearray(0xC28)
    ev[0x284:0x2BC] = bytes(range(1, 0x39))
    stock, ours = run_s7_site(
        emus, "CS1", {UC_X86_REG_EAX: S7_MEM, UC_X86_REG_ECX: 0, UC_X86_REG_EDX: 0xFFFFFFFF}, {S7_MEM: bytes(ev)}
    )
    assert ours[0] == stock[0]
    _, patched = emus
    got = patched.read(S7_MEM, 0xC28)
    assert struct.unpack("<ii", got[0x29C:0x2A4]) == (-1, -1)
    assert got[0x2A4:0x2AC] == bytes(8) and got[0x2AC:0x2B4] == b"\xff" * 8 and got[0x2B4:0x2BC] == bytes(8)
    assert got[0x284:0x29C] == bytes(range(1, 0x19))  # the template copy's midi[6] untouched
    assert got[0xC1C:0xC20] == b"\0\0\xff\xff"


@pytest.mark.parametrize("with_cn", [False, True])
def test_bt1_writes_the_eval_tails_and_returns(emus, with_cn):
    _, patched = emus
    t8, c8, ev = MT + 0x8000, MT + 0x9000, MT + 0xC000
    tpl = bytearray(0x80)
    tpl[0x48:0x50] = bytes([0, 2, 2, 0xFF, 0xFF, 0xFF, 0, 0xFF])
    tpl[0x50:0x58] = bytes([0xFF] * 6 + [1, 0xFF])
    struct.pack_into("<8i", tpl, 0x58, 40, 47, 52, -1, -1, -1, 35, -1)
    cn = bytearray(0x15A8)
    struct.pack_into("<8I", cn, 0x948, *([0] * 6 + [0x2004, 0]))
    cn[0x968:0x978] = b"\xff" * 16
    cn[0x96E] = 5
    patched.uc.mem_write(t8, bytes(tpl))
    patched.uc.mem_write(c8, bytes(cn))
    patched.uc.mem_write(ev, bytes(0xC28))
    ns_set(patched, [(NS_BEGIN, 1, t8, NS_BEGIN + 0x100, 1, c8, 2, 1)])
    ebp = STACK + 0x81000
    # [ebp] saved EBP, [ebp+4] return, [ebp+8] eval, [ebp+14h] template, [ebp+18h] ChordNotes; below: EDI ESI EBX
    patched.uc.mem_write(ebp, struct.pack("<7I", 0xEB0EB0, RET_MAGIC, ev, 0, 0, t8, c8 if with_cn else 0))
    esp = ebp - 12
    patched.uc.mem_write(esp, struct.pack("<III", 0xD1D1, 0x5151, 0xB0B0))
    try:
        patched.run(
            patched.mid_entries["BT1"],  # the mid-hook entry (the relocated epilogue ends in `ret 14h`)
            RET_MAGIC,
            {UC_X86_REG_ESP: esp, UC_X86_REG_EBP: ebp, UC_X86_REG_EBX: ev, UC_X86_REG_EAX: ev},
        )
    finally:
        ns_set(patched, [])
    assert patched.regs(UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP) == (
        ev,
        0xB0B0,
        0x5151,
        0xD1D1,
        0xEB0EB0,
    )
    assert patched.uc.reg_read(UC_X86_REG_ESP) == ebp + 8 + 0x14
    got = patched.read(ev, 0xC28)
    assert struct.unpack("<ii", got[0x29C:0x2A4]) == (35, -1)
    assert got[0x2AC:0x2B0] == bytes([0, 0xFF, 1, 0xFF])
    if with_cn:
        assert struct.unpack("<II", got[0x2A4:0x2AC]) == (0x2004, 0) and got[0x2B0:0x2B4] == bytes(
            [5, 0xFF, 0xFF, 0xFF]
        )
    else:
        assert got[0x2A4:0x2AC] == bytes(8) and got[0x2B0:0x2B4] == b"\xff" * 4


def test_lu3_sets_targets_6_and_7(emus):
    mem = {S7_EBP - 8: stack(0x12345678, 0x9ABCDEF0), S7_EBP + 8: stack(0x4321)}
    stock, ours = run_s7_site(emus, "LU3", {UC_X86_REG_ECX: 0x77, UC_X86_REG_EAX: 0xA0A0}, mem)
    assert ours[0] == stock[0]
    _, patched = emus
    assert struct.unpack("<ii", patched.read(S7_EBP - 8, 8)) == (-1, -1)
    assert struct.unpack("<I", patched.read(S7_EBP - 0x10, 4))[0] == 0x77
