"""Old-vs-new equivalence of the feature sites converted from naked stubs to C++ mid-hooks in refactor step 7 batch 5
(stubs frozen at 9c0e44f, tools/tests/fixtures/midhook_9c0e44f.json): the chord loops (features/chords), the
next-note grid (features/nextgrid), the FretGrid build (features/fretgrid), the chord diagrams (features/diagrams),
plain PSARC (features/psarc), the tuner (features/tuner) and the publication ports (features/publish).

What is compared: midhook_oracle.py. Every case runs under 3 incoming EFLAGS patterns (the last has DF set) and 2 stack
alignments.
"""

from __future__ import annotations

import itertools
import re
import struct
from pathlib import Path

import pytest
from unicorn.x86_const import (
    UC_X86_REG_EAX,
    UC_X86_REG_EBP,
    UC_X86_REG_EBX,
    UC_X86_REG_ECX,
    UC_X86_REG_EDI,
    UC_X86_REG_EDX,
    UC_X86_REG_ESI,
)

import sitetable
from emu_harness import DATA, DLL, SITES, SNAPSHOT, STACK, site_va
from midhook_oracle import Case, Helper, Spec, Sym, oracle_tests

pytestmark = pytest.mark.skipif(
    not DLL.exists() or not (SNAPSHOT / "memory.bin").exists(), reason="needs built DLL and snapshot"
)

ROOT = Path(__file__).resolve().parents[2]
D = DATA + 0x2000  # scratch objects (page-aligned, zeroed per run)
EBP_SAVED = 0xEB9EB9EB  # the caller's EBP an epilogue pops
SPECS: dict[str, tuple[Spec, list[Case]]] = {}
_, SITES_ALL = sitetable.load(SITES)
ALWAYS_CALLS: set[str] = set()


def register(spec: Spec, cases: list[Case], always: bool = False):
    SPECS[spec.sid] = (spec, cases)
    if always:
        ALWAYS_CALLS.add(spec.sid)


def blob(n: int, seed: int) -> bytes:
    return bytes((i * 31 + seed) & 255 for i in range(n))


def dwords(*values: int) -> bytes:
    return struct.pack(f"<{len(values)}I", *(v & 0xFFFFFFFF for v in values))


# ---- features/chords: the CL* chord loops over eight strings (light) -------------------------------------------------
# CLBM: TEST of the C8 mask[8] bit 1000h (flags exact: the SETA at the resume reads them).
MASKS = D + 0x100
register(
    Spec("CLBM", 7),
    [
        Case(f"s{s}-m{m:x}", regs={UC_X86_REG_EDX: MASKS, UC_X86_REG_EAX: s}, mem={MASKS + 0x948 + 4 * s: dwords(m)})
        for s, m in itertools.product((0, 7), (0, 0x1000, 0xFFFFFFFF, 0x80000000))
    ],
)
# CLBA: the C8 bends[8] address `eax * 184h + ecx + 988h`.
register(
    Spec("CLBA", 10, flags="free"),
    [
        Case(f"eax{a:x}", regs={UC_X86_REG_EAX: a, UC_X86_REG_ECX: 0x40000000})
        for a in (0, 1, 7, 0x7FFFFFFF, 0xFFFFFFFF)
    ],
)

# CLGU / CLGE / CLVB: string x stock count x chart bend byte x what Rs7ChordLoops_Target answers.
OWNER, INFO, OBJ = D + 0x400, D + 0x600, D + 0x800
STRINGS = (0, 5, 6, 7, 8, 0xFF)


def loop_cases(string_reg, owner_reg, info_reg, extra):
    out = []
    for s, count, found, (name, regs) in itertools.product(STRINGS, (6, 8), (0, OBJ), extra):
        out.append(
            Case(
                f"s{s:x}-n{count}-t{found:x}-{name}",
                regs={string_reg: 0xC1C1C100 | s, owner_reg: OWNER, info_reg: INFO, **regs},
                mem={OWNER: blob(0x40, s + 3), INFO: bytes(0x0B) + bytes([count]) + bytes(4)},
                rets={"Rs7ChordLoops_Target": found},
            )
        )
    return out


