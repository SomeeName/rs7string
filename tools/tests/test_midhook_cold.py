"""Old-vs-new equivalence of the cold sites converted from naked stubs to C++ mid-hook callbacks.

See midhook_oracle.py for what is compared (GPRs, EFLAGS, next EIP, x87, XMM, live stack, data, every
machine write, settings, complete helper inputs) and docs/ARCHITECTURE.md ("Mid-hook conversion").
Every case runs under 3 incoming EFLAGS patterns (the last has DF set) and 2 stack alignments.
"""

from __future__ import annotations

import itertools
from functools import lru_cache

import pytest

from test_emu import DLL, SNAPSHOT, DATA, STACK
from midhook_oracle import FLAG_PATTERNS, MISALIGN, Case, Helper, Machine, Spec, Sym, flags_dead_at

from unicorn.x86_const import (
    UC_X86_REG_EAX,
    UC_X86_REG_EBX,
    UC_X86_REG_ECX,
    UC_X86_REG_EDI,
    UC_X86_REG_EDX,
    UC_X86_REG_ESI,
)

pytestmark = pytest.mark.skipif(
    not DLL.exists() or not (SNAPSHOT / "memory.bin").exists(), reason="needs built DLL and snapshot"
)

SETTINGS = (0, 1, 2, 0x80000000, 0xFFFFFFFF)  # switches are dwords: noncanonical values too
D = DATA + 0x2000  # scratch objects below (page-aligned, zeroed per run)

SPECS: dict[str, tuple[Spec, list[Case]]] = {}


def register(spec: Spec, cases: list[Case]):
    SPECS[spec.sid] = (spec, cases)


def blob(n: int, seed: int) -> bytes:
    return bytes((i * 31 + seed) & 255 for i in range(n))


# ---- TK: entry of Play_Tuning_ReadStringOffsets (ECX = out, [esp+4] = holder) ---------------------
register(
    Spec(
        "TK",
        6,
        pushed=4,
        helpers={"Rs7_TunerKey": Helper(2, deref=((0, 24), (1, 32)))},
        words={"g_rs7_tn_on": 0},
    ),
    [
        Case(
            f"names{b:x}",  # 1.0: [tuner] seven_slot is gone (always on), TK always reads the key
            regs={UC_X86_REG_ECX: D + 0x100},
            mem={D + 0x100: blob(24, 1), D + 0x200: blob(32, 2)},
            stack={4: D + 0x200},
            words={"g_rs7_tn_on": b},
        )
        for b in SETTINGS
    ],
)

# ---- TS: entry of Play_TunerSession_ApplyTuning ([esp+4] = session, EDI = offsets) ----------------
register(
    Spec("TS", 6, pushed=4, helpers={"Rs7_TunerApply": Helper(2, deref=((0, 48), (1, 24)))}),
    [
        Case(
            "on",  # 1.0: [tuner] seven_slot is gone (always on), TS always hands over
            regs={UC_X86_REG_EDI: D + 0x300},
            mem={D + 0x300: blob(24, 3), D + 0x400: blob(48, 4)},
            stack={4: D + 0x400},
        )
    ],
)

# ---- TUT: the open call's Tuning object (EDI), Attributes at [ebp-88h]; flags dead ---------------
register(
    Spec(
        "TUT",
        6,
        helpers={"Rs7_TnUuidTuning": Helper(2, deref=((0, 16), (1, 16)))},
        flags="dead",
        words={"g_rs7_tn_on": 0},
    ),
    [
        Case(
            f"names{a:x}",
            regs={UC_X86_REG_EDI: D + 0x500},
            mem={
                D + 0x500: (D + 0x520).to_bytes(4, "little") + blob(28, 5),
                D + 0x520: (D + 0x540).to_bytes(4, "little") + blob(28, 6),
                D + 0x560: blob(16, 7),
            },
            ebp={-0x88: D + 0x560},
            words={"g_rs7_tn_on": a},
        )
        for a in SETTINGS
    ],
)

