"""Old-vs-new equivalence and stock references of the sites converted with the sse mid-hook mode and the FCOMP m32
relocation (stubs frozen at 7f80b7b, tools/tests/fixtures/midhook_7f80b7b.json; R2's logger Rs7_DiagNoteHead was not
exported there, freeze_stubs.py --map/--local named it):
- R2 (note head lane z) and R6 (lane offset y): sse mode, string 6's extrapolated lane through Rs7Mid::LoadSt0. The
  runs end after the game's next `fstp dword` (R2 0x79E730, R6 0x7A8C30), so the float the game keeps is compared;
  the x87 precision flags the old stubs' arithmetic raised are the contract "sticky".
- W (wrong-instrument threshold): full mode, the replayed `fcomp [34.0]` on a remapped ST0 (Context::CompareSt0).
- R1 (PUBGP, Rnd_FretGrid_GetCellPos replaced whole): sse mode, Rs7Publish_GridPos and `ret 0Ch` (Rs7Mid::Ret).
What is compared: midhook_oracle.py and midhook_stock.py.
"""

from __future__ import annotations

import math
import struct

import pytest
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDI, UC_X86_REG_EDX

from emu_harness import DATA, DLL, SNAPSHOT
from midhook_oracle import Case, Helper, Spec, oracle_tests
from midhook_stock import RET_AT, Ref, W, ruled, stock_tests

pytestmark = pytest.mark.skipif(
    not DLL.exists() or not (SNAPSHOT / "memory.bin").exists(), reason="needs built DLL and snapshot"
)

D = DATA + 0x2000  # scratch objects
ROW = D + 0x100  # a lane cell row: string s's cell at ROW + s * 4Ch
CELL = 0x4C
SPECS: dict[str, tuple[Spec, list[Case]]] = {}
EBP_AREA = 0x100  # EBP - 0x100 is the first byte of the oracle's compared EBP area


def register(spec: Spec, cases: list[Case]):
    SPECS[spec.sid] = (spec, cases)


def f32(x: float) -> bytes:
    """float32 bits of x, rounded to nearest (an overflow is infinity, as the x87 / SSE store with masked overflow)."""
    try:
        return struct.pack("<f", x)
    except OverflowError:
        return struct.pack("<f", math.copysign(math.inf, x))


# (string 0, string 1) coordinates: exact, inexact (the x87 precision flag), overflow to infinity, signed zeros. No
# denormals: an x87 load of one raises DE, which the "sticky" contract does not cover (lane coordinates never are).
LANES = ((1.5, 0.75), (0.1, 0.7), (-3.3, 1e-3), (3e38, -3e38), (-0.0, 0.0), (1e30, 3e30))


def lane_row(at: int, cell0: float, cell1: float) -> bytes:
    """A row with string 0 / 1 coordinates at `at` / `at + 4Ch` and distinct stock values for strings 2-7."""
    raw = bytearray(bytes((i * 29 + 5) & 255 for i in range(0x300)))
    for s in range(8):
        struct.pack_into("<f", raw, at + s * CELL, (cell0, cell1)[s] if s < 2 else 100.0 + s)
    return bytes(raw)


def lane(case: Case) -> tuple[float, float]:
    return LANES[int(case.id.split("-v")[1].split("-")[0])]


# ---- R2: `add edx,[ebp+1Ch]; fld dword [ebx+edx+44h]`, EBX = string * 4Ch; D1 logger with [debug] diagnostics -------
def r2_case(string: int, v: int, diag: int, carry: bool) -> Case:
    edx = 0x80000000 if carry else 8  # the `add` wraps (CF, OF) or not
    return Case(
        f"s{string}-v{v}-d{diag:x}-c{int(carry)}",
        regs={UC_X86_REG_EBX: string * CELL, UC_X86_REG_EDX: edx},
        mem={ROW: lane_row(0x44, *LANES[v])},
        ebp={0x1C: (ROW - edx) & 0xFFFFFFFF, 0x10: 0xAB01, 0x14: 0xCD00 | string, 0x18: 0xEF07, -0x30: 0xC09FAE14},
        words={"g_rs7_diag": diag},
    )


register(
    Spec(
        "R2",
        7,
        helpers={"Rs7_DiagNoteHead": Helper(7, deref=((3, 0x94),))},
        exits=(("resume", 6),),  # after `mov ebx,[ebp-10h]; fstp dword [ebp-24h]`
        fsw="sticky",
    ),
    [
        r2_case(s, v, d, c)
        for s in (0, 1, 5, 6, 7)
        for v in range(len(LANES))
        for d in (0, 1, 0x80000000)
        for c in (False, True)
        if c or v < 2
    ],
)

# ---- R6: `add eax,edi; fld dword [ecx+eax+40h]`, ECX = string * 4Ch ----------------------------------------------------
register(
    Spec("R6", 6, exits=(("resume", 3),), fsw="sticky"),  # after `fstp dword [ebp-0Ch]`
    [
        Case(
            f"s{s}-v{v}-c{int(c)}",
            regs={UC_X86_REG_ECX: s * CELL, UC_X86_REG_EAX: 0x80000000 if c else 0x10, UC_X86_REG_EDI: 0},
            mem={ROW: lane_row(0x40, *LANES[v])},
        )
        for s in (0, 3, 6, 7)
        for v in range(len(LANES))
        for c in (False, True)
    ],
)
for _case in SPECS["R6"][1]:  # EAX + EDI = ROW (with a wrapping add for c1)
    _case.regs[UC_X86_REG_EDI] = (ROW - _case.regs[UC_X86_REG_EAX]) & 0xFFFFFFFF