TARGET = {"Rs7ChordLoops_Target": Helper(3)}
register(
    Spec("CLGU", 13, helpers=TARGET, flags="free", exits=(("resume", 4), ("resume", 0x32))),
    loop_cases(
        UC_X86_REG_EAX,
        UC_X86_REG_ECX,
        UC_X86_REG_ESI,
        [(f"dl{d:x}", {UC_X86_REG_EDX: 0xD0D0D000 | d}) for d in (0, 0xFF)],
    ),
)
register(
    Spec("CLGE", 12, helpers=TARGET, flags="free", exits=("resume", ("resume", 0x20))),
    loop_cases(UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESI, [("eax", {UC_X86_REG_EAX: 0xAAAA5555})]),
)
register(
    Spec("CLVB", 12, helpers=TARGET, flags="free", exits=("resume", ("resume", 5))),
    loop_cases(UC_X86_REG_ECX, UC_X86_REG_EAX, UC_X86_REG_EDX, [("esi", {UC_X86_REG_ESI: 0x5E5E5E5E})]),
)

# ---- features/nextgrid: NG1 (light) and NG9 ---------------------------------------------------------------------------
# NG1: the cell index of an accepted (s, f) or the stock reject tail, which returns (`ret 10h`) to [ebp+4]; the return
# address is the resume, so both exits are the same address.
NG1_RESUME = site_va("NG1") + 16
register(
    Spec("NG1", 16, flags="free"),
    [
        Case(
            f"s{s:x}-f{f:x}",
            regs={UC_X86_REG_ESI: D + 0x100},
            ebp={0: EBP_SAVED, 4: NG1_RESUME, 8: 0xA5A5A500 | s, 0x0C: 0x5A5A5A00 | f},
            stack={0: 0xED1ED1ED},
        )
        for s, f in itertools.product((0, 7, 8, 0xFF), (0, 25, 26, 0xFF))
    ],
)

# NG9: the native frame (EBP2 above the live stack: the frame is 1074h bytes); the row-6 spawn result x tree size.
EBP2 = STACK + 0x84000
NG9_FRAME = {
    EBP2 + 0x0C: dwords(0x3C888889),
    EBP2 - 0x1074: dwords(6, 24, 0x42C80000),  # active strings, fret count, t
    EBP2 - 0x35C: dwords(0x11112222, 0x33334444),  # tree header, root
}
register(
    Spec(
        "NG9",
        15,
        helpers={"Rs7_NeckRow6N": Helper(6), "Rs7_NeckLog": Helper(1)},
        exits=("resume", "target"),
    ),
    [
        Case(
            f"row{r & 0xFF:x}-tree{n}",
            regs={UC_X86_REG_EBP: EBP2, UC_X86_REG_EBX: 0xBBBB0000},
            mem={**NG9_FRAME, EBP2 - 0x34C: dwords(n)},
            stack={0: 0xED1ED1ED, 4: 0xE51E51E5},
            rets={"Rs7_NeckRow6N": r},
        )
        for r, n in itertools.product((0xFFFFFFFF, 0, 5), (0, 3))
    ],
    always=True,  # Rs7_NeckRow6N runs every frame; Rs7_NeckLog only for a spawned target
)

# ---- features/fretgrid: FGRB (the build's "complete" exit) --------------------------------------------------------
register(
    Spec("FGRB", 5, helpers={"Rs7Fg_BuildExtra": Helper(2)}, flags="dead"),
    [Case(f"grid{g:x}", ebp={8: g, 0x0C: 0x6E6E0000}) for g in (0, D + 0x100)],
    always=True,
)

# ---- features/diagrams: DGB / DGM / DGS (helper + replay) and TP13 (the T8 record) --------------------------------
register(
    Spec("DGB", 11, helpers={"Rs7Diagram_Bind": Helper(2)}),
    [Case(f"attr{a:x}", stack={0x50: a}, ebp={8: 0x50450000}) for a in (0, D + 0x100)],
    always=True,
)
register(
    Spec("DGM", 7, helpers={"Rs7Diagram_Manifest": Helper(6)}),
    [
        Case(
            f"capo{capo:x}",
            stack={0x3C: capo << 24 | 0x123456, 0x50: D + 0x100, 0x58: D + 0x200, 0x5C: D + 0x300},
            ebp={0x0C: D + 0x400, 0x10: D + 0x500},
        )
        for capo in (0, 3, 0xFF)
    ],
    always=True,
)
register(
    Spec("DGS", 7, helpers={"Rs7Diagram_Loaded": Helper(2)}),
    [Case(f"tpl{t:x}", ebp={8: D + 0x100, 0x0C: t}) for t in (0, D + 0x200)],
    always=True,
)