# ---- XB1: end of SNG_Song_Load: song = [ebp+8], stream = site ESP + 14h ----------------------------
register(
    Spec(
        "XB1",
        9,
        helpers={"Rs7Ext_OnSngLoaded": Helper(2, deref=((0, 32), (1, 64)))},
        xmm="exact",
        patch=lambda m: {m.target: bytes.fromhex("B8 78 56 34 12 C3")},
    ),  # callee: mov eax,..; ret
    [
        Case(
            f"song{i}",
            regs={UC_X86_REG_EAX: 0xA1, UC_X86_REG_ECX: 0xC1},
            mem={D + 0x600: blob(32, 8 + i)},
            ebp={8: D + 0x600},
        )
        for i in range(3)
    ],
)


# ---- N1 / N21: pool request counts of Rnd_Guitar_LoadDependencies (flags dead) -------------------
COUNTS = (0, 4, 5, 6, 7, 8, 9, 255)
PROFILE_BYTES = (0, 1, 2, 3, 4, 5, 6)  # filler: the byte at +38h is the case's count


def profile(count: int) -> bytes:
    raw = bytearray(blob(0x40, 11))
    raw[0x38] = count
    return bytes(raw)


for sid, reg, other in (("N1", UC_X86_REG_EAX, UC_X86_REG_ECX), ("N21", UC_X86_REG_ECX, UC_X86_REG_EAX)):
    register(
        Spec(sid, 7, pushed=8, flags="dead", words={"g_rs7_publish_ports": 0, "g_rs7_s7_on": 0}),
        [
            Case(
                f"count{n}-ports{p:x}-s7{s:x}",
                regs={reg: D + 0x700, other: 0xC0FFEE00 + n},
                mem={D + 0x700: profile(n)},
                words={"g_rs7_publish_ports": p, "g_rs7_s7_on": s},
            )
            for n, p, s in itertools.product(COUNTS, (0, 1, 0x80000000), (0, 1, 0xFFFFFFFF))
        ],
    )


# ---- GSZ1: GuitarStrings free size (marker word +3Ah, magic dword +0ACh) ---------------------------
def gs_object(marker: int, magic: int) -> bytes:
    raw = bytearray(blob(0xB0, 21))
    raw[0x3A:0x3C] = marker.to_bytes(2, "little")
    raw[0xAC:0xB0] = magic.to_bytes(4, "little")
    return bytes(raw)


register(
    Spec("GSZ1", 5, pushed=12, flags="dead"),
    [
        Case(
            f"marker{m:x}-magic{g:x}",
            regs={UC_X86_REG_ESI: D + 0x800, UC_X86_REG_ECX: 0xAAA1, UC_X86_REG_EDX: 0xBBB2},
            mem={D + 0x800: gs_object(m, g)},
        )
        for m, g in itertools.product((0x3752, 0x3753, 0), (0x47375352, 0x47375353, 0))
    ],
)

# ---- GIZ1: InstrumentInfo free size (the helper's result also stays in EAX) -------------------------
register(
    Spec("GIZ1", 5, pushed=12, flags="dead", helpers={"Rs7Gs_InfoFreeSize": Helper(1, deref=((0, 0x48),))}),
    [
        Case(
            f"size{r:x}",
            regs={UC_X86_REG_ESI: D + 0x900, UC_X86_REG_ECX: 0xAAA1, UC_X86_REG_EDX: 0xBBB2},
            mem={D + 0x900: blob(0x48, 31)},
            rets={"Rs7Gs_InfoFreeSize": r},
        )
        for r in (0x48, 0x38, 0, 0xFFFFFFFF)
    ],
)


# ---- PSA2: the PegSet's sized free, native table entry cleared (ownership table g_rs7_pegsets) ------
def peg_table(entries: dict[int, int]) -> bytes:
    raw = bytearray(64 * 4)
    for index, value in entries.items():
        raw[4 * index : 4 * index + 4] = value.to_bytes(4, "little")
    return bytes(raw)


