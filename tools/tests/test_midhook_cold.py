"""Old-vs-new equivalence of the sites converted from naked stubs to C++ mid-hook callbacks (cold sites in full mode,
the hot BD1-BD4, TF1 and V2 in light mode; the N-slot chord sites are in test_midhook_nslot.py).

See midhook_oracle.py for what is compared (GPRs, EFLAGS, next EIP, x87, XMM, live stack, data, every
machine write, settings, complete helper inputs) and docs/ARCHITECTURE.md ("Mid-hook conversion").
Every case runs under 3 incoming EFLAGS patterns (the last has DF set) and 2 stack alignments.
"""

from __future__ import annotations

import itertools

import pytest

from emu_harness import DLL, SNAPSHOT, DATA, STACK, site_target
from midhook_oracle import Case, Helper, Spec, Sym, oracle_tests

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
        locals=0x28,  # `push ebp; mov ebp,esp; sub esp,28h`
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
    Spec("TS", 6, pushed=4, locals=8, helpers={"Rs7_TunerApply": Helper(2, deref=((0, 48), (1, 24)))}),
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


# ---- refactor step 7, batch 1 (stubs frozen at 0cf5874) -------------------------------------------
# BD1-BD4: perStringMode guards `cmp <string>,6; jae` (light). String 6 reads string 0's mode and joins the stock
# body after it; the old stubs left the flags of `add [esp],n` there, so the contract is "free" (exits checked).
MODES = (0, 1, 2, 0xFFFFFFFF)
STRINGS = (0, 3, 5, 6, 7, 0xFF)
BINFO, BGUITAR, BSESSION = D + 0x4000, D + 0x4100, D + 0x4200


def bend_info(mode0: int) -> bytes:
    raw = bytearray(blob(0x40, 81))
    raw[0x20:0x24] = mode0.to_bytes(4, "little")
    return bytes(raw)


def bend_objects(mode0: int) -> dict:
    guitar = bytearray(blob(0x40, 82))
    guitar[0x24:0x28] = BINFO.to_bytes(4, "little")
    session = bytearray(blob(0x400, 83))
    session[0x37C:0x380] = BGUITAR.to_bytes(4, "little")
    return {BINFO: bend_info(mode0), BGUITAR: bytes(guitar), BSESSION: bytes(session)}


BEND_SITES = {  # sid -> (string register, base register and value, join offset after the site)
    "BD1": (UC_X86_REG_ECX, (UC_X86_REG_EAX, BINFO), 0x0A),
    "BD2": (UC_X86_REG_ECX, (UC_X86_REG_EAX, BINFO), 0x0E),
    "BD3": (UC_X86_REG_EBX, (UC_X86_REG_EDI, BSESSION), 0x17),
    "BD4": (UC_X86_REG_EBX, (UC_X86_REG_EAX, BGUITAR), 0x20),
}
for sid, (string_reg, (base_reg, base), join) in BEND_SITES.items():
    register(
        Spec(sid, 5, flags="free", exits=("resume", "target", ("resume", join))),
        [
            Case(
                f"string{v:x}-mode{m:x}",
                regs={string_reg: 0xABCD0000 | v, base_reg: base, UC_X86_REG_EDX: 0xDDDD0003},
                mem=bend_objects(m),
            )
            for v, m in itertools.product(STRINGS, MODES)
        ],
    )
# BD1 leaves through the target for a string-6 mode other than 0, BD2-BD4 only for strings above 6.

# ---- D5: gem position log of strings 0 and 6 (diagnostics); replays `fld [ebx+14h]; mov eax,[esp+10h]` ------
GEM = D + 0x4800
register(
    Spec("D5", 7, helpers={"Rs7_DiagGemPos": Helper(2, deref=((0, 0x20), (1, 12)))}),
    [
        Case(f"string{v:x}", regs={UC_X86_REG_EBX: GEM}, mem={GEM: bytes(blob(0x0C, 91)) + bytes([v]) + blob(0x13, 92)})
        for v in STRINGS
    ],
)

# ---- B1: chord frame marks after Rnd_GemGeom_Ctor; replays `mov esi,[ebp+18h]; push eax; call <stdcall 1>` -------
register(
    Spec(
        "B1",
        9,
        helpers={"Rs7Chord_OnBoxGeom": Helper(3)},
        patch=lambda m: {m.target: bytes.fromhex("31 C0 B8 78 56 34 12 C2 04 00")},  # xor eax,eax; mov eax,..; ret 4
    ),
    [
        Case(f"geom{g:x}-chord{o:x}", regs={UC_X86_REG_EAX: g}, ebp={8: o, 0x0C: D + 0x4C00, 0x18: 0x51515151})
        for g, o in itertools.product((0, D + 0x4A00), (0, D + 0x4B00))
    ],
)