def registry(slot: int, count: int, begin: int) -> bytes:
    """g_rs7_ns (64 entries of 32 bytes): entry `slot` registers `begin` with `count` templates."""
    raw = bytearray(64 * 32)
    raw[slot * 32 : slot * 32 + 32] = dwords(begin, count, 0x33330000, 0x22220000, count, 0x44440000, 0x5000, count)
    return bytes(raw)


SONG, TPL = D + 0x600, 0x11110000
register(
    Spec("TP13", 12, flags="free"),
    [
        Case(
            f"on{on:x}-reg{slot}-{n}-id{i:x}",
            regs={UC_X86_REG_EAX: i, UC_X86_REG_ESI: SONG, UC_X86_REG_EDX: 0xD0D0D0D0},
            mem={SONG + 0x94: dwords(TPL), Sym("g_rs7_ns"): registry(slot, n, TPL if n else 0)},
            words={"g_rs7_diagrams": on},
        )
        for on, (slot, n), i in itertools.product((0, 1, 0x80000000), ((0, 0), (0, 10), (63, 10)), (0, 9, 10))
    ],
)


# ---- features/tuner: entry dispatch (TU8H/C/A, TS8*), session storage (TU8INIT, TU8FREE) and AT ----------------------
# An entry dispatch continues where the helper says: here the stock entry itself (exit at the site), whose prologue
# rewrites the flags.
def entry_cases(sid: str, helper: str, regs: list[dict]) -> list[Case]:
    va = site_va(sid)
    return [
        Case(f"r{k}", regs=r, stack={0: 0x7E7E7E7E, 4: D + 0x100 + k, 8: 0xA2A2A2A2}, rets={helper: va})
        for k, r in enumerate(regs)
    ]


OBJECTS = [{UC_X86_REG_EAX: D + 0x100, UC_X86_REG_ECX: 0, UC_X86_REG_ESI: 0xFFFFFFFF}, {UC_X86_REG_EDI: D + 0x200}]
for sid, length in (("TU8H", 5), ("TU8C", 7), ("TU8A", 6)):
    register(
        Spec(sid, length, helpers={"Rs7Tuner8_Target": Helper(2)}, flags="dead", exits=(("resume", -length),)),
        entry_cases(sid, "Rs7Tuner8_Target", OBJECTS),
        always=True,
    )
TS8_IDS = re.findall(r"X\((TS8\w+), \d+\)", (ROOT / "src/features/tuner/tuner8s_ids.generated.h").read_text())
SESSION_FRAME = Helper(2, deref=((1, 32), (1, 4, 40)), opaque=(1,))  # the PUSHAD frame and the entry's [esp+4]
for sid in TS8_IDS:
    length = next(x.length for x in SITES_ALL if x.id == sid)
    register(
        Spec(sid, length, helpers={"Rs7Tuner8_SessTarget": SESSION_FRAME}, flags="dead", exits=(("resume", -length),)),
        entry_cases(sid, "Rs7Tuner8_SessTarget", OBJECTS),
        always=True,
    )

INIT_VA = site_va("TU8INIT")
register(
    Spec("TU8INIT", 7, helpers={"Rs7Tuner8_InitSession": Helper(1)}, flags="dead", exits=(("resume", -7),)),
    [Case(f"s{s:x}", regs={UC_X86_REG_ESI: s}, ebp={0: EBP_SAVED, 4: INIT_VA}, stack={0: 0xEB0EB0}) for s in (0, D)],
    always=True,
)
register(
    Spec("TU8FREE", 5, helpers={"Rs7Tuner8_FreeSessionSize": Helper(1)}, flags="dead", pushed=4),
    [Case(f"size{n:x}", regs={UC_X86_REG_ESI: D}, rets={"Rs7Tuner8_FreeSessionSize": n}) for n in (0x278, 0x380)],
    always=True,
)

