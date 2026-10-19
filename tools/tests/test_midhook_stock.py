"""Stock reference of the converted mid-hook sites (cold, x87 / atomic and call sites): the game's original bytes against
the hooked path (midhook_stock.py: stock_differences, Ref, stock_tests). The frozen-stub comparisons (test_midhook_cold.py,
test_midhook_x87.py, test_midhook_calls.py) prove "unchanged"; this proves "like the game", the check that would have
caught PUBGE2 (test_publish_grid_emu.py). The cases are those modules' cases; where the stock run needs another input
(both layouts of a relocated record, a return address outside the site) a variant says so.

Each site is one rule: `expect(case)` is None where the hook must behave exactly like the stock bytes (six-string
input), otherwise the exact intended difference (W: which observables differ and the hook's values). Helpers stubbed
(`real=False`) are void here, so those references check the hook's registers, stack, exits and memory, not the helpers'
own effects. The feature and N-slot sites: test_midhook_stock_features.py.
"""

from __future__ import annotations

import dataclasses
import math
import re
import struct

import pytest
from unicorn.x86_const import UC_X86_REG_EAX as EAX_REG
from unicorn.x86_const import UC_X86_REG_ECX as ECX_REG
from unicorn.x86_const import UC_X86_REG_ESI as ESI_REG

import test_midhook_calls as calls
import test_midhook_cold as cold
import test_midhook_x87 as x87
from emu_common import dll_image, exe_sections
from emu_harness import DLL, SNAPSHOT, site_target
from midhook_oracle import ESP, Case, Helper, Spec, Sym
from midhook_stock import Ref, W, returning_elsewhere, ruled, stock_tests

pytestmark = pytest.mark.skipif(
    not DLL.exists() or not (SNAPSHOT / "memory.bin").exists(), reason="needs built DLL and snapshot"
)


def v(case: Case, key: str) -> str:
    """The value of `key` in a case id such as `count6-ports0-s71` (the frozen-stub modules' naming)."""
    match = re.search(rf"(?:^|-){key}([0-9a-fx]+)(?:-|$)", case.id)
    assert match, (key, case.id)
    return match.group(1)


def h(case: Case, key: str) -> int:
    return int(v(case, key), 16)


def dword(value: int) -> bytes:
    return struct.pack("<I", value & 0xFFFFFFFF)


def with_mem(extra) -> object:
    """variant: every case gets the memory `extra(case)` returns as well (the stock layout of a relocated record)."""

    def variant(spec, cases):
        return spec, [dataclasses.replace(c, mem={**c.mem, **extra(c)}) for c in cases]

    return variant


def game_dword(address: int) -> int:
    """A dword of the game image (.rdata / .data of the unpacked exe)."""
    for base, data in exe_sections():
        if base <= address < base + len(data):
            return struct.unpack_from("<I", data, address - base)[0]
    raise AssertionError(hex(address))


def f32(value: float) -> float:
    try:
        return struct.unpack("<f", struct.pack("<f", value))[0]
    except OverflowError:
        return math.inf


def significand(value: float) -> int:
    """The 64-bit x87 significand of a nonzero finite value (Unicorn reads back only that part of ST0)."""
    return int(abs(math.frexp(value)[0]) * 2**64)


EXPORTS = dll_image()[3]
SPECS = {**cold.SPECS, **x87.SPECS, **calls.SPECS}
STRING6 = "string 6 (a seven-string input: stock has no string 6) takes the seven-string path"
NATIVE_PEGSET = (
    "a PegSet in g_rs7_pegsets is a native capacity-8 PegSet (every PegSet is one after PSC1 unless the table is full): "
    "its sizes, counts and bounds are the native ones"
)
OWNED_SLOT = {"owned-first": 0, "owned-middle": 17, "owned-last": 63, "table-full-owned": 33}


def pool_count(c: Case):
    """N1 / N21: a 6/7-string profile requests the native capacity."""
    n, ports, s7 = v(c, "count"), v(c, "ports") != "0", v(c, "s7") != "0"
    if n in ("6", "7") and (ports or (n == "6" and s7)):
        return 8 if ports else 7
    return None


