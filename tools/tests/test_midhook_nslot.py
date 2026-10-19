"""Old-vs-new equivalence of the native N-slot chord sites converted from naked stubs to C++ mid-hooks (refactor
step 7, batch 3; stubs frozen at b8c0ed7, tools/tests/fixtures/midhook_b8c0ed7.json): the template / ChordNotes
redirects (features/nslot/hooks_records.cpp), the eval sites (hooks_eval.cpp) and the technique-check latches
(hooks_latches.cpp); and the first call site, SF1 (hooks_free.cpp, frozen at 44135a2, midhook_44135a2.json). What is
compared: midhook_oracle.py. Every case runs under 3 incoming EFLAGS patterns and 2 stack alignments.
"""

from __future__ import annotations

import itertools
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

from emu_harness import DATA, DLL, SNAPSHOT, site_va
from midhook_oracle import Case, Helper, Spec, Sym, oracle_tests

pytestmark = pytest.mark.skipif(
    not DLL.exists() or not (SNAPSHOT / "memory.bin").exists(), reason="needs built DLL and snapshot"
)

D = DATA + 0x2000  # scratch objects (page-aligned, zeroed per run)
JUNK = 0x5A5A5A00
SPECS: dict[str, tuple[Spec, list[Case]]] = {}


def register(spec: Spec, cases: list[Case]):
    SPECS[spec.sid] = (spec, cases)


def blob(n: int, seed: int) -> bytes:
    return bytes((i * 31 + seed) & 255 for i in range(n))


def dwords(*values: int) -> bytes:
    return struct.pack(f"<{len(values)}I", *(v & 0xFFFFFFFF for v in values))


# ---- TP* / CP* / DS1: the registry g_rs7_ns (64 entries of 32 bytes) -------------------------------------------
TPL, CN, T8, C8 = 0x11110000, 0x22220000, 0x33330000, 0x44440000


def registry(slot: int, count: int) -> bytes:
    """Entry `slot` registers TPL (count templates) and CN (count ChordNotes); entry 2 an unrelated song."""
    raw = bytearray(64 * 32)
    raw[slot * 32 : slot * 32 + 32] = dwords(TPL, count, T8, CN, count, C8, 0x5000, count)
    raw[2 * 32 : 2 * 32 + 32] = dwords(TPL + 0x100, 50, T8 + 0x10000, CN + 0x100, 50, C8 + 0x10000, 0x5100, 50)
    return bytes(raw)


REGISTRIES = {"empty": (0, 0), "first": (0, 10), "last": (63, 10)}
IDS = (0, 9, 10, 0xFFFFFFFF)


def lookup_cases(begin_reg, id_reg, base):
    return [
        Case(
            f"{name}-begin{b - base:x}-id{i:x}",
            regs={begin_reg: b, id_reg: i},
            mem={Sym("g_rs7_ns"): registry(*REGISTRIES[name])},
        )
        for name, b, i in itertools.product(REGISTRIES, (base, base + 0x100, 0), IDS)
        if not (name == "empty" and b == base + 0x100)
    ]


TP_SITES = {  # sid -> (displaced bytes, begin register, id register)
    "TP1": (6, UC_X86_REG_ESI, UC_X86_REG_EDI),
    "TP2": (6, UC_X86_REG_EBX, UC_X86_REG_EDI),
    "TP3": (6, UC_X86_REG_EDI, UC_X86_REG_EBX),
    "TP4": (6, UC_X86_REG_EBX, UC_X86_REG_EDI),
    "TP5": (6, UC_X86_REG_ECX, UC_X86_REG_EDI),
    "TP6": (6, UC_X86_REG_EBX, UC_X86_REG_EDI),
    "TP7": (8, UC_X86_REG_EBX, UC_X86_REG_ESI),
    "TP8": (6, UC_X86_REG_EBX, UC_X86_REG_EDI),
    "TP11": (6, UC_X86_REG_ESI, UC_X86_REG_ECX),
    "TP12": (7, UC_X86_REG_EDI, UC_X86_REG_EBX),
    "DS1": (12, UC_X86_REG_ESI, UC_X86_REG_ECX),
}