# AT: autotune off / on x "tuned" or not; the "tuned" exit is the return address (here: the entry itself).
AT_VA = site_va("AT")
register(
    Spec("AT", 11, helpers={"Rs7_TunerAutotune": Helper(1)}, exits=("resume", ("resume", -11)), pushed=8),
    [
        Case(
            f"auto{a:x}-r{r:x}",
            regs={UC_X86_REG_ESI: D + 0x100, UC_X86_REG_EAX: 0xAAAAAA00},
            mem={D + 0x100: blob(0x100, 7)},
            stack={0: AT_VA, 4: 1, 8: 2, 0x0C: 3},
            words={"g_rs7_tuner_auto": a},
            rets={"Rs7_TunerAutotune": r},
        )
        for a, r in itertools.product((0, 1, 0xFFFFFFFF), (0, 1, 0x80000000))
    ],
)

# ---- features/publish: entry dispatch of the copies (light) and the grid sites ------------------------------------------
PUB_ENTRIES = {"PUBCL": 6, "PUBTO": 7, "PUBWU": 7}
PUB_PITCH = {"PUBNL": 9, "PUBAN": 6, "PUBAC": 6}
for table, helper in ((PUB_ENTRIES, "Rs7Publish_Target"), (PUB_PITCH, "Rs7PublishPitch_Target")):
    for sid, length in table.items():
        register(
            Spec(sid, length, helpers={helper: Helper(2)}, exits=(("resume", -length),)),
            entry_cases(sid, helper, OBJECTS),
            always=True,
        )

GINFO = D + 0x900
COUNTS = (0, 4, 6, 7, 8, 0xFF)


def info(count: int) -> dict:
    return {GINFO: bytes(0x0B) + bytes([count, 0x18]) + bytes(3)}


# PUBGE2 is not compared with its old stub: batch 5 fixed it (the displaced `cmp byte [esi+8],0` and the rel8 target);
# test_publish_grid_emu.py checks it against the stock bytes.
for sid, length, string_reg, info_reg in (
    ("PUBGS", 8, UC_X86_REG_EAX, UC_X86_REG_EDI),
    ("PUBGE1", 9, UC_X86_REG_EDX, UC_X86_REG_ESI),
):
    register(
        Spec(sid, length, exits=("resume", "target")),
        [
            Case(f"s{s:x}-n{n:x}", regs={string_reg: 0x51515100 | s, info_reg: GINFO}, mem=info(n))
            for s, n in itertools.product((0, 5, 6, 7, 8, 0xFF), (6, 7, 8))
        ],
    )
for sid in ("PUBGC", "PUBGO"):
    register(
        Spec(
            sid, 6, locals=0x24 if sid == "PUBGO" else 0, flags="exact" if sid == "PUBGO" else "free"
        ),  # PUBGO: `sub esp,24h`; PUBGC: its compare flags are dead
        [
            Case(
                f"n{n:x}",
                regs={UC_X86_REG_EAX: GINFO, UC_X86_REG_ECX: 0xC0C0C0C0, UC_X86_REG_EDX: 0xD0D0D0D0},
                mem=info(n),
            )
            for n in COUNTS
        ],
    )