def bend_guard(sid: str):
    """BD1-BD4: string 6 reads string 0's perStringMode and joins the stock body after its own lookup."""

    def expect(c: Case):
        if v(c, "string") != "6":
            return None
        mode = h(c, "mode")
        if sid == "BD1":
            return W("eip", "regs", eip=0x0F, regs={"edx": 6}) if mode == 0 else None
        if sid == "BD2":
            return W("eip", "regs", eip=0x13, regs={"ecx": mode, "edx": 6})
        if sid == "BD3":
            return W("eip", "regs", eip=0x1C, regs={"eax": 6, "ecx": cold.BGUITAR, "edx": cold.BINFO, "edi": mode})
        return W(
            "ebp_area",
            "eip",
            "regs",
            eip=0x25,
            regs={"eax": mode, "ecx": 6, "edx": cold.BINFO},
            at={"ebp_area": (0xE8, dword(mode))},
        )

    return expect


def gs_start(c: Case):
    """GSSS on a native GuitarStrings object: the dimming pass covers the loaded count (8 slots)."""
    if not (v(c, "native") == "1" and v(c, "info") == "1" and v(c, "loaded") == "1"):
        return None
    count, stock = int(v(c, "count")), v(c, "stock")
    if stock == "0":
        return W("mem", "writes") if count else None
    if not count:
        return W("eip", "regs", eip=0x19, regs={"eax": 0})
    return W("eip", "mem", "regs", "writes", eip=0x19, regs={"eax": count, "esi": cold.STR + cold.OBJ * (count - 1)})


def gs_end(c: Case):
    """GSSE on a native GuitarStrings object: the pass runs over all 8 slots."""
    if v(c, "native") != "1":
        return None
    bl, stock = h(c, "bl"), int(v(c, "stock"))
    if bl < stock:  # the stock loop would continue: the hook finished it
        last = 0 if c.id.endswith("lastnull") else cold.STR + cold.OBJ * 7
        return W("eip", "mem", "regs", "writes", eip=0x16, regs={"eax": 8, "edi": last})
    return W("mem", "regs", "writes", regs={"eax": 8})


def lane(sid: str):
    """R3a-R3c / R4: string 6's lane is extrapolated into the scratch vec3: EAX = the scratch, ST0 = its x."""

    def expect(c: Case):
        if not c.id.startswith(f"row{6 * x87.ROWS:x}-"):
            return None
        return W("fpu", "regs", regs={"eax": EXPORTS[f"g_rs7_scratch_{sid}"]})

    return expect


def lane_st0(sid: str):
    """The check of `lane` plus the ST0 value with two free x87 slots (a full stack overflows on the replayed fld)."""
    expect = lane(sid)
    base = ruled(expect, "")["check"]

    def check(differences, case, machine):
        base(differences, case, machine)
        (x0, *_), (x1, *_) = x87.LANE_VALUES[int(v(case, "values"))]
        x = f32(2 * x0 - x1)  # the scratch is float32
        if case.id.endswith("depth2") and math.isfinite(x) and x:
            assert differences["fpu"][1][0] == significand(x), (case.id, hex(differences["fpu"][1][0]), x)

    return check


def mask_test(sid: str):
    """TM1-TM6: strings 6/7 test C8 mask[6..7] (stock: 0 for string >= 6); EAX = the bit, EDX = mask & flag where the
    continuation reads them."""

    def expect(c: Case):
        s, flag = int(v(c, "s")), h(c, "f")  # the string is decimal in these ids
        if not 6 <= s < 8:
            return None  # stock strings, and above the eight slots both answer 0
        hit = struct.unpack_from("<8I", calls.MASK8)[s] & flag
        if sid == "TM2":  # only AL is live there
            return W("regs", regs={"eax": 1}) if hit else None
        if sid == "TM5":
            return W("regs", regs={"eax": int(bool(hit))})
        return W("regs", regs={"eax": int(bool(hit)), "edx": hit})

    return expect