def registered(case: Case, begin_reg, id_reg) -> bool:
    """The lookup finds a T8 record: the case's begin/id pair is in the registry (entry 2: TPL + 100h, 50 templates)."""
    begin, id_ = case.regs[begin_reg], case.regs[id_reg]
    return (begin == TPL and id_ < 10 and not case.id.startswith("empty")) or (begin == TPL + 0x100 and id_ < 50)


# The DS1 cases the old-stub comparison drops (case ids of lookup_cases): every begin/id pair the registry does not
# hold. Spelled out so a change of the registry layout or the case ids cannot shrink the coverage silently.
DS1_UNREGISTERED = frozenset(
    [f"empty-begin{b}-id{i}" for b in ("-11110000", "0") for i in ("0", "9", "a", "ffffffff")]
    + [
        f"{name}-begin{b}-id{i}"
        for name in ("first", "last")
        for b, i in (("0", "a"), ("0", "ffffffff"), ("100", "ffffffff"))
        + tuple(("-11110000", i) for i in ("0", "9", "a", "ffffffff"))
    ]
)


def test_ds1_drops_exactly_the_unregistered_cases():
    _, begin_reg, id_reg = TP_SITES["DS1"]
    cases = lookup_cases(begin_reg, id_reg, TPL)
    dropped = {c.id for c in cases if not registered(c, begin_reg, id_reg)}
    assert dropped == DS1_UNREGISTERED, sorted(dropped ^ DS1_UNREGISTERED)
    assert {c.id for c in SPECS["DS1"][1]} == {c.id for c in cases} - DS1_UNREGISTERED


for sid, (length, begin_reg, id_reg) in TP_SITES.items():
    cases = lookup_cases(begin_reg, id_reg, TPL)
    if sid == "DS1":
        # Behaviour fix (not the frozen stub): an unregistered vector or an out-of-range id now counts the stock +4
        # over 6 strings; the old stub read +48h over 7 (the stock-reference finding of test_midhook_stock_features.py,
        # which now covers those cases). The frozen stub stays the reference for the registered ones.
        cases = [c for c in cases if registered(c, begin_reg, id_reg)]
    register(Spec(sid, length, flags="free"), cases)
register(Spec("CP1", 8, flags="free"), lookup_cases(UC_X86_REG_ESI, UC_X86_REG_EAX, CN))
register(Spec("CP2", 8, flags="free"), lookup_cases(UC_X86_REG_EBX, UC_X86_REG_ESI, CN))

# ---- LU3: targets 6 and 7 of the Lua binding's int32[8] (EBP-8, EBP-4) ----------------------------------------
register(
    Spec("LU3", 6),
    [Case(f"ecx{v:x}", regs={UC_X86_REG_ECX: v}, ebp={8: 0x1234 + v, -8: 0x77, -4: 0x88}) for v in (0, 0xDEADBEEF)],
)

# ---- WC2: the eval's midi[8] (+284h) copied to EBP-44h; ESI = that array, EAX = EBX ----------------------------
EVAL = D + 0x1000
register(
    Spec("WC2", 11),
    [
        Case(f"eval{i}", regs={UC_X86_REG_EDI: EVAL, UC_X86_REG_EBX: 0xB0B0B0B0 + i}, mem={EVAL: blob(0x2C0, 11 + i)})
        for i in range(2)
    ],
)

# ---- CS1: the empty tails from the stock clear's ECX / EDX (0 / -1 there; other values too) -------------------
register(
    Spec("CS1", 7),
    [
        Case(
            f"ecx{c:x}-edx{d:x}",
            regs={UC_X86_REG_EAX: EVAL, UC_X86_REG_ECX: c, UC_X86_REG_EDX: d},
            mem={EVAL: blob(0xC28, 21)},
        )
        for c, d in ((0, 0xFFFFFFFF), (0x12345678, 0x9ABCDEF0))
    ],
)