# ---- refactor step 7, batch 2 (stubs frozen at 24bfba8) -------------------------------------------
# N12: section mask guard `cmp cl,6; jae skip`; string 6 goes on to the stock write with [neck] string7_line. The old
# stub left the flags of its own compare of g_rs7_s7_on there, the hook the incoming ones ("free", exits checked).
register(
    Spec("N12", 5, flags="free", exits=("resume", "target")),
    [
        Case(f"string{v:x}-on{on:x}", regs={UC_X86_REG_ECX: 0xABCD0000 | v}, words={"g_rs7_s7_on": on})
        for v, on in itertools.product(STRINGS, SETTINGS)
    ],
)

# ---- N13: back-fill loop bound `cmp cl,6; jb loop`; after string 5 the string-6 back-fill (ESI = song, EDI = level) --
register(
    Spec("N13", 9, flags="free", helpers={"Rs7_S7MaskBackfill": Helper(2)}, exits=("resume", "target")),
    [
        Case(f"string{v:x}", regs={UC_X86_REG_ECX: 0xABCD0000 | v, UC_X86_REG_ESI: D + 0x5000, UC_X86_REG_EDI: 3})
        for v in STRINGS
    ],
)

# ---- PM0 / PR0 / PM1: native pitch tails at matcher creation, detector reset and matcher tuning --------------------
MATCHER, DETECTOR, TUNING = D + 0x5100, D + 0x6000, D + 0x5200
register(
    Spec("PM0", 7, flags="dead", helpers={"Rs7Pitch_MatNew": Helper(1, deref=((0, 0x80),))}),
    [Case("matcher", regs={UC_X86_REG_EAX: MATCHER}, mem={MATCHER: blob(0x80, 101)})],
)
register(
    Spec("PR0", 6, flags="dead", helpers={"Rs7Pitch_DetReset": Helper(1, deref=((0, 0x40),))}),
    [Case("detector", regs={UC_X86_REG_EBX: DETECTOR}, mem={DETECTOR: blob(0x1400, 102)})],
)
register(
    Spec("PM1", 6, helpers={"Rs7Pitch_MatTuned": Helper(2, deref=((0, 0x80), (1, 8)))}),
    [
        Case(
            f"det{d:x}",
            regs={UC_X86_REG_ESI: MATCHER, UC_X86_REG_EDX: TUNING},
            mem={MATCHER: blob(4, 103) + d.to_bytes(4, "little") + blob(0x78, 104), TUNING: blob(8, 105)},
        )
        for d in (0, D + 0x5300)
    ],
)
# PM0, PR0 and PM1 always call their helper.

# ---- refactor step 7, batch 3 (stubs frozen at b8c0ed7; the N-slot sites: test_midhook_nslot.py) ------------
# TF1: the tuner camera's reference string BL = min(count, 6) - 1 (light); the old stub left its `dec bl` flags.
TGUITAR, TINFO = D + 0x5000, D + 0x5100
register(
    Spec("TF1", 8, flags="free"),
    [
        Case(
            f"count{n:x}",
            regs={UC_X86_REG_EAX: TGUITAR, UC_X86_REG_EBX: 0xB1B1B100, UC_X86_REG_EDX: 0xDDDD0003},
            mem={TGUITAR: bytes(0x24) + TINFO.to_bytes(4, "little"), TINFO: bytes(0x0B) + bytes([n]) + bytes(4)},
        )
        for n in (0, 1, 4, 6, 7, 8, 0xFF)
    ],
)

# V2: the slide end cell `al * row + ecx + edx + 3Ch` with the stock and the native row stride (light).
register(
    Spec("V2", 15, flags="free", words={"g_rs7_fg_row": 0x1CC}),
    [
        Case(
            f"al{a:x}-row{r:x}",
            regs={UC_X86_REG_EAX: 0xEAEAEA00 | a, UC_X86_REG_ECX: 0x40000000, UC_X86_REG_EDX: 0x4C * 6},
            words={"g_rs7_fg_row": r},
        )
        for a, r in itertools.product((0, 1, 23, 0xFF), (0x1CC, 0x264))
    ],
)

# ---- refactor step 7, batch 2b (stubs frozen at 6d1a62c) ------------------------------------------
# N5: per-frame driver, then the replay `mov eax,[ebx+18h]; test eax,eax` (flags exact).
GUITAR = D + 0x7000
register(
    Spec("N5", 5, helpers={"Rs7_S7Update": Helper(1)}),
    [
        Case(f"gs{g:x}", regs={UC_X86_REG_EBX: GUITAR}, mem={GUITAR: blob(0x18, 111) + g.to_bytes(4, "little")})
        for g in (0, D + 0x7100)
    ],
)

