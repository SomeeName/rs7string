"""Old-vs-new equivalence of the wrappers converted from naked stubs to Wrap-placement mid-hooks (ARCHITECTURE.md
section 15 "Wrap placement"; stubs frozen at b72bc52, tools/tests/fixtures/midhook_b72bc52.json). What is compared:
midhook_oracle.py. Every case runs under 3 incoming EFLAGS patterns and 2 stack alignments; every run starts at the
hook (old stub / new entry; the stock run at the callee or the site's original bytes) with [esp] = the game's return
address (RET_AT for the function entries F1, TJ, TU, TN). The callees are replaced by short stand-ins that
scribble over their first argument slot (the old stubs and the new entries both pass copies), so a callback reading
the argument after the call would see the scribble if it read the callee's copy.
"""

from __future__ import annotations

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
from midhook_oracle import Case, Helper, Spec, oracle_tests
from midhook_stock import RET_AT, Ref, W, ruled, stock_tests

pytestmark = pytest.mark.skipif(
    not DLL.exists() or not (SNAPSHOT / "memory.bin").exists(), reason="needs built DLL and snapshot"
)

D = DATA + 0x2000  # scratch objects
SPECS: dict[str, tuple[Spec, list[Case]]] = {}


def register(spec: Spec, cases: list[Case]):
    SPECS[spec.sid] = (spec, cases)


def ret(sid: str) -> int:
    return site_va(sid) + 5


def stand_in(pops: int, eax: int = 0x1234):
    """A callee: mov dword [esp+4], 0DEADh; mov eax, `eax`; ret `pops` (flags untouched)."""
    code = bytes.fromhex("C7 44 24 04 AD 0D 00 00 B8") + struct.pack("<I", eax)
    code += bytes.fromhex("C2") + struct.pack("<H", pops) if pops else b"\xc3"

    def patch(machine) -> dict:
        return {machine.target: code}

    return patch


# ---- PP1: SNG_Song_PostProcess (`ret 8`), then Rs7Ns_OnPostProcessed(song = argument 0) ------------------------------
register(
    Spec("PP1", 5, helpers={"Rs7Ns_OnPostProcessed": Helper(1)}, exits=("resume",), patch=stand_in(8), call=True),
    [Case(f"song{n}", stack={0: ret("PP1"), 4: D + 0x100 * n, 8: 0x77}) for n in range(2)],
)

# ---- F1: the entry of Rnd_FretBars_ApplyTransforms (EAX = grid, ECX = guitar; plain `ret`), then Rs7_FretsAfterApply ---
# The stock body after the replayed prologue is replaced by its epilogue `mov esp,ebp; pop ebp; ret`.
register(
    Spec(
        "F1",
        6,
        helpers={"Rs7_FretsAfterApply": Helper(1)},
        flags="free",  # the old stub's `add esp,4` / the prologue's `sub`: dead after the function returns
        exits=(RET_AT,),
        patch=lambda m: {m.resume: bytes.fromhex("8B E5 5D C3")},
    ),
    [
        Case(f"grid{n}", regs={UC_X86_REG_EAX: D + 0x100 * n, UC_X86_REG_ECX: D + 0x800}, stack={0: RET_AT})
        for n in (0, 1)
    ],
)

# ---- IN1: Rnd_Inlays_ApplyTransforms (EAX = guitar, the set; `ret 4`), then Rs7_InlaysAfter(guitar, set) -------------
register(
    Spec(
        "IN1", 5, helpers={"Rs7_InlaysAfter": Helper(2)}, flags="dead", exits=("resume",), patch=stand_in(4), call=True
    ),
    [
        Case(f"set{n}", regs={UC_X86_REG_EAX: D, UC_X86_REG_EDI: D}, stack={0: ret("IN1"), 4: D + 0x400 + 0x40 * n})
        for n in range(2)
    ],
)

