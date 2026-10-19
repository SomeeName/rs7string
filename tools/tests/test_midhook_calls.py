"""Old-vs-new equivalence of the call sites converted from naked stubs to C++ mid-hooks in refactor step 7 batch 4
(stubs frozen at b6071a8, tools/tests/fixtures/midhook_b6071a8.json): TM1-TM6 and EV1-EV5 (features/nslot/
hooks_calls.cpp, light), S4, PC2 and PC3 (features/pitch/hooks_calls.cpp) - all Before placement - and PSL2
(features/headstock/hooks_mid.cpp, After placement). What is compared: midhook_oracle.py. Every case runs under 3
incoming EFLAGS patterns and 2 stack alignments; every run starts at the callee's entry with [esp] = site + 5.
"""

from __future__ import annotations

import struct

import pytest
from unicorn.x86_const import (
    UC_X86_REG_EAX,
    UC_X86_REG_EBX,
    UC_X86_REG_ECX,
    UC_X86_REG_EDX,
    UC_X86_REG_ESI,
)

from emu_harness import DATA, DLL, SNAPSHOT, site_va
from midhook_oracle import Case, Helper, Spec, oracle_tests

pytestmark = pytest.mark.skipif(
    not DLL.exists() or not (SNAPSHOT / "memory.bin").exists(), reason="needs built DLL and snapshot"
)

D = DATA + 0x2000  # scratch objects
JUNK = 0x5A5A5A00
SPECS: dict[str, tuple[Spec, list[Case]]] = {}


def register(spec: Spec, cases: list[Case]):
    SPECS[spec.sid] = (spec, cases)


def dwords(*values: int) -> bytes:
    return struct.pack(f"<{len(values)}I", *(v & 0xFFFFFFFF for v in values))


def ret(sid: str) -> int:
    return site_va(sid) + 5


# ---- TM1-TM6: SNG_ChordNotes_TestStringMask replaced (AL = string, ECX = C8 record, [esp+4] = flag, `ret 4`) ------
MASK8 = dwords(0x1, 0x2, 0x0, 0xFF, 0x80000000, 0x10, 0x3, 0xFFFFFFFF)
for i in range(1, 7):
    sid = f"TM{i}"
    register(
        Spec(sid, 5, flags="dead", exits=("resume",), call=True),
        [
            Case(
                f"s{string}-f{flag:x}",
                regs={UC_X86_REG_EAX: JUNK | string, UC_X86_REG_ECX: D},
                mem={D + 0x948: MASK8},
                stack={0: ret(sid), 4: flag},
            )
            for string in (0, 1, 2, 5, 6, 7, 8, 0xFF)
            for flag in (0x1, 0x2, 0x80000000)
        ],
    )

# ---- EV1-EV5: the matcher wrapper's first argument eval+2Ch becomes eval+284h ---------------------------------------
for i in range(1, 6):
    sid = f"EV{i}"
    register(
        Spec(sid, 5, flags="dead", exits=("target",), call=True),
        [Case(f"arg{arg:x}", stack={0: ret(sid), 4: arg, 8: 0x1234, 12: 0x5678}) for arg in (D + 0x2C, 0xFFFFFF00, 0)],
    )

# ---- S4: Rs7_OnSongTuning(session = EBX, detector = EAX, vector = ESI), then Play_Detector_SetTuning ----------------
register(
    Spec("S4", 5, helpers={"Rs7_OnSongTuning": Helper(3)}, flags="dead", exits=("target",), call=True),
    [
        Case(
            f"det{det:x}",
            regs={UC_X86_REG_EAX: det, UC_X86_REG_EBX: D + 0x400, UC_X86_REG_ESI: D + 0x800},
            stack={0: ret("S4"), 4: 7},
        )
        for det in (D, 0)
    ],
)


# ---- PC2 / PC3: 0x873F80 (ECX = tuning, EDX = instrument, ESI = string, [esp+4] = fret) over the tuning8 tail ------
TAIL = struct.pack("<8h", -2, 0, 1, 0, 0, 0, -1, 3)


def pitch_cases(sid: str, tuning: int, tail: int) -> list[Case]:
    obj = D + 0x100
    out = []
    for string in (0, 5, 6, 7, 8):
        for instrument in (1, 2, 0xFF):
            for tailed in (0, 1):
                out.append(
                    Case(
                        f"s{string}-i{instrument:x}-t{tailed}",
                        regs={UC_X86_REG_ECX: obj + tuning, UC_X86_REG_EDX: instrument, UC_X86_REG_ESI: string},
                        mem={obj + tail: TAIL},
                        stack={0: ret(sid), 4: 9},
                        rets={"Rs7Pitch_Tailed": tailed},
                    )
                )
    out.append(Case("null", regs={UC_X86_REG_ECX: 0, UC_X86_REG_ESI: 6}, stack={0: ret(sid), 4: 9}))
    return out


for sid, tuning, tail in (("PC2", 0x44, 0x58), ("PC3", 0x1340, 0x13B8)):
    register(
        Spec(sid, 5, helpers={"Rs7Pitch_Tailed": Helper(2)}, flags="dead", exits=("target", "resume"), call=True),
        pitch_cases(sid, tuning, tail),
    )


# ---- PSL2 (After placement): the callee (`ret 4`, patched: EAX = 1234h), then [ebx+8] = 1 and Rs7_PegSetLoaded ------
def psl2_callee(machine) -> dict:
    return {machine.target: bytes.fromhex("B8 34 12 00 00 C2 04 00")}  # mov eax,1234h; ret 4 (flags untouched)


register(
    Spec(
        "PSL2",
        5,
        helpers={"Rs7_PegSetLoaded": Helper(1, deref=((0, 16),))},
        flags="dead",
        exits=("resume",),
        patch=psl2_callee,
        call=True,
    ),
    [
        Case(f"ps{n}", regs={UC_X86_REG_EBX: D + 0x200 + 0x40 * n}, stack={0: ret("PSL2"), 4: D + 0x200 + 0x40 * n})
        for n in range(2)
    ],
)

ALWAYS_CALLS: set[str] = {"S4", "PSL2"}
(
    test_mid_hook_matches_frozen_stub,
    test_dead_flags_are_rewritten_before_any_read,
    test_every_registered_site_calls_its_helper_at_least_once,
) = oracle_tests(SPECS, ALWAYS_CALLS)
