"""Stock reference of the converted feature and N-slot mid-hook sites (test_midhook_features.py, test_midhook_nslot.py):
the game's original bytes against the hooked path. Framework and contract: midhook_stock.Ref / ruled / W; the cold,
x87 and call sites: test_midhook_stock.py.

The N-slot records are native for every song (ARCHITECTURE.md section 7: a T8 / C8 superset record per template /
ChordNotes, slots 6/7 empty for a six-string song), so a site that reads a relocated record is compared with both layouts
filled with the same strings; a lookup that hands out the record itself differs by design.
"""

from __future__ import annotations

import dataclasses
import re
import struct

import pytest
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDI, UC_X86_REG_EDX, UC_X86_REG_ESI

import test_midhook_features as feat
import test_midhook_nslot as ns
from emu_harness import needs_emu
from emu_harness import DLL, SNAPSHOT, site_va
from midhook_oracle import EBP, ESP, Case, Sym
import private_features
from midhook_stock import RET_AT, Ref, W, assert_want, returning_elsewhere, ruled, stock_tests
from test_midhook_stock import EXPORTS, NATIVE_PEGSET, STRING6, dword, h, v, with_mem

pytestmark = needs_emu

SPECS = {**feat.SPECS, **ns.SPECS}
T8_RECORD = "template / ChordNotes lookups return T8 / C8 records, with an empty widened fallback on failure"
T8_SIZE, C8_SIZE = 0x80, 0x15A8
# lookup_cases: entries 0 / 63 register the base (10 records), entry 2 base + 100h (50 records, at +10000h)
REGISTERED = re.compile(r"-begin(0|100)-id([0-9a-f]+)$")


def returning(spec, cases):
    return returning_elsewhere(spec, cases)


def entry(sid: str, helper: str):
    """A stubbed entry dispatch is a six-string reference only while its helper answers the stock entry."""
    return Ref(
        **ruled(
            lambda c: None if c.rets.get(helper) == site_va(sid) else W("eip"),
            "a helper answer other than the stock entry is a native copy",
        ),
        real=False,
    )


def tuner_init(differences, case, machine):
    """An installed TU8INIT owns the grown allocation even after runtime isolation."""
    assert_want(differences, W("writes"), machine)
    tail = bytearray(0x380 - 0x278)
    struct.pack_into("<I", tail, 0, 0x38545552)
    struct.pack_into("<8i", tail, 0x348 - 0x278, *([-2] * 8))
    session = case.regs[UC_X86_REG_ESI]
    expected = tuple((session + 0x278 + i, bytes([value])) for i, value in enumerate(tail))
    assert differences["writes"] == ((), expected)


def chord_loop(sid: str):
    """CLGU / CLGE / CLVB: strings 6/7 of an 8-string count stay in the chord loop."""

    def expect(c: Case):
        s = h(c, "s")
        if not (s in (6, 7) and v(c, "n") == "8") or c.id.endswith("dlff"):
            return None  # CLGU: chart bend byte FFh (no bend) takes the stock exit either way
        if sid == "CLGU":
            return W("eip", "regs", eip=0x3F, regs={"ecx": feat.OWNER, "edx": 0xD0D0D000})
        if sid == "CLGE":
            return W("eip", "regs", eip=0x2C, regs={"ecx": 0xC1C1C100 | s})
        return W("eip", "regs", eip=0x11, regs={"ecx": 0xC1C1C100 | s, "esi": 0x5E5E5E5E})

    return expect


def ng1(c: Case):
    """NG1 (native next-note grid): cell 347h + 26 * string + fret; strings >= 8 or frets >= 26 return through the
    stock reject tail (`ret 10h` to [ebp+4], the x87 argument popped)."""
    s, f = h(c, "s"), h(c, "f")
    if s < 8 and f < 26:
        return W("regs", regs={"eax": 0x347 + 26 * s + f})
    regs = {"eax": 0x12345678, "ebp": feat.EBP_SAVED, "esp": EBP + 0x18}
    return W("final_esp", "fpu", "regs", "stack", esp=EBP + 0x18 - ESP, regs=regs)


def record(sid: str, base: int, size: int, reg: str, offset: int = 0):
    """TP* / CP*: a registered (begin, id) gets its record; all other lookups get an empty widened record."""

    def expect(c: Case):
        m = REGISTERED.search(c.id)
        if c.id.startswith("empty") or not m or int(m[2], 16) >= (10 if m[1] == "0" else 50):
            return None
        entry_base = base + (0x10000 if m[1] == "100" else 0)
        return W("regs", regs={reg: entry_base + int(m[2], 16) * size + offset})

    def check(differences, case, machine):
        want = expect(case)
        if want is not None:
            assert_want(differences, want, machine)
        else:
            empty_record(differences, machine, reg, size, offset)

    return Ref(six=lambda c: False, why=T8_RECORD, check=check)