# ---- PSN2: the named-colour dispatch (EAX = name, ECX = player, slot or NULL; `ret 4`; EBX = index, ESI = session) ----
register(
    Spec("PSN2", 5, helpers={"Rs7_PegNamed": Helper(2)}, flags="dead", exits=("resume",), patch=stand_in(4), call=True),
    [
        Case(
            f"i{index}-{'slot' if slot else 'null'}",
            regs={UC_X86_REG_EAX: D + 0x600, UC_X86_REG_ECX: D + 0x700, UC_X86_REG_EBX: index, UC_X86_REG_ESI: D},
            stack={0: ret("PSN2"), 4: slot},
        )
        for index in (0, 5, 6, 7, 0x80000000)
        for slot in (D + 0x300, 0)
    ],
)

# ---- AM1: 0x4E6A70 (targets, tolerance, two floats; `ret 10h`) gets an int32[8] built by Rs7Ns_Shift8 ---------------
register(
    Spec(
        "AM1",
        5,
        # the int32[8] lies at different stack addresses (old stub frame / new scratch): compared by content
        helpers={"Rs7Ns_Shift8": Helper(4, deref=((0, 32), (1, 24), (2, 16)), opaque=(0,))},
        flags="dead",
        exits=("resume",),
        patch=stand_in(16),
        call=True,
    ),
    [
        Case(
            f"shift{shift}",
            regs={UC_X86_REG_EDI: shift & 0xFFFFFFFF, UC_X86_REG_ESI: D + 0x700},
            mem={D + 0x100: struct.pack("<6i", 40, 45, 50, 55, 59, 64), D + 0x500: bytes(range(16))},
            ebp={-0x44: D + 0x500},
            stack={0: ret("AM1"), 4: D + 0x100, 8: 50, 12: 0x3F000000, 16: 0x40000000},
        )
        for shift in (0, 2, -3)
    ],
)

# ---- TJ / TU / TN: the tuning-name resolvers (function entries; the body after the prologue loads EAX first, so the
# stand-in is `mov eax,1234h` and the epilogue `mov esp,ebp; pop ebp; ret`) --------------------------------------------
BODY = bytes.fromhex("B8 34 12 00 00 8B E5 5D C3")


def names(sid: str, helpers: dict) -> Spec:
    return Spec(sid, 9, helpers=helpers, flags="free", exits=(RET_AT,), patch=lambda m: {m.resume: BODY})


ON = (0, 1, 2)  # g_rs7_tn_on, a noncanonical value included
register(
    names("TJ", {"Rs7_TnJsonPre": Helper(4), "Rs7_TnJsonPost": Helper(1)}),
    [
        Case(
            f"on{on}-h{handle:x}",
            regs={UC_X86_REG_ECX: D + 0x100, UC_X86_REG_EDX: D + 0x200},
            stack={0: RET_AT, 4: D + 0x300, 8: 0xFFFFFF9C},
            words={"g_rs7_tn_on": on},
            rets={"Rs7_TnJsonPre": handle},
        )
        for on in ON
        for handle in (0, 0x77)
    ],
)
register(
    names("TU", {"Rs7_TnUuidPre": Helper(1), "Rs7_TnUuidPost": Helper(1)}),
    [
        Case(
            f"on{on}-h{handle:x}",
            regs={UC_X86_REG_ECX: D + 0x100, UC_X86_REG_EDX: D + 0x200},
            stack={0: RET_AT},
            words={"g_rs7_tn_on": on},
            rets={"Rs7_TnUuidPre": handle},
        )
        for on in ON
        for handle in (0, 0x77)
    ],
)
register(
    # the post flag lies in the old stub's frame / the new WrapFrame::user: compared by content
    names("TN", {"Rs7_TnArrayPre": Helper(3, deref=((1, 24), (2, 4)), opaque=(2,)), "Rs7_TnArrayPost": Helper(2)}),
    [
        Case(
            f"on{on}-r{done}-p{post:x}",
            mem={D + 0x300: struct.pack("<6i", -2, 0, 0, 0, 0, 0)},
            stack={0: RET_AT, 4: D + 0x200, 8: D + 0x300},
            words={"g_rs7_tn_on": on},
            rets={"Rs7_TnArrayPre": done},
            stores={"Rs7_TnArrayPre": ((2, post),)},
        )
        for on in ON
        for done in (0, 1)
        for post in (0, 1, 0x80000000)
    ],
)