PEG = D + 0xA00
OTHERS = {i: 0x5D700000 + 0x100 * i for i in range(64)}
PEG_TABLES = {
    "empty": ({}, PEG),
    "other-entries": ({3: 0x5D700100, 40: 0x5D700200}, PEG),
    "owned-first": ({0: PEG, 5: 0x5D700100}, PEG),
    "owned-middle": ({17: PEG, 2: 0x5D700100, 60: 0x5D700200}, PEG),
    "owned-last": ({63: PEG, 0: 0x5D700100}, PEG),
    "table-full-other": (dict(OTHERS), PEG),
    "table-full-owned": ({**OTHERS, 33: PEG}, PEG),
    "null-pegset": ({7: 0x5D700100}, 0),
}
register(
    Spec("PSA2", 5, pushed=4, flags="dead"),
    [
        Case(name, regs={UC_X86_REG_ESI: ps}, mem={Sym("g_rs7_pegsets"): peg_table(entries)})
        for name, (entries, ps) in PEG_TABLES.items()
    ],
)


# ---- PSD1 / PSD2 / PSU3: the PegSet destructor and unload counts, selected by table membership -------
def peg_cases(reg, extra_regs=None, extra_mem=None):
    return [
        Case(
            name,
            regs={reg: ps, **(extra_regs or {})},
            mem={Sym("g_rs7_pegsets"): peg_table(entries), **(extra_mem or {})},
        )
        for name, (entries, ps) in PEG_TABLES.items()
    ]


register(Spec("PSD1", 7, flags="dead"), peg_cases(UC_X86_REG_EAX))
register(Spec("PSD2", 6, flags="dead"), peg_cases(UC_X86_REG_EAX, {UC_X86_REG_EDI: 0xD1D1D1D1}))
register(Spec("PSU3", 5, flags="dead"), peg_cases(UC_X86_REG_EDI, {UC_X86_REG_ECX: 0xC1C1C1C1}))

# ---- PSC1: PegSet constructor tail (EDI = PegSet; replayed: mov eax,edi; pop ebx; mov esp,ebp) -----
register(
    Spec("PSC1", 5, flags="dead", helpers={"Rs7_PegSetCtor": Helper(1, deref=((0, 0x1D4),))}),
    [
        Case(f"ctor{i}", regs={UC_X86_REG_EDI: D + 0xB00 + 0x400 * i}, mem={D + 0xB00 + 0x400 * i: blob(0x1D4, 41 + i)})
        for i in range(3)
    ],
)

# ---- PSL1: the count a PegSet load takes (EDI = info, +0Bh = string count; EBX = PegSet) ----------
INFO = D + 0x1800
register(
    Spec("PSL1", 6, flags="dead", words={"g_rs7_head_on": 0, "g_rs7_peg_load": 7}),
    [
        Case(
            f"count{n}-head{h:x}-load{ld:x}-{name}",
            regs={UC_X86_REG_EDI: INFO, UC_X86_REG_EBX: ps, UC_X86_REG_EAX: 0xAAAA0001, UC_X86_REG_ECX: 0xCCCC0002},
            mem={INFO: bytes(0x0B) + bytes([n]) + bytes(4), Sym("g_rs7_pegsets"): peg_table(entries)},
            words={"g_rs7_head_on": h, "g_rs7_peg_load": ld},
        )
        for n, h, ld, (name, (entries, ps)) in itertools.product(
            (0, 4, 5, 6, 7, 8),
            (0, 1, 0x80000000),
            (7, 8, 0x12345678),
            [
                (k, v)
                for k, v in PEG_TABLES.items()
                if k in ("empty", "owned-middle", "table-full-other", "null-pegset")
            ],
        )
    ],
)