def empty_record(differences, machine, reg: str, size: int, offset: int = 0):
    """Only the destination changes, and it points to a complete, unused record (no guessed DLL address)."""
    assert_want(differences, W("regs"), machine)
    index = ("eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp").index(reg)
    old, new = differences["regs"]
    assert old[:index] + old[index + 1 :] == new[:index] + new[index + 1 :]
    raw = machine.emu.read(new[index] - offset, size)
    expected = bytearray(size)
    if size == T8_SIZE:
        expected[4:0x28] = b"\xff" * 0x24
        expected[0x48:0x78] = b"\xff" * 0x30
        expected[0x78] = 6
    else:
        expected[0x930:0x93C] = b"\xff" * 12
        expected[0x968:0x978] = b"\xff" * 16
    assert raw == bytes(expected)


def slot_bound(sid: str, exit_):
    """PSU1 / PSU2 / PSM1: a native PegSet's slot bound is 8, not its active count."""
    return Ref(
        **ruled(
            lambda c: W("eip", eip=exit_) if "native1" in c.id and int(v(c, "n")) <= h(c, "slot") < 8 else None,
            NATIVE_PEGSET,
        )
    )


def whole_function(spec, cases):
    """PUBGF replaces the finger-span function with native publication on: both runs go to the function's return."""
    spec, cases = returning_elsewhere(spec, cases)
    return dataclasses.replace(spec, exits=(RET_AT,)), cases


DS1_BEGIN, DS1_T8 = ns.D + 0x100, ns.D + 0x800
DS1_FRETS = ((0xFF, 3, 0xFF, 0xFF, 0xFF, 0xFF), (5, 7, 0xFF, 0xFF, 0xFF, 0xFF), (0, 2, 2, 1, 0, 0), (0xFF,) * 6)


def ds1_templates() -> bytes:
    raw = bytearray()
    for frets in DS1_FRETS:
        raw += bytes([0x11] * 4) + bytes(frets) + bytes([0x22] * 0x3E)
    return bytes(raw)


def ds1_cases(spec, cases):
    """DS1 counts the frets != -1 of a chord template (2 = double stop). Both layouts carry the same six frets (stock
    template +4, T8 record +48h with slots 6/7 empty); the run goes to the end of the count loop (0x55BA03). Registered:
    the song's T8 array is in g_rsxr_ns. Empty fallback counts are covered by the native failure tests."""
    t8 = bytearray(4 * T8_SIZE)
    for i, frets in enumerate(DS1_FRETS):
        t8[i * T8_SIZE + 0x48 : i * T8_SIZE + 0x50] = bytes(frets) + bytes([0xFF, 0xFF])
    entry_ = struct.pack("<8I", DS1_BEGIN, 4, DS1_T8, 0, 0, 0, 0x5000, 4)
    out = []
    for registered, i in ((True, i) for i in range(4)):
        table = (entry_ if registered else bytes(32)) + bytes(63 * 32)
        out.append(
            Case(
                f"{'registered' if registered else 'unregistered'}-tpl{i}",
                regs={UC_X86_REG_ESI: DS1_BEGIN, UC_X86_REG_ECX: i, UC_X86_REG_EDI: ns.D},
                mem={DS1_BEGIN: ds1_templates(), DS1_T8: bytes(t8), Sym("g_rsxr_ns"): table},
            )
        )
    return dataclasses.replace(spec, exits=(("resume", 10),)), out


def pl1_cases(spec, cases):
    """PL1 with EAX's upper bytes 0 (the old-stub cases) and set: the replayed `mov al` keeps them, like the stock code."""
    eax = UC_X86_REG_EAX
    return spec, cases + [
        dataclasses.replace(c, id=f"{c.id}-upper", regs={**c.regs, eax: ns.JUNK | c.regs[eax]}) for c in cases
    ]


