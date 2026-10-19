"""The calls written into the native GuitarStrings copies (call-in entries, features/strings/hooks_callin.cpp) against
the naked helpers they replaced (frozen at 304b9d3, tools/tests/fixtures/midhook_304b9d3.json) and against the stock
bytes of the edit (six-string cases). Oracle: callin_oracle.py. Rs7_Call_GlowRead (an x87 result, Sse mode) is frozen at
06733ba (tools/tests/fixtures/midhook_06733ba.json).
"""

from __future__ import annotations

import struct

import pytest
from unicorn import UcError
from unicorn.x86_const import (
    UC_X86_REG_EAX,
    UC_X86_REG_EBX,
    UC_X86_REG_ECX,
    UC_X86_REG_EDI,
    UC_X86_REG_EDX,
    UC_X86_REG_ESI,
)

from callin_oracle import Case, Spec, check, flags_dead, matrix
from emu_harness import CALLINS, DATA, DLL, SNAPSHOT, Emu

pytestmark = pytest.mark.skipif(
    not DLL.exists() or not (SNAPSHOT / "memory.bin").exists(), reason="needs built DLL and snapshot"
)

D = DATA + 0x2000  # scratch objects
PATTERN = bytes((i * 13 + 5) & 255 for i in range(0x1400))
SPECS: dict[str, tuple[Spec, list[Case]]] = {}


def register(spec: Spec, cases: list[Case]):
    SPECS[spec.name] = (spec, cases)


# ---- TP2 (GSDM 0x4608AC): EBX = template vector begin, EDI = id -> EDI = the record, EDX = id * 9 -------------------
T8 = DATA + 0x8000


def ns(*entries) -> bytes:
    blob = b"".join(struct.pack("<8I", *e, *(0,) * (8 - len(e))) for e in entries)
    return blob + bytes(64 * 32 - len(blob))


register(
    Spec("TP2", "Rs7_Call_TP2", 0x4608AC, 6, flags="dead", frozen=("Rs7_NsTplAt",)),
    [
        Case("unregistered", regs={UC_X86_REG_EBX: D, UC_X86_REG_EDI: 3}, data={"g_rs7_ns": ns()}),
        Case("other-vector", regs={UC_X86_REG_EBX: D, UC_X86_REG_EDI: 3}, data={"g_rs7_ns": ns((D + 8, 9, T8))}),
        Case("past-count", regs={UC_X86_REG_EBX: D, UC_X86_REG_EDI: 9}, data={"g_rs7_ns": ns((D, 9, T8))}),
        Case("t8", regs={UC_X86_REG_EBX: D, UC_X86_REG_EDI: 4}, data={"g_rs7_ns": ns((D, 9, T8))}, stock=False),
        Case(
            "t8-second",
            regs={UC_X86_REG_EBX: D, UC_X86_REG_EDI: 0},
            data={"g_rs7_ns": ns((D + 8, 9, T8 + 0x100), (D, 2, T8))},
            stock=False,
        ),
    ],
)

# ---- GlowSetup (GSGL 0x544243, EBX = renderer): ESI = [EBX+4] + 80h, EDI = -ESI. The stock lea alone (its neighbour
# edits are NOPs) is not comparable: no stock case.
register(
    Spec("GlowSetup", "Rs7_Call_GlowSetup", 0x544243, 6, flags="dead"),
    [
        Case(f"gs{i}", regs={UC_X86_REG_EBX: D}, mem={D: struct.pack("<II", 0, gs)}, stock=False)
        for i, gs in enumerate((D + 0x400, 0xFFFFFF90))
    ],
)

# ---- GlowColour (GSGL 0x544384): ECX = colour manager, EAX = mode * 0A8h + EDX, EDX = mesh index -> EAX = the RGB ----
register(
    Spec(
        "GlowColour",
        "Rs7_Call_GlowColour",
        0x544384,
        7,
        flags="dead",
        helpers={"Rs7_GlowFixPtr": 3},
        local={"Rs7_GlowFixPtr": "Rs7_CallIn_GlowColour"},  # not exported
    ),
    [
        Case(
            f"m{mode}-s{s}-native{native}-fix{fix}",
            regs={UC_X86_REG_ECX: D, UC_X86_REG_EAX: mode * 0xA8 + s, UC_X86_REG_EDX: s},
            mem={D: PATTERN},
            words={"g_rs7_native_colours": native, "g_rs7_glow_fix": fix},
            rets={"Rs7_GlowFixPtr": 0xF1F1F1F1},
            stock=not native and not fix and s < 6,
        )
        for mode in (0, 1)
        for s in (0, 5, 6, 7)
        for native in (0, 1)
        for fix in (0, 1)
    ],
)