# N18 / N22: `cmp al,[esi+0Bh]; jae` with a string-6 path to a join (EDX = 6, ECX = the object, EAX = the join).
# Strings x count x fret x helper result; the flags the old stubs left are dead at every exit ("free").
BEND_INFO, BEND_GS, BEND_OBJ = D + 0x7200, D + 0x7300, D + 0x7400
COUNTS = (6, 7)


def info_with(count: int) -> bytes:
    raw = bytearray(blob(0x10, 112))
    raw[0x0B] = count
    return bytes(raw)


for sid, length, join, amount_at in (("N18", 9, 0x10, -0x64), ("N22", 5, 0x0C, -0x60)):
    register(
        Spec(
            sid,
            length,
            flags="free",
            helpers={"Rs7_S7BendTarget": Helper(4)},
            exits=("resume", "target", ("resume", join)),
        ),
        [
            Case(
                f"string{v:x}-count{n}-fret{f:x}-obj{r:x}",
                regs={
                    UC_X86_REG_EAX: 0xABCD0000 | v,
                    UC_X86_REG_EDX: 0xDDDD0000 | f,
                    UC_X86_REG_ESI: BEND_INFO,
                    UC_X86_REG_ECX: BEND_GS,
                },
                mem={BEND_INFO: info_with(n)},
                ebp={amount_at: 0x3F000000},
                rets={"Rs7_S7BendTarget": r},
            )
            for v, n, f, r in itertools.product(STRINGS, COUNTS, (3, 0xFF), (0, BEND_OBJ))
        ],
    )

# N19 / N20 / N23 (light): the release shape `cmp <string>,[info+0Bh]; jae`, join at resume + 7. The old stubs called
# Rs7_S7BendTarget (kinds 1 / 3, no log), which now calls the split-off Rs7_S7BendObject the light hooks call directly:
# the real Rs7_S7BendTarget runs in the old run, so both runs record the same Rs7_S7BendObject calls.
RELEASE_SITES = {  # sid -> (string register, info register, GuitarStrings register)
    "N19": (UC_X86_REG_EAX, UC_X86_REG_EDX, UC_X86_REG_ECX),
    "N20": (UC_X86_REG_EAX, UC_X86_REG_EDX, UC_X86_REG_ECX),
    "N23": (UC_X86_REG_ECX, UC_X86_REG_ESI, UC_X86_REG_EDX),
}
for sid, (string_reg, info_reg, gs_reg) in RELEASE_SITES.items():
    register(
        Spec(sid, 5, flags="free", helpers={"Rs7_S7BendObject": Helper(2)}, exits=("resume", "target", ("resume", 7))),
        [
            Case(
                f"string{v:x}-count{n}-obj{r:x}",
                regs={string_reg: 0xABCD0000 | v, info_reg: BEND_INFO, gs_reg: BEND_GS},
                mem={BEND_INFO: info_with(n)},
                rets={"Rs7_S7BendObject": r},
            )
            for v, n, r in itertools.product(STRINGS, COUNTS, (0, BEND_OBJ))
        ],
    )

# I1: `sub eax,2; jz bass` in Play_MidiToFret; string 6 of an active song returns MIDI - base through `pop ebp; ret`
# (the old stub's own, the hook's the stock one at resume + 0Dh). The return address on the stack is the bass target,
# an exit, so both runs end there with the same EBP / ESP.
I1_BASS = 0x874079  # site_target("I1"), a constant so that collection needs no game image
register(
    Spec("I1", 5, flags="free", helpers={"Rs7_Base6": Helper(1)}, exits=("resume", "target")),
    [
        Case(
            f"instr{i}-string{s}-key{k:x}-base{b:x}",
            regs={UC_X86_REG_EAX: i, UC_X86_REG_ECX: 50, UC_X86_REG_EDX: s, UC_X86_REG_ESI: k},
            stack={0: 0x5EB05EB0, 4: I1_BASS},
            rets={"Rs7_Base6": b},
        )
        for i, s, k, b in itertools.product((0, 1, 2, 3), (5, 6, 7), (0, D + 0x7500), (0x80000000, 37))
    ],
)

ALWAYS_CALLS = {"XB1", "GIZ1", "PSC1", "TK", "TS", "B1", "PM0", "PR0", "PM1", "N5"}  # the helper call is unconditional
(
    test_mid_hook_matches_frozen_stub,
    test_dead_flags_are_rewritten_before_any_read,
    test_every_registered_site_calls_its_helper_at_least_once,
) = oracle_tests(SPECS, ALWAYS_CALLS)


def test_i1_return_address_is_the_bass_target():
    assert site_target("I1") == I1_BASS
