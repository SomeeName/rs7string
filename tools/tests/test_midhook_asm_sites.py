"""Old-vs-new equivalence and stock references of the sites converted from naked stubs in the asm-reduction pass (stubs
frozen at b72bc52 and c15ca8d, tools/tests/fixtures/midhook_b72bc52.json / midhook_c15ca8d.json):
- the guitar ctor's allocations and GuitarStrings ctor, now call sites (GSA1, GSC1: Before placement; GIA1: Wrap) whose
  old jmp stubs also covered the instructions before the call (Spec.lead);
- GRM3 (an x87 result through Context::PushSt0);
- BT1 (Play_Eval_Build's epilogue, `ret 14h` now relocated) and AG1-AG3 (whole child queries, light, Rs7Mid::Ret).
What is compared: midhook_oracle.py and midhook_stock.py. Callees are replaced by short stand-ins.
"""

from __future__ import annotations

import dataclasses
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

from emu_harness import DATA, DLL, SNAPSHOT
from midhook_oracle import EBP, Case, Helper, Spec, oracle_tests
from midhook_stock import RET_AT, Ref, W, ruled, stock_tests

pytestmark = pytest.mark.skipif(
    not DLL.exists() or not (SNAPSHOT / "memory.bin").exists(), reason="needs built DLL and snapshot"
)

D = DATA + 0x2000  # scratch objects
SPECS: dict[str, tuple[Spec, list[Case]]] = {}


def register(spec: Spec, cases: list[Case]):
    SPECS[spec.sid] = (spec, cases)


def callee(code: str):
    def patch(machine) -> dict:
        return {machine.target: bytes.fromhex(code)}

    return patch


# ---- GSA1: `push 40h; call 0x8F9D40` (cdecl (size, 0)); the allocator stand-in returns its size argument ------------
register(
    # the old stub calls through g_rs7_fn_alloc (X_ALLOC, bound to the allocator by Rs7_Startup)
    Spec("GSA1", 5, lead=2, pushed=4, patch=callee("8B 44 24 04 C3"), words={"g_rs7_fn_alloc": 0x8F9D40}),
    [Case("alloc", stack={0: 0})],
)

# ---- GSC1: `push eax; call Rnd_GuitarStrings_Ctor` (`ret 4`; ESI = guitar) -> Rs7Gs_Construct(guitar, gs) -----------
register(
    Spec(
        "GSC1",
        5,
        lead=1,
        helpers={"Rs7Gs_Construct": Helper(2, ret=D + 0x300)},
        patch=callee("8B 44 24 04 C2 04 00"),  # the ctor stand-in returns `this`
        flags="dead",  # the old stub left its `add esp,8` flags
        clobbers=("ecx", "edx"),  # after a call: the old stub returned with the C helper's ECX / EDX
    ),
    [Case("gs", regs={UC_X86_REG_EAX: D + 0x300, UC_X86_REG_ESI: D})],
)

# ---- GIA1: `push 38h; mov [esi+4Ch],ebx; call 0x8F9D40` -> 48h, then Rs7Gs_InfoAllocated(a non-NULL info) ---------
# The allocator stand-in returns its second argument (the case's [esp]), so both branches run.
register(
    Spec(
        "GIA1",
        5,
        lead=5,
        pushed=4,
        helpers={"Rs7Gs_InfoAllocated": Helper(1)},
        flags="dead",  # the old stub left its `test eax,eax` or the helper's flags
        patch=callee("8B 44 24 08 C3"),  # mov eax,[esp+8]; ret
    ),
    [
        Case(f"info{n}", regs={UC_X86_REG_ESI: D, UC_X86_REG_EBX: 0x1234}, stack={0: info})
        for n, info in enumerate((0, D + 0x600))
    ],
)

# A full x87 stack (the default fld1; fldpi and six more fld1): PushSt0 refuses and the replay's own fld overflows, as
# the old stub's fld did. Not in the matrix: test_grm3_full_x87_stack_faults_like_the_old_stub.
GRM3_FULL = Case("row18-full", ebp={0x10: 0x18}, x87=bytes.fromhex("D9 E8") * 6)