# ---- GlowRead (GSGL 0x5443B3 `fld dword [ebx+edx*4+0ACh]`, EBX = renderer): ST0 = glow[EDX] at [EBX+4] + 80h. With
# [EBX+4] = EBX + 2Ch the native glow is the stock address: a stock case.
GLOW = (
    0.125,
    0.25,
    0.5,
    0.75,
    1.0,
    -1.5,
    3.0e-39,
    1234.5,
)  # a denormal among them; none 0.0 (the stock slot of a native case)
register(
    Spec("GlowRead", "Rs7_Call_GlowRead", 0x5443B3, 7),
    [
        Case(
            f"s{s}-{'stock' if at == D + 0x2C else 'native'}",
            regs={UC_X86_REG_EBX: D, UC_X86_REG_EDX: s},
            # the stock slots zeroed first (a native case: earlier cases leave memory behind)
            mem={D: struct.pack("<II", 0, at), D + 0xAC: bytes(32), at + 0x80: struct.pack("<8f", *GLOW)},
            stock=at == D + 0x2C,
        )
        for s in range(8)
        for at in (D + 0x2C, D + 0x400)
    ],
)

# ---- HsLoaded (GSHC 0x58E516): ECX = s, ESI = headstock, EDI = guitar -> the flags of "loaded" ---------------------
register(
    Spec("HsLoaded", "Rs7_Call_HsLoaded", 0x58E516, 5, helpers={"Rs7Gs_SegHandle": 2}),
    [
        *(
            Case(
                f"s{s}-flag{flag}",
                regs={UC_X86_REG_ECX: s, UC_X86_REG_ESI: D, UC_X86_REG_EDI: D + 0x200},
                mem={D + 0x18 + s: bytes([flag])},
            )
            for s in (0, 3, 5)
            for flag in (0, 1, 0x80)
        ),
        *(
            Case(
                f"s{s}-seg{bool(h)}",
                regs={UC_X86_REG_ECX: s, UC_X86_REG_ESI: D, UC_X86_REG_EDI: D + 0x200},
                rets={"Rs7Gs_SegHandle": h},
                stock=False,
            )
            for s in (6, 7)
            for h in (0, D + 0x500, 0x80000000)
        ),
    ],
)

# ---- HsSeg (GSHC 0x58E52D): ECX = s, ESI = headstock -> ECX = the segment's colour param ---------------------------
register(
    Spec("HsSeg", "Rs7_Call_HsSeg", 0x58E52D, 7, flags="dead", helpers={"Rs7Gs_SegHandle": 2}),
    [
        *(
            Case(
                f"s{s}",
                regs={UC_X86_REG_ECX: s, UC_X86_REG_ESI: D, UC_X86_REG_EDI: D + 0x200},
                mem={
                    D + 0x20 + 4 * s: struct.pack("<I", D + 0x300 + 0x10 * s),
                    D + 0x30C + 0x10 * s: struct.pack("<I", 0xC010 + s),
                },
            )
            for s in (0, 5)
        ),
        *(
            Case(
                f"s{s}-seg{bool(h)}",
                regs={UC_X86_REG_ECX: s, UC_X86_REG_ESI: D, UC_X86_REG_EDI: D + 0x200},
                rets={"Rs7Gs_SegHandle": h},
                stock=False,
            )
            for s in (6, 7)
            for h in (0, D + 0x500)
        ),
    ],
)

# ---- Attach (GSLD 0x59A5E9 `call 0x594530`, ECX = info, cdecl(s, &vec3) -> AL) -------------------------------------
ATTACH, SHAPE = 0x594530, 0x5996A0
FCW_SEEN = DATA + 0x7F00  # where the attach stand-in stores its x87 control word (a compared write)
register(
    Spec(
        "Attach",
        "Rs7_Call_Attach",
        0x59A5E9,
        5,
        flags="free",
        clobbers=("ecx", "edx"),  # after a call; the old helper left Rs7Gs_AttachExt's
        helpers={"Rs7Gs_AttachExt": 3},
        game_fcw=("Rs7Gs_AttachExt",),  # it calls the game's attach query
        # fnstcw [FCW_SEEN]; mov eax,[esp+4]; add eax,ecx; ret: the game function records its control word
        standins={ATTACH: "D9 3D " + struct.pack("<I", FCW_SEEN).hex(" ") + " 8B 44 24 04 03 C1 C3"},
        words={"g_rs7_fn_attach": ATTACH},
    ),
    [
        Case(
            f"s{s:x}",
            regs={UC_X86_REG_ECX: D},
            stack={0: s, 4: D + 0x400},
            rets={"Rs7Gs_AttachExt": 1},
            stock=(s & 0xFF) < 6,
        )
        for s in (0, 5, 0x105, 6, 7, 0x306)
    ],
)