register(
    Spec("PUBGB", 5, helpers={"Rs7Publish_GridBuild": Helper(2)}, flags="dead"),
    [Case(f"r{r}", ebp={8: D + 0x100, 0x0C: D + 0x200}, rets={"Rs7Publish_GridBuild": r}) for r in (0, 2)],
    always=True,
)
GA_VA, GF_VA = site_va("PUBGA"), site_va("PUBGF")
register(
    Spec(
        "PUBGA",
        6,
        helpers={"Rs7Gs_AttachExt": Helper(3)},
        flags="free",
        exits=("resume", ("resume", -6)),
        pushed=4,
        locals=0x4C,
    ),
    [
        Case(
            f"s{s:x}-hi{hi:x}",
            regs={UC_X86_REG_ECX: GINFO, UC_X86_REG_EAX: 0xAAAAAAAA},
            stack={0: GA_VA, 4: hi | s, 8: D + 0x300},
            rets={"Rs7Gs_AttachExt": 1},
        )
        for s, hi in itertools.product((0, 5, 6, 7, 8, 0xFF), (0, 0x12345600))
    ],
)
register(
    Spec(
        "PUBGF",
        5,
        helpers={"Rs7Publish_FingerSpan": Helper(3)},
        flags="free",
        exits=("resume", ("resume", -5)),
        pushed=12,
    ),
    [
        Case(
            f"on{on:x}",
            regs={UC_X86_REG_EDI: 3},
            stack={0: GF_VA, 4: D + 0x100, 8: D + 0x200},
            words={"g_rs7_native_publish": on},
            rets={"Rs7Publish_FingerSpan": 4},
        )
        for on in (0, 1, 0x80000000)
    ],
)
register(
    Spec("PUBEDGE", 13, helpers={"Rs7Publish_Edge": Helper(2)}, flags="dead"),
    [
        Case(f"cl{cl:x}-r{r}", regs={UC_X86_REG_EAX: GINFO, UC_X86_REG_ECX: cl}, rets={"Rs7Publish_Edge": r})
        for cl, r in itertools.product((0, 1, 0x100), (0, 6))
    ],
    always=True,
)


def length(sid: str) -> int:
    return next(x.length for x in SITES_ALL if x.id == sid)


# ---- features/headstock: PegSet slot bounds, LT1, AL1 ----------------------
PEG = D + 0xA00


def owners(native: bool) -> dict:
    """g_rs7_pegsets (64 entries): PEG registered in the last entry, or not at all."""
    table = [0x1000 + 0x10 * i for i in range(63)] + [PEG if native else 0x2000]
    return {Sym("g_rs7_pegsets"): dwords(*table)}


for sid, slot_reg, peg_reg, slots in (
    ("PSU1", UC_X86_REG_ECX, UC_X86_REG_EDI, (0, 5, 6, 7, 8)),
    ("PSU2", UC_X86_REG_EBX, UC_X86_REG_EDI, (0, 5, 6, 7, 8)),
    ("PSM1", UC_X86_REG_ECX, UC_X86_REG_EAX, (0, 5, 6, 7, 8)),
):
    register(
        Spec(sid, length(sid), exits=("resume", "target")),
        [
            Case(
                f"slot{v:x}-n{n}-native{int(nat)}",
                regs={slot_reg: 0xC0DE0000 | v, peg_reg: PEG},
                mem={**owners(nat), PEG: blob(4, 1) + n.to_bytes(2, "little") + blob(10, 2)},
            )
            for v, n, nat in itertools.product((*slots, 0xFFFF), (0, 6, 8), (False, True))
        ],
    )
register(
    Spec("LT1", 5, helpers={"Rs7Asset_LetterInline": Helper(1)}, extra_cmp=((Sym("g_rs7_lt1_inline"), 4),)),
    [
        Case(f"r{r:x}", regs={UC_X86_REG_EDI: D + 0x100, UC_X86_REG_EAX: 0xAAAAAAAA}, rets={"Rs7Asset_LetterInline": r})
        for r in (0, 1, 0x100)
    ],
    always=True,
)
register(
    Spec(
        "AL1",
        8,
        flags="dead",
        exits=("resume", Sym("Rs7_NifHandler")),
        pushed=4,  # the replayed `push ebp`
        words={"g_rs7_frame_global": 0x135FBFC},  # the X_DT ref the old stub's replay reads through
    ),
    [Case(f"on{on:x}", words={"g_rs7_asset_on": on}, stack={4: D + 0x100}) for on in (0, 1, 0x80000000)],
)