# ---- GRM3: `mov edx,[ebp+10h]; fld dword [edx+1199D9Ch]` (EDX = 4 * mesh); strings 6/7 load g_rs7_ns_v ------------
register(
    Spec("GRM3", 9),
    [Case(f"row{row:x}", ebp={0x10: row}) for row in (0, 0x14, 0x18, 0x1C)],
)

# ---- BT1: the epilogue of Play_Eval_Build: Rs7Ns_OnEvalBuilt(eval = EBX, template, ChordNotes), then the epilogue ----
# [esp] = the saved EDI, ESI, EBX, EBP, then the return address and the five arguments `ret 14h` pops.
register(
    Spec("BT1", 7, helpers={"Rs7Ns_OnEvalBuilt": Helper(3)}, flags="dead", exits=(RET_AT,)),
    [
        Case(
            f"tpl{t}-cn{n}",
            regs={UC_X86_REG_EBX: D, UC_X86_REG_EAX: D},
            ebp={0x14: D + 0x100 if t else 0, 0x18: D + 0x200 if n else 0},
            stack={0: 0x111, 4: 0x222, 8: 0x333, 12: EBP, 16: RET_AT},
        )
        for t in (0, 1)
        for n in (0, 1)
    ],
)

# ---- AG1-AG3: the child queries (ESI = parent eval, EDI = tracker, EDX = string pointer): AL = Rs7Ns_Aggregate --------
for sid in ("AG1", "AG2", "AG3"):
    register(
        Spec(sid, 10, helpers={"Rs7Ns_Aggregate": Helper(5)}, flags="dead", exits=(RET_AT,)),
        [
            Case(
                f"r{r}-{'null' if not esi else 'eval'}",
                regs={UC_X86_REG_EAX: 0x12345678, UC_X86_REG_ESI: esi, UC_X86_REG_EDI: D + 0x800, UC_X86_REG_EDX: D},
                mem={D + 0x400: bytes(0xC28), D + 0x800: bytes(0x2000)},
                stack={0: RET_AT},
                rets={"Rs7Ns_Aggregate": r},
            )
            for r in (0, 1)
            for esi in (0, D + 0x400)
        ],
    )

# ---- PE0: the entry of 0x873E90 (EAX = instrument, EDX = string, [esp+4] = fret, [esp+8] = std::vector<int16>*) -----
VEC = D + 0xA00
TUNE8 = (-2, 0, 1, 0, 0, 3, -1, 2)


def pe0_case(instrument: int, string: int, entries: int) -> Case:
    data = VEC + 0x40
    return Case(
        f"i{instrument}-s{string}-n{entries}",
        regs={UC_X86_REG_EAX: instrument, UC_X86_REG_EDX: string, UC_X86_REG_ECX: 0xC1C1, UC_X86_REG_ESI: 0x5151},
        mem={VEC: struct.pack("<II", data, data + 2 * entries), data: struct.pack("<8h", *TUNE8)},
        stack={0: RET_AT, 4: 7, 8: VEC},
    )


register(
    Spec(
        "PE0", 6, pushed=4, flags="dead", exits=(RET_AT, ("resume", 0x73))
    ),  # the return, or the stock failure path 0x873F09
    [pe0_case(i, s, n) for i in (1, 2, 7) for s in (0, 3, 5, 6, 7, 8) for n in (6, 8)],
)

ALWAYS_CALLS: set[str] = {"GSC1", "BT1", "AG1", "AG2", "AG3"}
(
    test_mid_hook_matches_frozen_stub,
    test_dead_flags_are_rewritten_before_any_read,
    test_every_registered_site_calls_its_helper_at_least_once,
) = oracle_tests(SPECS, ALWAYS_CALLS)


def size_slot(size: int):
    """The size argument the allocator got: the first live stack dword after the run."""
    return W("stack", at={"stack": (0, struct.pack("<I", size))})


