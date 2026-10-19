"""The calls written into the publication and tuner copies (call-in entries, features/publish/hooks_callin.cpp,
features/tuner/hooks_callin.cpp) against the naked helpers they replaced (frozen at 304b9d3,
tools/tests/fixtures/midhook_304b9d3.json) and, where the edit keeps the stock meaning on six strings, against its
stock bytes. Oracle: callin_oracle.py; part 1 (the GuitarStrings copies) is test_midhook_callin.py.
"""

from __future__ import annotations

import struct

import pytest
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDI, UC_X86_REG_EDX, UC_X86_REG_ESI

from callin_oracle import FLAG_PATTERNS, Case, Machine, Spec, check, diff, flags_dead, matrix
from emu_harness import CALLINS, DATA, DLL, SNAPSHOT, Emu

pytestmark = pytest.mark.skipif(
    not DLL.exists() or not (SNAPSHOT / "memory.bin").exists(), reason="needs built DLL and snapshot"
)

D = DATA + 0x2000  # scratch objects
SPECS: dict[str, tuple[Spec, list[Case]]] = {}


def register(spec: Spec, cases: list[Case]):
    SPECS[spec.name] = (spec, cases)


# ---- PubBaseG / PubBaseB (PUBCL 0x8751FC / 0x875205): `sub eax,[table+ecx*4]`, stock six / four entries -------------
for name, va, strings, stock in (("PubBaseG", 0x8751FC, 8, 6), ("PubBaseB", 0x875205, 4, 4)):
    register(
        Spec(name, f"Rs7_Call_{name}", va, 7),
        [
            Case(f"s{s}-midi{midi:x}", regs={UC_X86_REG_ECX: s, UC_X86_REG_EAX: midi}, stock=s < stock)
            for s in range(strings)
            for midi in (0, 40, 64, 0x7F, 0xFFFFFFF0)
        ],
    )

# ---- PubClassify (PUBCL 0x87522C, in place of `mov [ebp-3Ch],0`): [ebp-38h] = Rs7Publish_Classify(EDI, ESI, ...) -----
register(
    Spec(
        "PubClassify",
        "Rs7_Call_PubClassify",
        0x87522C,
        7,
        flags="dead",
        helpers={"Rs7Publish_Classify": 4},
    ),
    [
        Case(
            f"r{r:x}", regs={UC_X86_REG_EDI: D, UC_X86_REG_ESI: D + 0x100}, rets={"Rs7Publish_Classify": r}, stock=False
        )
        for r in (0, 0x305, 0xFFFFFFFF)
    ],
)

# ---- PubWireCount (PUBWU 0x58E7A2, `mov [ebp+8],6`): [ebp+8] = the count byte [[[edi]]+24h]+0Bh ---------------------
register(
    Spec("PubWireCount", "Rs7_Call_PubWireCount", 0x58E7A2, 7),
    [
        Case(
            f"n{n}",
            regs={UC_X86_REG_EDI: D},
            mem={D: struct.pack("<I", D + 0x100), D + 0x124: struct.pack("<I", D + 0x200), D + 0x20B: bytes([n])},
            stock=n == 6,
        )
        for n in (4, 6, 7, 8, 0xFF)
    ],
)

# ---- PubEdge (TU8A 0x7E72D0, 13 bytes; EAX = InstrumentInfo, CL = first): EAX = Rs7Publish_EdgeCopy(info, first) ---
register(
    Spec("PubEdge", "Rs7_Call_PubEdge", 0x7E72D0, 13, flags="dead", helpers={"Rs7Publish_EdgeCopy": 2}),
    [
        Case(
            f"cl{cl:x}-r{r}",
            regs={UC_X86_REG_EAX: D, UC_X86_REG_ECX: 0x1200 | cl},
            rets={"Rs7Publish_EdgeCopy": r},
            stock=False,
        )
        for cl in (0, 1, 0xFF)
        for r in (0, 5, 7)
    ],
)

# ---- Tu8Advance / Tu8First: stdcall (session, dir) / (session) -> EAX, ECX / EDX kept; synthetic calls of the session
# copies (tools/gen_tuner8s.py: `call; test eax,eax` rewrites the flags), so no stock bytes
register(
    Spec(
        "Tu8Advance",
        "Rs7Tu8_Advance",
        0,
        5,
        flags="free",
        helpers={"Rs7Tu8_AdvanceC@8": (2, 8)},
        local={"Rs7Tu8_AdvanceC@8": "Rs7_CallIn_Tu8Advance"},
    ),
    [
        Case(f"dir{d}-r{r}", stack={0: D, 4: d & 0xFFFFFFFF}, rets={"Rs7Tu8_AdvanceC@8": r}, stock=False)
        for d in (1, -1)
        for r in (0, 1)
    ],
)
register(
    Spec(
        "Tu8First",
        "Rs7Tu8_First",
        0,
        5,
        flags="free",
        helpers={"Rs7Tu8_FirstC@4": (1, 4)},
        local={"Rs7Tu8_FirstC@4": "Rs7_CallIn_Tu8First"},
    ),
    [Case(f"r{r}", stack={0: D}, rets={"Rs7Tu8_FirstC@4": r}, stock=False) for r in (0, 6)],
)