# ---- W: `fcomp dword [1224C6Ch]` (34.0), ST0 = the mean, ESI = session; the session's threshold with lowrange -------
MEAN = D + 0x40
K34 = 0x8800000000000000  # 34.0: mantissa, exponent 4004h
MEANS = {  # name -> (mantissa, sign | exponent) of the 80-bit mean
    "29": (0xE800000000000000, 0x4003),
    "33": (0x8400000000000000, 0x4004),
    "34": (K34, 0x4004),
    "34+ulp": (K34 + 1, 0x4004),  # rounds to 34.0 as a double: the remap must compare exactly
    "34-ulp": (K34 - 1, 0x4004),
    "35.5": (0x8E00000000000000, 0x4004),
    "nan": (0xC000000000000000, 0x7FFF),
    "-inf": (0x8000000000000000, 0xFFFF),
}
THRESHOLDS = {"34": 0x42080000, "30": 0x41F00000, "35.5": 0x420E0000}
SWITCHES = ((1, 1), (0, 1), (1, 0), (2, 0x80000000))  # (lowrange, tuner_any)


def w_case(mean: str, thr: str, switches: tuple[int, int]) -> Case:
    return Case(
        f"m{mean}-t{thr}-l{switches[0]:x}-a{switches[1]:x}",
        regs={UC_X86_REG_EAX: 0x1111},
        mem={MEAN: struct.pack("<QH", *MEANS[mean])},
        words={"g_rs7_lowrange": switches[0], "g_rs7_tuner_any": switches[1], "g_rs7_wrong_thr": THRESHOLDS[thr]},
        x87=bytes.fromhex("DB 2D") + struct.pack("<I", MEAN),  # fld tbyte [MEAN]
    )


register(
    # the old stub's flags are its `cmp` / helper leftovers; `test ah,41h` at the resume rewrites them
    Spec(
        "W",
        6,
        helpers={"Rs7_WrongThreshold": Helper(1)},
        flags="dead",
        words={"g_rs7_wrong_const": 0x1224C6C},  # X_WTHR, as Rs7_Startup binds it
    ),
    [w_case(m, t, s) for m in MEANS for t in THRESHOLDS for s in SWITCHES if t == "34" or s == (1, 1)],
)

# ---- R1 (PUBGP): Rnd_FretGrid_GetCellPos entry (AL = fret, ECX = out, stack: grid, k, string; `ret 0Ch`) -----------
register(
    Spec(
        "R1",
        6,
        stub="Rs7_Stub_PUBGP",
        helpers={"Rs7Publish_GridPos": Helper(5)},
        flags="dead",  # the old stub's `add esp,20`; a return (flags are not part of the convention)
        exits=(RET_AT,),
    ),
    [
        Case(
            f"r{r}-s{s}",
            regs={UC_X86_REG_EAX: 0x12345600 | 7, UC_X86_REG_ECX: D + 0x600, UC_X86_REG_EDX: 0xD0D0},
            stack={0: RET_AT, 4: D, 8: 2, 12: 0xABCDEF00 | s},
            rets={"Rs7Publish_GridPos": r},
        )
        for r in (0, 1)
        for s in (0, 6, 7)
    ],
)

ALWAYS_CALLS: set[str] = {"R1"}
(
    test_mid_hook_matches_frozen_stub,
    test_dead_flags_are_rewritten_before_any_read,
    test_every_registered_site_calls_its_helper_at_least_once,
) = oracle_tests(SPECS, ALWAYS_CALLS)


def lane_store(offset: int):
    """String 6: the float the game stores after the site ([ebp - offset]) is the extrapolated lane, not stock's
    (out-of-row) string-6 read; nothing else differs."""

    def expect(case: Case):
        if case.regs.get(UC_X86_REG_EBX, case.regs.get(UC_X86_REG_ECX)) != 6 * CELL:
            return None
        cell0, cell1 = (struct.unpack("<f", f32(x))[0] for x in lane(case))  # the stored float32 cells
        return W("ebp_area", at={"ebp_area": (EBP_AREA - offset, f32(2.0 * cell0 - cell1))})

    return expect


REFS: dict[str, Ref] = {
    # stubbed logger: the real one needs the log file (Win32 imports the emulator lacks); it observes only
    "R2": Ref(**ruled(lane_store(0x24), why="string 6: the extrapolated lane z, stock has six cells"), real=False),
    "R6": Ref(**ruled(lane_store(0x0C), why="string 6: the extrapolated lane y, stock has six cells")),
    # Rs7_WrongThreshold for real on the default state (no session registered): the stock threshold 34
    "W": Ref(),
}
NONE = {
    "R1": "PUBGP replaces the whole function with the native-cell lookup (test_publish_grid_emu.py checks its answers)",
}
(
    test_hook_behaves_like_the_stock_bytes,
    test_hook_differs_from_the_stock_bytes_on_purpose,
    test_every_converted_site_has_a_stock_reference_or_a_reason,
) = stock_tests(SPECS, REFS, NONE)


def test_r2_r6_lane_cases_reach_string_six_with_every_value_set():
    """The matrix is not vacuous: string 6 runs with every lane value set (inexact and overflowing ones included)."""
    for sid, register_ in (("R2", UC_X86_REG_EBX), ("R6", UC_X86_REG_ECX)):
        sixes = {lane(c) for c in SPECS[sid][1] if c.regs[register_] == 6 * CELL}
        assert sixes == set(LANES), sid