# ---- PSM2: peg mesh collection pass latch `cmp bl,6; jb loop` (PegSet = [[ebp+8]+8]) ----------------
# A native PegSet compares against 8; every other PegSet keeps the stock compare. Flags are exact on all
# paths and both branch exits are reached. (PSU1, PSU2, PSM1 stay hand-written: operand-size prefix.)
PEGSET = D + 0x2400
HOLDER = D + 0x2800
JUNK = 0x5A5A0000
register(
    Spec("PSM2", 9, exits=("resume", "target")),
    [
        Case(
            f"bl{v:x}-{name}",
            regs={UC_X86_REG_EBX: JUNK | v, UC_X86_REG_EAX: 0xEA000001},
            mem={
                HOLDER: bytes(8) + PEGSET.to_bytes(4, "little"),
                Sym("g_rs7_pegsets"): peg_table({i: (PEGSET if x == PEG else x) for i, x in entries.items()}),
            },
            ebp={8: HOLDER},
        )
        for v, (name, (entries, ps)) in itertools.product(
            (0, 1, 5, 6, 7, 8, 9, 0xFF), [(k, PEG_TABLES[k]) for k in ("empty", "owned-middle", "table-full-other")]
        )
    ],
)

# ---- PSR1: colour reset loop bound: the PegSet's active count word (flags untouched) ---------------
SESS, PA, PB, PP = D + 0x3000, D + 0x3100, D + 0x3200, D + 0x3300
register(
    Spec("PSR1", 10),
    [
        Case(
            f"active{n:x}",
            regs={
                UC_X86_REG_ESI: SESS,
                UC_X86_REG_EAX: 0xAAAA0001,
                UC_X86_REG_ECX: 0xCCCC0002,
                UC_X86_REG_EDX: 0xDDDD0003,
            },
            mem={
                SESS: bytes(0x2C) + PA.to_bytes(4, "little"),
                PA: bytes(0x18) + PB.to_bytes(4, "little"),
                PB: bytes(0x0C) + PP.to_bytes(4, "little"),
                PP: bytes(4) + n.to_bytes(2, "little") + bytes(6),
            },
        )
        for n in (0, 4, 6, 7, 8, 0xFFFF)
    ],
)

# ---- PSN1: colour name lookup `mov ecx,[ebx*4+names]`; index >= 6 takes name 0 (flags: cmp ebx,6) ----
NAMES = D + 0x3400
register(
    Spec("PSN1", 7, words={"g_rs7_pegnames": NAMES}),
    [
        Case(
            f"index{i:x}",
            # ESI is a real tuner session at PSN1, now also used to track the palette's player.
            regs={UC_X86_REG_EBX: i, UC_X86_REG_ECX: 0xCCCC0002, UC_X86_REG_ESI: D + 0x3500},
            mem={
                NAMES: b"".join((0x71000000 + k).to_bytes(4, "little") for k in range(6)),
                D + 0x3500: bytes(0x30),
            },
            words={"g_rs7_pegnames": NAMES},
        )
        for i in (0, 1, 2, 5, 6, 7, 8, 255, 0x80000000, 0xFFFFFFFF)
    ],
)

# ---- GSSS / GSSE: section dimming switched on / off per string (+70h) -----------------------------
GS, INFO2, STR = D + 0x3800, D + 0x3A00, D + 0x3C00
OBJ = 0x80


def gs_image(native: bool, info: int, loaded: int, pointers) -> bytes:
    raw = bytearray(blob(0xB0, 51))
    raw[0x3A:0x3C] = (0x3752 if native else 0x3753).to_bytes(2, "little")
    raw[0xAC:0xB0] = (0x47375352 if native else 0x47375353).to_bytes(4, "little")
    raw[0xA8:0xAC] = info.to_bytes(4, "little")
    raw[0xA3] = loaded
    for i, p in enumerate(pointers):
        raw[0x40 + 4 * i : 0x44 + 4 * i] = p.to_bytes(4, "little")
    return bytes(raw)


def string_objects() -> dict:
    return {STR + OBJ * i: blob(OBJ, 60 + i) for i in range(8)}


def info_image(stock_count: int, loaded_count: int) -> bytes:
    raw = bytearray(blob(0x60, 71))
    raw[0x0B] = stock_count
    raw[0x41] = loaded_count
    return bytes(raw)