# ---- the pitch copies' synthetic calls (tools/gen_publish_pitch.py; no stock bytes) ------------------------------------
# PubChordTail (PUBAC): Rs7PublishPitch_ChordTail(EBP-94h, [EBP-44h], [EBP-3Ch]); registers kept
register(
    Spec(
        "PubChordTail",
        "Rs7_Call_PubChordTail",
        0,
        5,
        flags="dead",  # test_pitch_callin_flags_are_dead
        helpers={"Rs7PublishPitch_ChordTail": 3},
    ),
    [Case("tail", stock=False)],
)
# ReverseTail (PUBAN): ECX = MIDI, EDX = string, ESI = tail tuning or 0, [esp] = instrument -> EAX = ECX = the fret
TAIL = (3, -1, 0, 2, -5, 1, -2, 4)
register(
    Spec("ReverseTail", "Rs7_Publish_ReverseTail", 0, 5, flags="dead"),
    [
        Case(
            f"s{s:x}-tail{bool(tail)}",
            regs={UC_X86_REG_ECX: 52, UC_X86_REG_EDX: s, UC_X86_REG_ESI: tail},
            mem={D + 0x700: struct.pack("<8h", *TAIL)},
            stack={0: 1},
            stock=False,
        )
        for s in (0, 5, 6, 7, 8, 0xFFFFFFFF)
        for tail in (0, D + 0x700)
        if not (tail and s > 7)  # the old helper reads the tail at any index; out of range is not a case of the copy
    ],
)

MACHINES: dict = {}


@pytest.mark.parametrize("name,case_id", matrix(SPECS, "old"))
def test_callin_matches_the_frozen_helper(name, case_id):
    check(MACHINES, SPECS, name, case_id, "old")


@pytest.mark.parametrize("name,case_id", matrix(SPECS, "stock"))
def test_callin_matches_the_stock_bytes_on_six_strings(name, case_id):
    check(MACHINES, SPECS, name, case_id, "stock")


@pytest.mark.parametrize("name", [n for n, (s, _) in SPECS.items() if s.flags != "exact" and s.stock_va])
def test_dead_flags_are_dead_at_the_stock_continuation(name):
    assert flags_dead(SPECS[name][0])


@pytest.mark.parametrize("name", [n for n, (s, _) in SPECS.items() if s.stock_va])
def test_every_native_case_differs_from_stock(name):
    """The cases marked not-stock run the native path (the eight-entry tables, a C helper): they differ from the stock
    bytes."""
    spec, cases = SPECS[name]
    machine = MACHINES.setdefault(name, Machine(spec))
    for case in (c for c in cases if not c.stock):
        want, got = machine.run("stock", case, FLAG_PATTERNS[0], 0), machine.run("new", case, FLAG_PATTERNS[0], 0)
        assert diff(spec, want, got, FLAG_PATTERNS[0]), case.id


def test_every_callin_has_a_spec():
    """Each call-in the DLL defines is compared in this module or in test_midhook_callin.py (ShapeDone through Shape)."""
    from test_midhook_callin import SPECS as STRINGS

    assert set(CALLINS) == set(SPECS) | set(STRINGS) | {"ShapeDone"}


def test_pitch_callin_flags_are_dead():
    """After the pitch copies' synthetic calls the generated code rewrites every arithmetic flag before a read: PUBAN
    `add esp,4` after ReverseTail, PUBAC `mov edi,-2; xor eax,eax` after the chord tail."""
    import re

    import capstone

    from emu_common import REPO

    text = (REPO / "src/features/publish/publish_pitch.generated.h").read_text()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    for ident, sym, after in (("PUBAN", "ReverseTail", ["add"]), ("PUBAC", "PubChordTail", ["mov", "xor"])):
        code = re.search(rf"kPitchCode_{ident}\[\] = \{{(.*?)\}};", text, re.S).group(1)
        code = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", code))
        off = int(re.search(rf"\{{(\d+), 0, &g_rs7_callin_{sym}\}}", text).group(1))
        insns = list(md.disasm(code[off + 4 : off + 4 + 16], 0))
        assert [i.mnemonic for i in insns[: len(after)]] == after, (ident, [i.mnemonic for i in insns[:3]])


def _flags_dead_after(code: bytes, start: int) -> bool:
    """No arithmetic flag is read from `start` on before all six are written (linear: a branch first is a failure)."""
    import capstone

    from midhook_oracle import _flag_set

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    defined: set = set()
    for insn in md.disasm(code[start:], start):
        if _flag_set(insn, ("TEST",)) - defined:
            return False
        defined |= _flag_set(insn, ("MODIFY", "RESET", "SET", "UNDEFINED"))
        if len(defined) == 6:
            return True
        if insn.mnemonic.startswith(("j", "call", "ret")):
            return False
    return False


def test_tuner_session_calls_flags_are_dead():
    """flags="free" for Tu8Advance / Tu8First: after every synthetic call of them in the session copies
    (tuner8s.generated.h, kTu8Sym + 0 / + 2) the generated code rewrites all arithmetic flags before reading one
    (`test eax,eax` after a step)."""
    import re

    from emu_common import REPO

    text = (REPO / "src/features/tuner/tuner8s.generated.h").read_text()
    sites = 0
    for ident, body, rels in re.findall(
        r"kTu8Code_(\w+)\[\] = \{(.*?)\};\s*static const Tu8Rel kTu8Rel_\1\[\] = \{(.*?)\};", text, re.S
    ):
        code = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", body))
        for off, k in re.findall(r"\{(\d+), kTu8Sym \+ (\d+), 0\}", rels):
            if k in ("0", "2"):  # SYMS: g_rs7_callin_Tu8Advance, g_rs7_callin_Tu8First
                assert code[int(off) - 1] == 0xE8, (ident, off)
                assert _flags_dead_after(code, int(off) + 4), (ident, off)
                sites += 1
    assert sites >= 10