# UP1-UP3, PD2-PD5 (`inc cnt; add ptr,step; cmp cnt,6; jb loop`): past string 5 the hook folds strings 6/7 into the
# technique latch from their tails. The tails are TAILS[k] of test_midhook_nslot.py (k = 0: both empty, the six-string
# chord); counter 6 never reaches this site in the stock loop. The intended results per (counter, tails):
LATCH_CLEARED = {  # the latch byte becomes 0: a string 6/7 tail lacks the technique
    "UP1": {5: {2, 3, 4, 5}, 6: {4, 5}},
    "UP2": {5: {2, 3, 4, 5}, 6: {4, 5}},
    "UP3": {5: {2, 3, 4, 5}, 6: {4, 5}},
    "PD2": {5: {1, 2, 3, 4, 5}, 6: {1, 2, 3, 4, 5}},
    "PD3": {5: {1, 2, 3, 4, 5}, 6: {1, 2, 3, 4, 5}},
    "PD4": {5: {1, 2, 4}, 6: {1, 2, 3}},
}
LATCH_SET = {"PD4": {5: {3, 5}, 6: {4, 5}}}  # the latch becomes exactly 1 (stock may hold any nonzero byte)
LATCH_REG = {"UP1": ("ecx", 0xACAC0000), "UP2": ("edx", 0xACAC0000), "UP3": ("edx", 0)}
# PD5 accumulates a signed value over strings 6/7 (the slide distances of their tails): the hook's EAX per case
PD5_EAX = dict(
    [
        ("cnt5-tails1-eax0", 0x2), ("cnt5-tails2-eax0", 0xFFFFFFFD), ("cnt5-tails2-eax3", 0x5),
        ("cnt5-tails3-eax0", 0x7), ("cnt5-tails3-eax3", 0x7), ("cnt5-tails4-eax0", 0xFFFFFFF7),
        ("cnt5-tails4-eaxfffffffd", 0xFFFFFFF7), ("cnt5-tails5-eax0", 0xFFFFFF01), ("cnt5-tails5-eax3", 0xFE),
        ("cnt5-tails5-eaxfffffffd", 0xFFFFFF01), ("cnt6-tails0-eax0", 0x7C), ("cnt6-tails0-eax3", 0x7C),
        ("cnt6-tails1-eax0", 0x7C), ("cnt6-tails1-eax3", 0x7C), ("cnt6-tails2-eax0", 0x7C), ("cnt6-tails2-eax3", 0x7C),
        ("cnt6-tails3-eax0", 0xFFFFFFFD), ("cnt6-tails3-eax3", 0x7C), ("cnt6-tails4-eax0", 0xFFFFFFF7),
        ("cnt6-tails4-eax3", 0x7C), ("cnt6-tails4-eaxfffffffd", 0xFFFFFFF7), ("cnt6-tails5-eax0", 0xFE),
        ("cnt6-tails5-eax3", 0xFE),
    ]
)  # fmt: skip


def latch(sid: str):
    def expect(c: Case):
        cnt, tails = h(c, "cnt"), int(v(c, "tails"))
        if sid == "PD5":
            return W("regs", regs={"eax": PD5_EAX[c.id]}) if c.id in PD5_EAX else None
        reg, upper = LATCH_REG.get(sid, ("eax", 0xEAEA0000))
        latch_in = h(c, "acc" if sid.startswith("UP") else "al")
        if tails in LATCH_CLEARED[sid].get(cnt, ()) and latch_in:
            return W("regs", regs={reg: upper})
        if tails in LATCH_SET.get(sid, {}).get(cnt, ()) and latch_in > 1:
            return W("regs", regs={reg: upper | 1})
        return None

    return Ref(
        **ruled(
            expect, "non-empty string 6/7 tails (a seven-string chord) or a counter the stock loop never reaches (6)"
        )
    )


def pubga(c: Case):
    s = h(c, "s")
    if s < 6:
        return None
    regs = {"eax": 1 if s < 8 else 0, "ebp": EBP, "esp": ESP + 4}  # RsxrGs_AttachExt (stubbed: 1) or 0, returned
    return W("eip", "final_esp", "regs", "stack", eip=("abs", RET_AT), esp=0x54, regs=regs)


def at_autotune(c: Case):
    """AT (Rsxr_TunerAutotune stubbed): 0 = not tuned, the six-string answer."""
    if v(c, "r") == "0":
        return None
    if v(c, "auto") == "0":
        return W()  # autotune off: the answer is not consulted
    regs = {"eax": 0xAAAAAA01, "ebp": EBP, "esp": ESP + 0x10}  # AL = tuned, returned to the caller
    return W("eip", "final_esp", "regs", "stack", eip=("abs", RET_AT), esp=0x18, regs=regs)