def pe0_native(case: Case):
    """PE0 against the stock function: guitar strings 6/7 use the native bases (stock: base -1)."""
    instrument, string = case.regs[UC_X86_REG_EAX], case.regs[UC_X86_REG_EDX]
    entries = int(case.id.rsplit("-n", 1)[1])
    if instrument == 2 or string not in (6, 7):
        return None
    base = (35, 30)[string - 6]
    if string >= entries:  # the stock failure path: ECX = the base (stock -1), live at its call
        return W("regs", regs={"ecx": base})
    return W("regs", regs={"eax": TUNE8[string] + base + 7})  # ECX is dead at the return


def bt1_tails(case: Case):
    """BT1 with the real Rs7Ns_OnEvalBuilt: the eval (D) gets its N-slot tails (unregistered template / ChordNotes: the
    empty tails), which the stock epilogue never writes."""
    return W("writes")


REFS: dict[str, Ref] = {
    "GSA1": Ref(
        **ruled(
            # the allocator stand-in returns its size argument
            lambda case: W("regs", "stack", regs={"eax": 0xB0}, at={"stack": (0, struct.pack("<I", 0xB0))}),
            why="the native GuitarStrings size 0B0h instead of 40h",
        )
    ),
    # stubbed: the real Rs7Gs_Construct allocates the extra strings through the game allocator (g_rs7_fn_alloc), which needs
    # the live heap; stubbed it returns the GuitarStrings as the real one does
    "GSC1": Ref(real=False),
    "GIA1": Ref(  # Rs7Gs_InfoAllocated for real: it registers the info in the DLL (not game-observable)
        **ruled(
            lambda case: (
                dataclasses.replace(size_slot(0x48), keys=frozenset({"stack", "writes"}))
                if case.stack[0]
                else size_slot(0x48)
            ),
            why="the native InstrumentInfo size 48h instead of 38h; a non-NULL info gets its extension (writes)",
        ),
    ),
    "BT1": Ref(**ruled(lambda case: bt1_tails(case), why="the eval's N-slot tails, written by Rs7Ns_OnEvalBuilt")),
    **{sid: Ref() for sid in ("AG1", "AG2", "AG3")},  # Rs7Ns_Aggregate for real: six-string evals give stock's AL
    "PE0": Ref(
        **ruled(lambda case: pe0_native(case), why="guitar strings 6/7: the native bases 35 / 30, stock has six")
    ),
    "GRM3": Ref(
        **ruled(
            lambda case: W("fpu") if case.ebp[0x10] >= 0x18 else None,
            why="strings 6/7: the V row from g_rs7_ns_v, the stock table has six rows",
        )
    ),
}
(
    test_hook_behaves_like_the_stock_bytes,
    test_hook_differs_from_the_stock_bytes_on_purpose,
    test_every_converted_site_has_a_stock_reference_or_a_reason,
) = stock_tests(SPECS, REFS, {})


def test_grm3_full_x87_stack_faults_like_the_old_stub():
    """Strings 6/7 on a full x87 stack: the new hook replays the stock fld, the old stub loaded g_rs7_ns_v; both
    push onto a full stack. The same exit, registers, flags, status word and tags. ST0 is not compared: on a masked stack
    overflow the hardware loads the QNaN indefinite whatever the operand (Intel SDM, FLD); Unicorn does not model the
    stack fault and loads the operand (the old stub's g_rs7_ns_v, the replay's stock row)."""
    from midhook_oracle import FLAG_PATTERNS, Machine

    machine = Machine(SPECS["GRM3"][0])
    old, new = (machine.run(which, GRM3_FULL, FLAG_PATTERNS[1], 0) for which in ("old", "new"))
    assert old["fpu"][4] == new["fpu"][4] == 0  # all eight registers valid before and after: a push onto a full stack
    for key in ("eip", "regs", "flags", "stack", "writes"):
        assert old[key] == new[key], key
    assert old["fpu"][1:] == new["fpu"][1:]