# ---- Shape (GSLD 0x59A15B `call 0x5996A0`, stdcall(obj, info, s)); strings 6/7 return through ShapeDone ------------
register(
    Spec(
        "Shape",
        "Rs7_Call_Shape",
        0x59A15B,
        5,
        flags="free",
        clobbers=("ecx", "edx"),  # after a call; the old helper left Rs7Gs_ShapeDone's
        helpers={"Rs7Gs_ShapeDone": 2},
        standins={SHAPE: "8B 44 24 04 03 44 24 0C C2 0C 00"},  # mov eax,[esp+4]; add eax,[esp+0Ch]; ret 0Ch
        words={"g_rs7_fn_shape": SHAPE},
        entries={"g_rs7_callin_shape_ret": "ShapeDone"},
    ),
    [
        Case(f"s{s:x}", stack={0: D + 0x600, 4: D + 0x700, 8: s}, stock=(s & 0xFF) < 6)
        for s in (0, 5, 0x105, 6, 7, 0x207)
    ],
)

MACHINES: dict = {}


@pytest.mark.parametrize("name,case_id", matrix(SPECS, "old"))
def test_callin_matches_the_frozen_helper(name, case_id):
    check(MACHINES, SPECS, name, case_id, "old")


@pytest.mark.parametrize("name,case_id", matrix(SPECS, "stock"))
def test_callin_matches_the_stock_bytes_on_six_strings(name, case_id):
    check(MACHINES, SPECS, name, case_id, "stock")


@pytest.mark.parametrize("name", [n for n, (s, _) in SPECS.items() if s.flags != "exact"])
def test_dead_flags_are_dead_at_the_stock_continuation(name):
    assert flags_dead(SPECS[name][0])


def test_every_native_case_differs_from_stock():
    """The cases marked not-stock exercise the native path: the new run differs from the stock bytes."""
    from callin_oracle import FLAG_PATTERNS, Machine, diff

    for name, (spec, cases) in SPECS.items():
        if spec.name == "GlowSetup":
            continue  # no stock reference (its neighbour edits are NOPs)
        machine = MACHINES.setdefault(name, Machine(spec))
        for case in (c for c in cases if not c.stock):
            got = machine.run("new", case, FLAG_PATTERNS[0], 0)
            try:
                want = machine.run("stock", case, FLAG_PATTERNS[0], 0)
            except UcError:
                continue  # the stock bytes fault on the added string (HsSeg: no seventh segment pointer)
            assert diff(spec, want, got, FLAG_PATTERNS[0]), (name, case.id)


@pytest.mark.parametrize("name", [n for n, (_, mode, _) in CALLINS.items() if mode])
def test_light_and_sse_callins_use_no_fp(name):
    """Every light call-in the DLL defines (CALLINS: read from the C++) uses no x87/MMX/SSE, every sse one no x87/MMX."""
    from test_midhook_emu import _fp_instructions

    emu = Emu(False)
    callback, mode, _ = CALLINS[name]
    assert _fp_instructions(emu, emu.exports[callback], sse=mode == 2) == []


def test_callins_are_read_from_every_definition():
    """CALLINS parses each `Rs7Mid::CallIn g_rs7_callin_<name>{...};` definition; one written differently is missed."""
    import re

    from emu_common import REPO

    defs = sum(
        len(re.findall(r"^Rs7Mid::CallIn g_rs7_callin_\w+\{", p.read_text(encoding="utf-8"), re.M))
        for p in (REPO / "src").rglob("*.cpp")
    )
    assert defs == len(CALLINS)


def test_every_strings_callin_has_a_spec():
    """Each call-in of the GuitarStrings copies is defined (ShapeDone: Shape's return entry); that every call-in has a
    spec: test_midhook_callin2.py."""
    assert set(SPECS) | {"ShapeDone"} <= set(CALLINS)