REFS: dict[str, Ref] = {
    # ---- features/chords (CLBM: masks at the record start and in the C8 tail, all eight strings) ----
    "CLBM": Ref(
        variant=with_mem(lambda c: {feat.MASKS + 4 * int(v(c, "s")): c.mem[feat.MASKS + 0x948 + 4 * int(v(c, "s"))]})
    ),
    "CLBA": Ref(
        **ruled(
            lambda c: W("regs", regs={"eax": h(c, "eax") * 0x184 + 0x40000000 + 0x988}),
            "the bends[8] of the C8 record (+988h) in place of the stock bends (+18h), for every song",
        )
    ),
    **{sid: Ref(**ruled(chord_loop(sid), STRING6 + " (8-string count)")) for sid in ("CLGU", "CLGE", "CLVB")},
    # ---- features/nextgrid, fretgrid, diagrams ----
    "NG1": Ref(**ruled(ng1, "the native next-note grid (nextgrid=native, shipped) has its own cell index and bounds")),
    "NG9": Ref(
        **ruled(
            lambda c: (
                W(
                    "eip",
                    "final_esp",
                    "regs",
                    "stack",
                    eip=0x0F,
                    esp=-4,
                    regs={"ecx": feat.EBP2 - 0x35C, "edx": 0x33334444, "esp": ESP + 4},
                )
                if "tree3" in c.id
                else None
            ),
            "the native grid frame moved the tree (EBP-35Ch, size EBP-34Ch; companion sites): the stock "
            "displacements address other locals",
        )
    ),  # fmt: skip
    "FGRB": Ref(),
    "DGB": Ref(),
    "DGM": Ref(),
    "DGS": Ref(),
    "TP13": Ref(
        **ruled(
            lambda c: (
                W("regs", regs={"eax": ns.T8 + h(c, "id") * T8_SIZE})
                if v(c, "on") != "0" and "-10-" in c.id and h(c, "id") < 10
                else None
            ),
            "with [chords] diagrams " + T8_RECORD,
        )
    ),
    # ---- features/tuner (void / dispatch helpers stubbed: they need the live tuner) ----
    **{sid: entry(sid, "RsxrTuner8_Target") for sid in ("TU8H", "TU8C", "TU8A")},
    **{sid: entry(sid, "RsxrTuner8_SessTarget") for sid in feat.TS8_IDS},
    "TU8INIT": Ref(
        six=lambda c: c.regs[UC_X86_REG_ESI] == 0,
        why="the installed allocation stays grown after runtime isolation",
        check=tuner_init,
        variant=returning,
    ),
    "TU8FREE": Ref(),
    "AT": Ref(**ruled(at_autotune, "[tuner] autotune on and the string in tune"), real=False, variant=returning),
    # ---- features/publish ----
    **{sid: entry(sid, "RsxrPublish_Target") for sid in feat.PUB_ENTRIES},
    **{sid: entry(sid, "RsxrPublishPitch_Target") for sid in feat.PUB_PITCH},
    **{
        sid: Ref(
            **ruled(
                lambda c, n=length: W("eip", eip=n) if h(c, "s") in (6, 7) and h(c, "s") < h(c, "n") else None,
                STRING6 + " (strings 6/7 of a 7/8-string guitar are in range)",
            )
        )
        for sid, length in (("PUBGS", 8), ("PUBGE1", 9))
    },
    "PUBGC": Ref(
        **ruled(
            lambda c: W("regs", regs={"ecx": 6}) if h(c, "n") > 6 else None,
            "the stock prefix traversal is capped at 6 strings (PUBGB builds the extra cells)",
        )
    ),
    "PUBGO": Ref(
        **ruled(
            lambda c: W("regs", regs={"edx": 0xD0D0D008}) if h(c, "n") in (6, 7, 0xFF) else None,
            "a guitar (count >= 6) populates all eight world columns (native publication, shipped)",
        )
    ),
    "PUBGB": Ref(
        **ruled(
            lambda c: None if c.id == "r2" else W("regs", regs={"eax": 0}),
            "RsxrPublish_GridBuild (stubbed) answers 2, the stock status; 0 = not ready while the strings load",
        ),
        real=False,
    ),
    "PUBGA": Ref(
        **ruled(pubga, STRING6 + ": strings 6/7 go to RsxrGs_AttachExt, >= 8 return 0"), real=False, variant=returning
    ),
    "PUBEDGE": Ref(),
    "PUBGF": Ref(variant=whole_function),
    **{sid: slot_bound(sid, e) for sid, e in (("PSU1", 6), ("PSU2", "target"), ("PSM1", 0x0A))},
    "LT1": Ref(),
    "AL1": Ref(
        **ruled(
            lambda c: (
                None
                if v(c, "on") == "0"
                else W(
                    "eip",
                    "final_esp",
                    "regs",
                    "stack",
                    eip=("abs", EXPORTS["Rsxr_NifHandler"]),
                    esp=4,
                    regs={"eax": 0x12345678, "ebp": EBP, "esp": ESP},
                )
            ),
            "with the asset hook on the load continues in Rsxr_NifHandler (the game function's signature, entry stack)",
        )
    ),  # fmt: skip
    # ---- features/strings ----
    **{sid: entry(sid, "RsxrGs_Target") for sid in feat.GS_ENTRIES},
    "GRM1": Ref(),
    "GRM2": Ref(
        **ruled(
            lambda c: (
                W("regs", regs={"eax": int.from_bytes(feat.blob(0x80, 5)[0x10 * h(c, "s") + 4 :][:4], "little")})
                if "native1" in c.id
                else None
            ),
            "a native GuitarStrings object keeps main[] at +40h",
        )
    ),
}