def psn1(spec, cases):
    """PSN1 reads g_rs7_pegnames, which startup binds to the game's own table (ref site X_PEGNAMES)."""
    table = site_target("X_PEGNAMES")
    return dataclasses.replace(spec, words={"g_rs7_pegnames": table}), [
        dataclasses.replace(c, words={"g_rs7_pegnames": table}, mem={}) for c in cases
    ]


def psr1_cases(spec, cases):
    """PSR1 replaces the loop bound guitar info +0Bh (stock: session+2Ch -> +24h -> +0Bh) by the PegSet's active count
    (session+2Ch -> +18h -> +0Ch -> +4): both chains; a stock guitar / bass, and a native PegSet with 7 active pegs."""
    info = cold.D + 0x3380
    out = []
    for count, active in ((6, 6), (4, 4), (6, 7)):
        mem = {
            cold.SESS: bytes(0x2C) + dword(cold.PA),
            cold.PA: bytes(0x18) + dword(cold.PB) + bytes(8) + dword(info),
            cold.PB: bytes(0x0C) + dword(cold.PP),
            cold.PP: bytes(4) + struct.pack("<H", active) + bytes(6),
            info: bytes(0x0B) + bytes([count]) + bytes(4),
        }
        out.append(dataclasses.replace(cases[0], id=f"count{count}-active{active}", mem=mem))
    return spec, out


def psc1_cases(spec, cases):
    """PSC1 plus a full g_rs7_pegsets, the one case where the constructor keeps the stock layout."""
    full = {Sym("g_rs7_pegsets"): cold.peg_table(dict(cold.OTHERS))}
    return spec, [*cases, dataclasses.replace(cases[0], id="table-full", mem={**cases[0].mem, **full})]


# ---- the SE pilot (its frozen stubs are test_midhook_pilot.py's own fixture): song-end diagnostics ----
for sid, length in (("SE1", 6), ("SE2", 6), ("SE3", 8)):
    SPECS[sid] = (
        Spec(sid, length, helpers={"Rs7End_OnContext": Helper(2)}),
        [
            Case(
                f"log{s:x}",
                regs={ECX_REG: calls.D, ESI_REG: calls.D},  # SE3 replays `mov edx,[esi]; mov eax,[edx+0B0h]`
                mem={calls.D: dword(calls.D + 0x100)},
                words={"g_rs7_endlog": s},
            )
            for s in (0, 1, 0x80000000)
        ],
    )