# ---- CC3: `test dword [edx+ecx*4+948h],1000h` over the C8 mask[8] (flags exact) ----------------------------------
CN8 = D + 0x2000


def c8_masks(bit_strings) -> bytes:
    raw = bytearray(blob(0x968, 31))
    for s in range(8):
        raw[0x948 + 4 * s : 0x94C + 4 * s] = dwords(0x80001000 if s in bit_strings else 0x80000FFF)
    return bytes(raw)


register(
    Spec("CC3", 7),
    [
        Case(f"string{s}-bits{len(b)}", regs={UC_X86_REG_EDX: CN8, UC_X86_REG_ECX: s}, mem={CN8: c8_masks(b)})
        for s, b in itertools.product(range(8), ((), (0, 6), (1, 7)))
    ],
)

# ---- CH3: child eval slot store (strings 0-5: the replay; 6/7: the byte at +0C1Eh; parent -1: nothing) --------
TRACKER = D + 0x3000
register(
    Spec("CH3", 13),
    [
        Case(
            f"string{s}-parent{p:x}",
            regs={
                UC_X86_REG_EBX: TRACKER,
                UC_X86_REG_EDX: 0x30A * (p & 1),
                UC_X86_REG_EAX: 0xAAAA0042,
                UC_X86_REG_ECX: 3,
            },
            mem={TRACKER: blob(0x1900, 41)},
            ebp={-0xE0C: s, -0xE00: p},
        )
        for s, p in itertools.product((0, 5, 6, 7), (0, 1, 0xFFFFFFFF))
    ],
)

# ---- PL1: chord string 6 plucked through Rs7_S7Pluck; replays `mov al,[ebp-10h]; cmp al,0FFh` (full mode) ------
T8REC, SESSION, GUITAR = D + 0x5000, D + 0x5400, D + 0x5800


def pluck_objects(fret: int) -> dict:
    raw = bytearray(blob(0x80, 51))
    raw[0x0A + 0x44] = fret
    session = bytearray(blob(0x380, 52))
    session[0x37C:0x380] = dwords(GUITAR)
    guitar = bytearray(blob(0x10, 53))
    guitar[0x0C:0x10] = dwords(0x6E6E0000)
    return {T8REC: bytes(raw), SESSION: bytes(session), GUITAR: bytes(guitar)}


# Behaviour fix (not the frozen stub): the hook keeps EAX's upper bytes for the replayed `mov al` like the stock code;
# the old stub zero-extended the fret into EAX first (the stock-reference finding of test_midhook_stock_features.py,
# which covers incoming upper bytes). The frozen stub stays the reference with EAX's upper bytes 0, where both agree.
register(
    Spec("PL1", 5, helpers={"Rs7_S7Pluck": Helper(3)}),
    [
        Case(
            f"fret{f:x}-al{a:x}",
            regs={UC_X86_REG_EDI: T8REC + 0x0A, UC_X86_REG_EAX: 7},
            mem=pluck_objects(f),
            ebp={8: SESSION, -0x10: a},
        )
        for f, a in itertools.product((0xFF, 0, 1, 12), (0xFF, 3))
    ],
)


# ---- UP1-UP3, PD2-PD5: technique-check latches `inc cnt; add ptr,step; cmp cnt,6; jb loop` (flags exact) ---------
EV = D + 0x6000
TAILS = (  # (midi, mask, fret, slideTo) of strings 6 and 7
    ((-1, 0, 0, 0), (-1, 0, 0, 0)),
    ((40, 0x400000, 3, 5), (0, 0x800, 0, 0)),
    ((40, 0x800, 5, 2), (35, 0x400000, 7, 12)),
    ((35, 0x1010, 2, 9), (30, 0x401810, 4, 1)),
    ((35, 0, 0, 0), (30, 0x10, 9, 0)),
    ((1, 0xFFFFFFFF, 0xFF, 0), (0x7FFFFFFF, 0x10, 1, 0xFF)),
)