def child_slot(c: Case):
    """CH3: strings 6/7 store the child slot byte at parent eval +0C1Eh + (string - 6); a parent without a slot (-1) gets
    nothing (stock writes the dword past its six-entry array)."""
    s, parent = int(v(c, "string")), h(c, "parent")
    if s < 6:
        return None
    first = 0xC20 + 0xC28 * (parent & 1)
    if parent == 0xFFFFFFFF:
        stock_at = 0xC0C + 0x30A * 4 + 4 * s
        return W("mem", "writes", at={"mem0": (stock_at, ns.blob(0x1900, 41)[stock_at : stock_at + 4])})
    return W("mem", "writes", at={"mem0": (first + s, b"\x42")})


REFS.update(
    {
        **{sid: record(sid, ns.T8, T8_SIZE, reg) for sid, reg in (
            ("TP1", "eax"), ("TP2", "edi"), ("TP3", "ebx"), ("TP4", "edx"), ("TP5", "edi"), ("TP6", "edx"),
            ("TP7", "ebx"), ("TP8", "edx"), ("TP11", "edx"),
        )},
        "TP12": record("TP12", ns.T8, T8_SIZE, "edi", offset=4),  # the T8's frets
        "CP1": record("CP1", ns.C8, C8_SIZE, "eax"),
        "CP2": record("CP2", ns.C8, C8_SIZE, "ebx"),
        "DS1": Ref(variant=ds1_cases),
        "LU3": Ref(
            **ruled(
                lambda c: W("ebp_area", at={"ebp_area": (0xF8, b"\xff" * 8)}),
                "targets 6/7 = -1 at EBP-8 / EBP-4, where the stock frame keeps its /GS cookie: LU1/LU2 move the "
                "cookie (a frame change over three sites; this site alone has no stock equivalent)",
            )
        ),
        "WC2": Ref(
            **ruled(
                lambda c: W(
                    "ebp_area", "regs", regs={"esi": EBP - 0x44},
                    at={"ebp_area": (0xBC, c.mem[ns.EVAL][0x284:0x2A4])}, span={"ebp_area": (0xBC, 0xF7)},
                ),
                "the matcher targets become the eval's int32[8] (+284h) copied into the frame WC1 grew (EBP-44h); the "
                "stock copy's two stores to EBP-10h / EBP-0Ch are not made",
            )
        ),
        "CS1": Ref(
            **ruled(
                lambda c: W(
                    "mem", "writes", at={"mem0": (0x29C, dword(c.regs[UC_X86_REG_EDX]) * 2)},
                    span={"mem0": (0x29C, 0xC1F)},
                ),
                "Play_Eval_ClearState also empties the N-slot tails (midi[6..7] = EDX ...) and child slots of the "
                "native eval",
            )
        ),
        "CC3": Ref(variant=with_mem(lambda c: {ns.CN8: c.mem[ns.CN8][0x948:0x968] + c.mem[ns.CN8][0x20:]})),
        "CH3": Ref(**ruled(child_slot, STRING6 + " (its child slot byte at +0C1Eh)")),
        "PL1": Ref(variant=pl1_cases),  # EAX's upper bytes were a finding until the fix (the hook zero-extended the fret)
        **{sid: latch(sid) for sid in ("UP1", "UP2", "UP3", "PD2", "PD3", "PD4", "PD5")},
        "SF1": Ref(real=False),
    }
)  # fmt: skip

# Stubbed helpers (void, or the entry dispatch answering the stock entry): the reference covers the hook's glue only.
private_features.extend(__name__, globals())
GLUE_ONLY = sorted(sid for sid, ref in REFS.items() if not ref.real)
NO_REFERENCE: dict[str, str] = {}

(
    test_hook_behaves_like_the_stock_bytes,
    test_hook_differs_from_the_stock_bytes_on_purpose,
    test_every_converted_site_has_a_stock_reference_or_a_reason,
) = stock_tests(SPECS, REFS, NO_REFERENCE)