REFS: dict[str, Ref] = {
    # ---- cold: tuner, SNG load (void helpers stubbed: they read live tuner / stream objects) ----
    "TK": Ref(real=False),
    "TS": Ref(),
    "TUT": Ref(real=False),
    "XB1": Ref(real=False),
    # ---- pool counts and free sizes ----
    **{
        sid: Ref(
            **ruled(
                lambda c, r=reg: W("regs", regs={r: pool_count(c)}) if pool_count(c) else None,
                "a 6/7-string profile requests the native capacity: 8 with [publish] ports, 7 with the string-7 object "
                "(an allocation size, not game behaviour)",
            )
        )
        for sid, reg in (("N1", "eax"), ("N21", "ecx"))
    },
    "GSZ1": Ref(
        **ruled(
            lambda c: (
                W("mem", "stack", "writes", at={"stack": (8, dword(0xB0))}, span={"mem0": (0x3A, 0xAF)})
                if c.id == "marker3752-magic47375352"
                else None
            ),
            "the native GuitarStrings object (marker 3752h, magic RS7G) is freed with its own size B0h",
        )
    ),
    "GIZ1": Ref(),  # EAX differs, but is dead at the allocator's indirect call (regs_live_at)
    "PSA2": Ref(
        **ruled(
            lambda c: (
                W("mem", "stack", at={"stack": (0, dword(0x1D4)), "mem0": (4 * OWNED_SLOT[c.id], bytes(4))})
                if c.id in OWNED_SLOT
                else None
            ),
            NATIVE_PEGSET + ": freed with 1D4h, its g_rs7_pegsets entry cleared",
        )
    ),
    "PSD1": Ref(
        **ruled(
            lambda c: W("ebp_area", at={"ebp_area": (0xFC, dword(7))}) if c.id in OWNED_SLOT else None,
            NATIVE_PEGSET + ": the destructor's peg count [ebp-4] is 7",
        )
    ),
    "PSD2": Ref(
        **ruled(
            lambda c: W("regs", regs={"edi": c.regs[EAX_REG] + 0x154}) if c.id in OWNED_SLOT else None,
            NATIVE_PEGSET + ": the destructor walks the native array at +154h (stock +104h)",
        )
    ),
    "PSU3": Ref(
        **ruled(
            lambda c: W("regs", regs={"ecx": 8}) if c.id in OWNED_SLOT else None, NATIVE_PEGSET + ": 8 pegs unloaded"
        )
    ),
    "PSM2": Ref(
        **ruled(
            lambda c: W("eip", eip="target") if "owned" in c.id and v(c, "bl") in ("6", "7") else None,
            NATIVE_PEGSET + ": the mesh latch runs to 8",
        )
    ),
    "PSC1": Ref(
        **ruled(
            lambda c: None if c.id == "table-full" else W("mem", "writes", span={"mem0": (0xFC, 0x1D3)}),
            "Rs7_PegSetCtor makes every PegSet it can register native (its tail +0FCh..+1D3h); a full g_rs7_pegsets "
            "keeps the stock layout",
            variant=psc1_cases,
        )
    ),
    "PSL1": Ref(
        **ruled(
            lambda c: (
                W("regs", regs={"eax": h(c, "load") & 0xFFFF})
                if "owned-middle" in c.id and v(c, "count") == "6" and v(c, "head") != "0"
                else None
            ),
            NATIVE_PEGSET + " ([neck] headstock_peg loads g_rs7_peg_load pegs; the count is read as a word)",
        )
    ),
    "PSR1": Ref(
        **ruled(
            lambda c: W("regs", regs={"eax": 7}) if c.id == "count6-active7" else None,
            NATIVE_PEGSET + " (7 active pegs on a 6-string guitar)",
            variant=psr1_cases,
        )
    ),
    "PSN1": Ref(
        **ruled(
            lambda c: (
                W("regs", regs={"ecx": game_dword(site_target("X_PEGNAMES"))})
                if h(c, "index") >= 6 and h(c, "index") * 4 & 0xFFFFFFFF
                else None
            ),
            "indices >= 6 (the 7th peg) take name 0; stock reads past its six names (0x80000000 wraps to name 0 in "
            "stock too)",
            variant=psn1,
        )
    ),
    "GSSS": Ref(**ruled(gs_start, "a native GuitarStrings object (capacity 8)")),
    "GSSE": Ref(**ruled(gs_end, "a native GuitarStrings object (capacity 8)")),
    **{sid: Ref(**ruled(bend_guard(sid), STRING6)) for sid in ("BD1", "BD2", "BD3", "BD4")},
    "D5": Ref(real=False),
    "B1": Ref(),
    "N12": Ref(
        **ruled(
            lambda c: W("eip", eip=5) if v(c, "string") == "6" and v(c, "on") != "0" else None,
            STRING6 + " ([neck] string7_line: on to the stock write)",
        )
    ),
    "N13": Ref(),
    "PM0": Ref(),
    "PR0": Ref(),
    "PM1": Ref(),
    "TF1": Ref(
        **ruled(
            lambda c: W("regs", regs={"ebx": 0xB1B1B105}) if h(c, "count") > 6 else None,
            "the camera's reference string is min(count, 6) - 1: a 7/8-string guitar keeps string 5",
        )
    ),
    "V2": Ref(
        **ruled(
            lambda c: (
                W("regs", regs={"eax": 0x40000000 + h(c, "al") * 0x264 + 6 * 0x4C + 0x3C})
                if v(c, "row") == "264" and v(c, "al") != "0"
                else None
            ),
            "the native FretGrid row stride 264h (8 cells) instead of 1CCh",
        )
    ),
    "N5": Ref(),
    **{sid: Ref() for sid in ("N18", "N22", "N19", "N20", "N23")},
}