def eval_tails(tails) -> bytes:
    raw = bytearray(blob(0xC28, 61))
    for t, (midi, mask, fret, slide) in enumerate(tails):
        raw[0x29C + 4 * t : 0x2A0 + 4 * t] = dwords(midi)
        raw[0x2A4 + 4 * t : 0x2A8 + 4 * t] = dwords(mask)
        raw[0x2AC + t] = fret
        raw[0x2B0 + t] = slide
    return bytes(raw)


COUNTS = (0, 4, 5, 6, 0xFF)  # 5 and 6 reach the tails (6 and 7 after the step), the others the loop


def latch_cases(counter, pointer, base, step, extra):
    """`extra`: list of (name, regs) varied with the tails."""
    out = []
    for n, (ti, tails), (name, regs) in itertools.product(COUNTS, enumerate(TAILS), extra):
        if n not in (5, 6) and ti:
            continue  # the loop exit does not read the tails
        out.append(
            Case(
                f"cnt{n:x}-tails{ti}-{name}",
                regs={**regs, counter: JUNK | n, pointer: EV + base + step * (n & 0xF)},
                mem={EV: eval_tails(tails)},
            )
        )
    return out


ACCS = [(f"acc{a:x}", a) for a in (0, 1, 0xFF)]
for sid, pointer, acc in (
    ("UP1", UC_X86_REG_ESI, UC_X86_REG_ECX),
    ("UP2", UC_X86_REG_ESI, UC_X86_REG_EDX),
    ("UP3", UC_X86_REG_EDI, UC_X86_REG_EDX),
):
    register(
        Spec(sid, 9, exits=("resume", "target")),
        latch_cases(UC_X86_REG_EAX, pointer, 0x2BC, 4, [(name, {acc: 0xACAC0000 | a}) for name, a in ACCS]),
    )
for sid, counter, scratch in (
    ("PD2", UC_X86_REG_ECX, UC_X86_REG_EDX),
    ("PD3", UC_X86_REG_EBX, UC_X86_REG_ECX),
    ("PD4", UC_X86_REG_ECX, UC_X86_REG_EDX),
):
    register(
        Spec(sid, 10, exits=("resume", "target")),
        latch_cases(
            counter,
            UC_X86_REG_ESI,
            0x2BC,
            4,
            [(f"al{a:x}", {UC_X86_REG_EAX: 0xEAEA0000 | a, scratch: 0x5C5C5C77}) for a in (0, 1, 0xFF)],
        ),
    )
register(
    Spec("PD5", 8, exits=("resume", "target")),
    latch_cases(
        UC_X86_REG_EBX,
        UC_X86_REG_ESI,
        0xBEC,
        1,
        [(f"eax{a:x}", {UC_X86_REG_EAX: a, UC_X86_REG_ECX: 0xC1C1C1C1}) for a in (0, 3, 0xFFFFFFFD, 0x7FFFFFFF)],
    ),
)

# ---- SF1: the SNG_Song destructor's call of its body (call site, Before placement; stub frozen at 44135a2) ---------
# The run starts at the callee's entry: [esp] = the return address, ESI = the song. Rs7Ns_OnSongFree(song) always runs.
register(
    Spec("SF1", 5, helpers={"Rs7Ns_OnSongFree": Helper(1)}, exits=("target",), call=True),
    [
        Case(f"song{song:x}", regs={UC_X86_REG_ESI: song}, stack={0: site_va("SF1") + 5, 4: 1})
        for song in (0, D + 0x100, 0xFFFFFFFF)
    ],
)

ALWAYS_CALLS: set[str] = {"SF1"}
(
    test_mid_hook_matches_frozen_stub,
    test_dead_flags_are_rewritten_before_any_read,
    test_every_registered_site_calls_its_helper_at_least_once,
) = oracle_tests(SPECS, ALWAYS_CALLS)