# ---- MD1: the entry of Play_Matcher_DetectChord (matcher, int32 targets[8], tolerance, history; `ret 10h`) ----------
# Rs7Ns_DetectChord compacts into the scratch (returns -1, the stock body runs on it) or judges itself (0 / 1). The
# stand-in body after the replayed prologue returns targets[0] of the array it got: `mov eax,[ebp+0Ch]; mov eax,[eax]`,
# then `mov esp,ebp; pop ebp; ret 10h`. The helper writes 55h as the compacted array's first target.
TARGETS8 = struct.pack("<8i", 0x55, 45, -1, 50, 55, -1, 59, 64)
register(
    Spec(
        "MD1",
        9,
        # the int32[6] lies in the old stub's frame / the new scratch: compared by content
        helpers={"Rs7Ns_DetectChord": Helper(5, deref=((1, 32), (4, 24)), opaque=(4,))},
        flags="free",
        exits=(RET_AT,),
        patch=lambda m: {m.resume: bytes.fromhex("8B 45 0C 8B 00 8B E5 5D C2 10 00")},
        clobbers=("ecx", "edx"),  # the judged path: the old stub returns with Rs7Ns_DetectChord's ECX / EDX
    ),
    [
        Case(
            f"r{judged & 0xFF:x}",
            mem={D + 0x100: TARGETS8},
            stack={0: RET_AT, 4: D, 8: D + 0x100, 12: 50, 16: 3},
            rets={"Rs7Ns_DetectChord": judged & 0xFFFFFFFF},
            stores={"Rs7Ns_DetectChord": ((4, 0x55),)},
        )
        for judged in (-1, 0, 1)
    ],
)

ALWAYS_CALLS: set[str] = {"PP1", "F1", "IN1", "AM1", "MD1"}
(
    test_mid_hook_matches_frozen_stub,
    test_dead_flags_are_rewritten_before_any_read,
    test_every_registered_site_calls_its_helper_at_least_once,
) = oracle_tests(SPECS, ALWAYS_CALLS)

# ---- stock reference (midhook_stock.py): the original bytes against the hooked path --------------------------------
# The helpers are stubbed (real=False; they need a loaded song, guitar or tuner the emulator lacks), so these check the
# entries' registers, stack, exits and memory against the game. The stand-in callees keep every case observable.


def tn_named(case: Case) -> bool:
    """TN: Rs7_TnArrayPre built a known 7-string name, the original is skipped."""
    return bool(case.words["g_rs7_tn_on"] and case.rets["Rs7_TnArrayPre"])


REFS: dict[str, Ref] = {
    **{sid: Ref(real=False) for sid in ("PP1", "IN1", "F1", "PSN2", "AM1", "TJ", "TU")},
    "MD1": Ref(
        real=False,
        **ruled(
            lambda case: (
                None
                if case.rets["Rs7Ns_DetectChord"] == 0xFFFFFFFF
                else W("regs", regs={"eax": case.rets["Rs7Ns_DetectChord"]})
            ),
            why="seven or eight targets: Rs7Ns_DetectChord judged the chord, the stock function is skipped",
        ),
    ),
    "TN": Ref(
        real=False,
        **ruled(
            lambda case: W("regs", regs={"eax": D + 0x200}) if tn_named(case) else None,
            why="a known 7-string tuning: Rs7_TnArrayPre constructed the name, the hook returns the output itself",
        ),
    ),
}
(
    test_hook_behaves_like_the_stock_bytes,
    test_hook_differs_from_the_stock_bytes_on_purpose,
    test_every_converted_site_has_a_stock_reference_or_a_reason,
) = stock_tests(SPECS, REFS, {})