# ---- features/vguit: TICK, JUD1, JUD2, AUD1 ([test] vguitar) ----------------------------------------------------------
VG_ON = (0, 1, 0xFFFFFFFF)
register(
    Spec("TICK", length("TICK"), helpers={"Rs7Vg_OnTick": Helper(2)}, pushed=4, align=0x40),
    [Case(f"on{on:x}", stack={4: D + 0x100, 8: 0x42F60000}, words={"g_rs7_vg_on": on}) for on in VG_ON],
)
register(
    Spec("JUD1", length("JUD1"), helpers={"Rs7Vg_OnJudge": Helper(2)}, flags="dead"),
    [
        Case(f"on{on:x}", regs={UC_X86_REG_EBX: D + 0x100, UC_X86_REG_ESI: D + 0x300}, words={"g_rs7_vg_on": on})
        for on in VG_ON
    ],
)
register(
    Spec("JUD2", length("JUD2"), helpers={"Rs7Vg_OnScore": Helper(3)}, pushed=4, align=0x40),
    [
        Case(
            f"on{on:x}-cl{cl:x}",
            regs={UC_X86_REG_ECX: 0xC1C1C100 | cl, UC_X86_REG_EDX: D + 0x300},
            stack={4: D + 0x100},
            words={"g_rs7_vg_on": on},
        )
        for on, cl in itertools.product(VG_ON, (0, 1, 0xFF))
    ],
)
register(
    Spec("AUD1", length("AUD1"), helpers={"Rs7Vg_OnInput": Helper(2)}),
    [
        Case(
            f"on{on:x}-cb{cb:x}",
            regs={UC_X86_REG_ESI: D + 0x100, UC_X86_REG_EBX: D + 0x300},
            mem={D + 0x100: dwords(0x11, cb)},
            words={"g_rs7_vg_on": on},
        )
        for on, cb in itertools.product(VG_ON, (0, 0x12345678))
    ],
)

# ---- features/strings: the copied-function entries, GRM1, GRM2 ------------------------------------------------------
GS_ENTRIES = ("GSFR GSBD GSUL GSLD GSFO GSFI GSXF GSWD GSUP GSRS GSPK GSVB GSIC GSDM GSGL GSHC").split()
for sid in GS_ENTRIES:
    register(
        Spec(sid, length(sid), helpers={"Rs7Gs_Target": Helper(4)}, flags="dead", exits=(("resume", -length(sid)),)),
        entry_cases(sid, "Rs7Gs_Target", [*OBJECTS, {UC_X86_REG_ESI: D + 0x300, UC_X86_REG_EAX: 0}]),
        always=True,
    )
RENDERER, GS = D + 0xB00, D + 0xC00
register(
    Spec("GRM1", length("GRM1"), helpers={"Rs7Gs_MeshCount": Helper(1)}, flags="dead"),
    [
        Case(f"n{n}", ebp={8: RENDERER, -0x14: 0x5A5A5A5A}, mem={RENDERER: dwords(0, GS)}, rets={"Rs7Gs_MeshCount": n})
        for n in (6, 7)
    ],
    always=True,
)


def gs_image(native: bool) -> bytes:
    raw = bytearray(blob(0xB0, 9))
    raw[0x3A:0x3C] = (0x3752 if native else 0x3751).to_bytes(2, "little")
    raw[0xAC:0xB0] = (0x47375352).to_bytes(4, "little")
    for k in range(8):
        raw[0x04 + 4 * k : 0x08 + 4 * k] = (D + 0xD00 + 0x10 * k).to_bytes(4, "little")  # stock main[] (+4)
    for k in range(8):
        raw[0x40 + 4 * k : 0x44 + 4 * k] = (D + 0xE00 + 0x10 * k).to_bytes(4, "little")  # native main[] (+40h)
    return bytes(raw)


register(
    Spec("GRM2", length("GRM2"), flags="free"),
    [
        Case(
            f"s{k}-native{int(nat)}",
            regs={UC_X86_REG_EAX: GS, UC_X86_REG_ECX: 4 * k},
            mem={GS: gs_image(nat), D + 0xD00: blob(0x80, 4), D + 0xE00: blob(0x80, 5)},
        )
        for k, nat in itertools.product((0, 5, 6, 7), (False, True))
    ],
)

(
    test_mid_hook_matches_frozen_stub,
    test_dead_flags_are_rewritten_before_any_read,
    test_every_registered_site_calls_its_helper_at_least_once,
) = oracle_tests(SPECS, ALWAYS_CALLS)