def i1(c: Case):
    """I1 (Rs7_Base6 stubbed): 80000000h = no active seven-string song, the six-string answer; 37 = the base of one."""
    if v(c, "base") == "80000000":
        return None
    if v(c, "string") == "6" and v(c, "key") != "0" and v(c, "instr") != "2":
        regs = {"eax": 50 - 37, "ecx": 50 - 37, "ebp": 0x5EB05EB0, "esp": ESP + 8}  # `pop ebp; ret` with MIDI - base
        return W("eip", "final_esp", "regs", "stack", eip="target", esp=8, regs=regs)
    return W()  # a seven-string answer the site does not consult (no string 6, no key, bass): stock path


REFS.update(
    {
        "I1": Ref(
            **ruled(i1, STRING6 + ": MIDI - Rs7_Base6 of the active seven-string song, returned to the bass target"),
            real=False,
        ),
        # ---- x87 and atomic sites ----
        **{
            sid: Ref(
                six=lambda c, s=sid: lane(s)(c) is None,
                why=STRING6 + ": its lane is extrapolated from strings 0 and 1 (2 * cell0 - cell1)",
                check=lane_st0(sid),
            )
            for sid in ("R3a", "R3b", "R3c", "R4")
        },
        "S3": Ref(
            **ruled(
                lambda c: W("mem", "writes", at={"mem0": (0x11F8, dword(35))}) if c.id == "vec7" else None,
                "a seven-entry tuning vector gives the detector string 6 (MIDI 35 at +11F8h, Rs7_OnDetectorTuned)",
                variant=lambda spec, cases: returning_elsewhere(spec, cases, x87.S3_RET, -1),
            )
        ),
        "PT1": Ref(variant=lambda spec, cases: returning_elsewhere(spec, cases, x87.PT1_RET, -1)),
        # ---- call sites: TM1-TM6 replace SNG_ChordNotes_TestStringMask 0x5581C0 (`test [ecx+al*4]`); the stock run
        # reads the masks at the record start, the hook the C8 tail mask[8] (+948h): both get the same masks ----
        **{
            f"TM{i}": Ref(
                **ruled(
                    mask_test(f"TM{i}"),
                    "strings 6/7 test C8 mask[6..7]; stock answers 0 for every string >= 6",
                    variant=with_mem(lambda c: {calls.D: calls.MASK8}),
                )
            )
            for i in range(1, 7)
        },
        **{
            f"EV{i}": Ref(
                **ruled(
                    lambda c: W("stack", at={"stack": (4, dword(h(c, "arg") + 0x258))}),
                    "every eval wrapper gets the int32[8] at eval+284h (template midi[6] + the N-slot tails) in place "
                    "of the stock int32[6] at eval+2Ch, for every song",
                )
            )
            for i in range(1, 6)
        },
        "S4": Ref(real=False),
        "PC2": Ref(),
        "PC3": Ref(),
        # The hook stores the stock `mov byte [ebx+8],1` after the call itself (the game repeats it): the stock run goes
        # past it too.
        "PSL2": Ref(variant=lambda spec, cases: (dataclasses.replace(spec, exits=(("resume", 4),)), cases)),
        **{sid: Ref(real=False) for sid in ("SE1", "SE2", "SE3")},
    }
)

# Stubbed helpers (all void): the reference covers the hook's glue, not the helper's own work.
GLUE_ONLY = sorted(sid for sid, ref in REFS.items() if not ref.real)
NO_REFERENCE: dict[str, str] = {}

(
    test_hook_behaves_like_the_stock_bytes,
    test_hook_differs_from_the_stock_bytes_on_purpose,
    test_every_converted_site_has_a_stock_reference_or_a_reason,
) = stock_tests(SPECS, REFS, NO_REFERENCE)