ALL_STRINGS = [STR + OBJ * i for i in range(8)]
register(
    Spec("GSSS", 7, exits=("resume", "target")),
    [
        Case(
            f"native{int(n)}-info{int(i)}-loaded{ld}-count{cnt}-stock{sc}",
            regs={
                UC_X86_REG_ECX: GS,
                UC_X86_REG_EDX: INFO2,
                UC_X86_REG_EAX: 0xEAEAEA01,
                UC_X86_REG_ESI: 0x51515101,
                UC_X86_REG_EDI: 0xD1D1D101,
            },
            mem={
                GS: gs_image(n, INFO2 if i else INFO2 + 4, ld, ALL_STRINGS),
                INFO2: info_image(sc, cnt),
                **string_objects(),
            },
        )
        for n, i, ld, cnt, sc in itertools.product((True, False), (True, False), (0, 1), (0, 1, 3, 8), (0, 6))
    ],
)
register(
    Spec("GSSE", 5, exits=("resume", "target")),
    [
        Case(
            f"native{int(n)}-bl{bl:x}-stock{sc}-{name}",
            regs={
                UC_X86_REG_ECX: GS,
                UC_X86_REG_EDX: INFO2,
                UC_X86_REG_EBX: 0xB1B1B100 | bl,
                UC_X86_REG_EAX: 0xEAEAEA01,
                UC_X86_REG_EDI: 0xD1D1D101,
            },
            mem={GS: gs_image(n, INFO2, 1, ptrs), INFO2: info_image(sc, 8), **string_objects()},
        )
        for n, bl, sc, (name, ptrs) in itertools.product(
            (True, False),
            (0, 1, 0xFF),
            (0, 1, 6),
            (
                ("all", ALL_STRINGS),
                ("holes", [ALL_STRINGS[0], 0, ALL_STRINGS[2], 0, 0, ALL_STRINGS[5], 0, ALL_STRINGS[7]]),
                ("lastnull", ALL_STRINGS[:7] + [0]),
            ),
        )
    ],
)


@lru_cache(maxsize=None)
def machine(sid: str) -> Machine:
    return Machine(SPECS[sid][0])


def cases():
    for sid, (_, cs) in SPECS.items():
        for case, flags, misalign in itertools.product(cs, FLAG_PATTERNS, MISALIGN):
            yield pytest.param(sid, case, flags, misalign, id=f"{sid}-{case.id}-f{flags:x}-a{misalign}")


@pytest.mark.parametrize("sid,case,flags,misalign", list(cases()))
def test_mid_hook_matches_frozen_stub(sid, case, flags, misalign):
    machine(sid).compare(case, flags, misalign)


@pytest.mark.parametrize("sid", [sid for sid, (spec, _) in SPECS.items() if spec.flags == "dead"])
def test_dead_flags_are_rewritten_before_any_read(sid):
    """The flag contract "dead" (the hook leaves the incoming arithmetic flags, the old stub left helper
    leftovers) is only valid where no continuation reads them: scan the game code after each exit."""
    m = machine(sid)
    for exit_address in m.exit_addresses:
        assert flags_dead_at(exit_address), f"{sid}: flags may be live at {exit_address:#x}"


def test_every_registered_site_calls_its_helper_at_least_once():
    """The matrix is not vacuous: for every site some case reaches each helper (and some case skips it)."""
    for sid, (spec, cs) in SPECS.items():
        reached = {name: False for name in spec.helpers}
        skipped = {name: False for name in spec.helpers}
        exits = set()
        for case in cs:
            state = machine(sid).compare(case, FLAG_PATTERNS[0], 0)
            exits.add(state["eip"])
            calls = {c[0] for c in state["calls"]}
            for name in spec.helpers:
                (reached if name in calls else skipped)[name] = True
        assert exits == set(machine(sid).exit_addresses), (sid, [hex(e) for e in exits])  # every exit is taken
        assert all(reached.values()), (sid, reached)
        assert all(skipped.values()) or sid in ALWAYS_CALLS, (sid, skipped)


ALWAYS_CALLS = {"XB1", "GIZ1", "PSC1", "TK", "TS"}  # the helper call is unconditional
