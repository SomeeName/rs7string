"""Stock reference of the converted mid-hook sites: the game's original bytes against the hooked path, on the machine
and cases of the frozen-stub oracle (midhook_oracle.py). Used by test_midhook_stock.py and test_midhook_stock_features.py.
"""

from __future__ import annotations

from dataclasses import dataclass, field

import capstone
from capstone import x86_const as X86

from emu_common import SECURITY_CHECK_COOKIE
from emu_harness import CODE, TEB, TEXT_VA, game_text, site_va
from midhook_oracle import ARITH, FLAG_PATTERNS, Case, Machine, Spec, brief, flags_dead_at


# ---- stock reference: the game's original bytes ------------------------------------------------------------------------
# The frozen-stub comparison above proves "no change"; it cannot show a bug that the old stub had too (PUBGE2). A stock
# reference runs the site's original instructions instead and compares what the game can observe: the exit taken, the
# GPRs, the live stack, x87/SSE, the DATA and EBP areas, the case's memory and every write outside the DLL image (the
# hook's own state is not game-observable), with the register bits no continuation reads (regs_live_at) masked. The
# hook's helpers run for real on the DLL's default state (six strings, no RS7X chart; reset before every run), so a
# case's `rets` do not apply, unless a site's helpers need game state the emulator lacks (Ref.real). Arithmetic flags
# are compared at a common exit unless flags_dead_at proves it rewrites them before any read; the other flag bits
# always. Writes to the fake TEB (SEH frame bookkeeping of a helper's
# __try) are not game state either.
STOCK_KEYS = ("eip", "regs", "final_esp", "stack", "fpu", "xmm", "data", "ebp_area", "mem", "writes")
_GPR = ("eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp")  # ALL_REGS order
_PARTS = {  # GPR -> {name of a part: the bits it covers}
    "eax": {"eax": 0xFFFFFFFF, "ax": 0xFFFF, "al": 0xFF, "ah": 0xFF00},
    "ebx": {"ebx": 0xFFFFFFFF, "bx": 0xFFFF, "bl": 0xFF, "bh": 0xFF00},
    "ecx": {"ecx": 0xFFFFFFFF, "cx": 0xFFFF, "cl": 0xFF, "ch": 0xFF00},
    "edx": {"edx": 0xFFFFFFFF, "dx": 0xFFFF, "dl": 0xFF, "dh": 0xFF00},
    "esi": {"esi": 0xFFFFFFFF, "si": 0xFFFF},
    "edi": {"edi": 0xFFFFFFFF, "di": 0xFFFF},
}
FULL = 0xFFFFFFFF


def regs_live_at(address: int, budget: int = 80) -> dict:
    """{GPR: the bits of its value at `address` some game instruction may read}: a branch-following scan of the game
    code until the register is written whole. A partial write (AL) does not kill it; a read of a part (AL) makes only
    those bits live. A `ret` (and RET_AT) reads EAX (the return value) and the callee-saved EBX / ESI / EDI, not the
    caller-saved ECX / EDX of the MSVC conventions; __security_check_cookie reads ECX only; an indirect `call` reads
    everything but EAX (below); any other `call`, an indirect jump or the end of the budget make everything still
    undecided live. EBP / ESP are not scanned (always
    compared)."""
    text = game_text()
    if address == RET_AT:  # a return to the caller, as at a `ret`
        return {**dict.fromkeys(_PARTS, FULL), "ecx": 0, "edx": 0}
    if not TEXT_VA <= address < TEXT_VA + len(text):
        return dict.fromkeys(_PARTS, FULL)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    live = dict.fromkeys(_PARTS, 0)
    todo, seen = [(address, frozenset())], set()

    def rest(killed, regs=tuple(_PARTS)):
        for r in regs:
            if r not in killed:
                live[r] = FULL

    while todo:
        pc, killed = todo.pop()
        while (pc, killed) not in seen:
            if budget <= 0:
                rest(killed)
                break
            seen.add((pc, killed))
            budget -= 1
            insn = next(md.disasm(text[pc - TEXT_VA : pc - TEXT_VA + 16], pc), None)
            if insn is None:
                rest(killed)
                break
            reads, writes = ({insn.reg_name(r) for r in group} for group in insn.regs_access())
            for r, parts in _PARTS.items():
                if r not in killed:
                    live[r] |= sum(bits for name, bits in parts.items() if name in reads)
            killed = killed | {r for r in _PARTS if r in writes}
            if insn.mnemonic == "ret":
                rest(killed, ("eax", "ebx", "esi", "edi"))
                break
            if insn.mnemonic == "call" and insn.operands[0].type == X86.X86_OP_IMM:
                if insn.operands[0].imm == SECURITY_CHECK_COOKIE:  # reads ECX, changes no register
                    live["ecx"] |= FULL if "ecx" not in killed else 0
                    pc += insn.size
                    continue
            if insn.mnemonic == "call" and insn.operands[0].type != X86.X86_OP_IMM:
                # An indirect call (a vtable or function pointer) uses a standard MSVC convention: thiscall / fastcall
                # may pass ECX / EDX, never EAX, which the call clobbers. A direct call may be an LTCG custom
                # convention (0x5581C0 takes AL), so it keeps everything live.
                rest(killed | {"eax"})
                break
            if insn.mnemonic == "call" or (insn.mnemonic.startswith("j") and insn.operands[0].type != X86.X86_OP_IMM):
                rest(killed)
                break
            if insn.mnemonic.startswith("j"):
                if insn.mnemonic != "jmp":
                    todo.append((insn.operands[0].imm, killed))
                    pc += insn.size
                else:
                    pc = insn.operands[0].imm
                continue
            pc += insn.size
    return live


def stock_differences(machine: Machine, case: Case, flags: int = FLAG_PATTERNS[0], misalign: int = 0) -> dict:
    """{key: (stock, hooked)} for every observable that differs between the original bytes and the hooked path (a
    Machine built with real_helpers=True). Empty = the hook behaves exactly like the game for this input."""
    from emu_common import dll_image

    base, size = dll_image()[:2]
    states = {}
    for which in ("stock", "new"):
        state = machine.run(which, case, flags, misalign)
        state["writes"] = tuple(
            w for w in state["writes"] if not (base <= w[0] < base + size or TEB <= w[0] < TEB + 0x1000)
        )
        data = bytearray(state["data"])
        data[0x500:0x520] = bytes(0x20)  # the call counter of the stubbed helpers (Machine._capture)
        state["data"] = bytes(data)
        states[which] = state
    stock, new = states["stock"], states["new"]
    # arithmetic flags count where the (common) exit may read them; after different exits the exit already differs
    if stock["eip"] != new["eip"] or flags_dead_at(stock["eip"]):
        for state in (stock, new):
            state["flags"] &= ~ARITH
    if stock["eip"] == new["eip"]:  # register bits no continuation reads are not observable
        live = {**regs_live_at(stock["eip"]), "ebp": FULL, "esp": FULL}
        for state in (stock, new):
            state["regs"] = tuple(v & live[name] for name, v in zip(_GPR, state["regs"]))
    return {key: (stock[key], new[key]) for key in (*STOCK_KEYS, "flags") if stock[key] != new[key]}


def describe(differences: dict) -> str:
    return "; ".join(f"{key}: {brief(*pair)}" for key, pair in differences.items())


RET_AT = CODE + 0x7000  # a return address outside every site: the exit of retargeted cases


def returning_elsewhere(spec: Spec, cases: list, back: int | None = None, offset: int | None = None) -> tuple:
    """The frozen-stub matrix lets some sites return into their own bytes (by default the site address, exit ("resume",
    -length)), which a stock run would take before it executes them. These copies return to RET_AT instead: every
    stack / EBP dword equal to `back` and the exit ("resume", offset) are replaced."""
    import dataclasses

    va = site_va(spec.sid) if back is None else back
    offset = -spec.length if offset is None else offset

    def moved(table):
        return {k: (RET_AT if v == va else v) for k, v in table.items()}

    exits = tuple(RET_AT if e == ("resume", offset) else e for e in spec.exits)
    return dataclasses.replace(spec, exits=exits), [
        dataclasses.replace(c, stack=moved(c.stack), ebp=moved(c.ebp)) for c in cases
    ]


_GPR_INDEX = {name: i for i, name in enumerate(_GPR)}


@dataclass
class Want:
    """An intended difference, exactly: `keys` = the set of observables that differ (stock_differences keys); the
    hook's exit `eip` (an offset from the site, "target", or ("abs", address)), its ESP minus the stock one (`esp`),
    register values (`regs`, name -> value as compared, i.e. masked to the live bits), bytes (`at`: key -> (offset,
    bytes); "mem0" is the first compared range) and the exact span of changed bytes (`span`: key -> (first, last))."""

    keys: frozenset
    eip: object = None
    esp: int | None = None
    regs: dict = field(default_factory=dict)
    at: dict = field(default_factory=dict)
    span: dict = field(default_factory=dict)


def _blobs(differences: dict, key: str) -> tuple:
    if key.startswith("mem") and key != "mem":
        i = int(key[3:])
        return differences["mem"][0][i], differences["mem"][1][i]
    return differences[key]


def assert_want(differences: dict, want: Want, machine: Machine) -> None:
    """The hook differs from the stock bytes by exactly `want` (nothing else differs, every stated value holds)."""
    assert set(differences) == set(want.keys), f"differing {sorted(differences)}, intended {sorted(want.keys)}"
    if want.eip is not None:
        e = want.eip
        address = machine.target if e == "target" else e[1] if isinstance(e, tuple) else machine.va + e
        assert differences["eip"][1] == address, f"exit {differences['eip'][1]:#x}, intended {address:#x}"
    if want.esp is not None:
        delta = differences["final_esp"][1] - differences["final_esp"][0]
        assert delta == want.esp, f"ESP {delta:+#x}, intended {want.esp:+#x}"
    for name, value in want.regs.items():
        got = differences["regs"][1][_GPR_INDEX[name]]
        assert got == value & FULL, f"{name} {got:#x}, intended {value & FULL:#x}"
    for key, (offset, data) in want.at.items():
        got = bytes(_blobs(differences, key)[1][offset : offset + len(data)])
        assert got == data, f"{key}+{offset:#x}: {got.hex(' ')}, intended {data.hex(' ')}"
    for key, (first, last) in want.span.items():
        old, new = _blobs(differences, key)
        changed = [i for i in range(len(old)) if old[i] != new[i]]
        assert changed and (changed[0], changed[-1]) == (first, last), f"{key} changes {changed[:3]}..{changed[-1:]}"


def ruled(expect, why: str, **kw) -> dict:
    """Ref arguments from one rule: `expect(case)` -> Want (the intended difference) or None (behaves like stock)."""
    return dict(
        six=lambda case: expect(case) is None,
        why=why,
        check=lambda differences, case, machine: assert_want(differences, expect(case), machine),
        **kw,
    )


def W(*keys, **values) -> Want:  # noqa: N802 - reads like a table row
    return Want(frozenset(keys), **values)


@dataclass
class Ref:
    """The stock reference of one site. `six(case)`: the cases with six-string input (stock strings, a stock object,
    the shipped switches) where the hook must behave exactly like the original bytes. The other cases differ on
    purpose (`why`: a seven-string input, a native capacity-8 object, a switch); `check(differences, case, machine)` asserts the
    intended difference: exactly which observables differ and the hook's values (mandatory with `why`). `finding(case)`:
    six-string cases where the hook DISAGREES with the game (`note`), strict xfail until the product code changes;
    `finding_check(differences, case, machine)` asserts that the difference is exactly that finding. `real`: the
    hook's helpers run for real (False: stubbed with the case's `rets`, where they need game state the emulator lacks;
    `six` then requires the rets to be the six-string answers). `variant(spec, cases)`: other inputs or exits
    for the stock run (returning_elsewhere, both layouts of a relocated record)."""

    six: object = staticmethod(lambda case: True)
    why: str = ""
    check: object = None
    real: bool = True
    variant: object = None
    finding: object = None
    finding_check: object = None
    note: str = ""


def stock_tests(specs: dict, refs: dict, none: dict):
    """The stock-reference tests of a site table {sid: (Spec, [Case])}: `refs` {sid: Ref}, `none` {sid: why the site has
    no meaningful stock reference}. A test module assigns the three functions to module-level names."""
    from functools import lru_cache

    import pytest

    @lru_cache(maxsize=None)
    def site(sid: str):
        ref = refs[sid]
        spec, cases = specs[sid]
        if ref.variant:
            spec, cases = ref.variant(spec, cases)
        return Machine(spec, real_helpers=ref.real), cases

    def params(want_six: bool):
        for sid, ref in refs.items():
            spec, cases = specs[sid]
            if ref.variant:
                spec, cases = ref.variant(spec, cases)
            for index, case in enumerate(cases):
                if bool(ref.six(case)) != want_six:
                    continue
                marks = ()
                if want_six and ref.finding and ref.finding(case):
                    marks = (pytest.mark.xfail(strict=True, raises=AssertionError, reason=f"{sid}: {ref.note}"),)
                for flags in FLAG_PATTERNS if want_six else FLAG_PATTERNS[:1]:
                    yield pytest.param(sid, index, flags, id=f"{sid}-{case.id}-f{flags:x}", marks=marks)

    @pytest.mark.parametrize("sid,index,flags", list(params(True)))
    def test_hook_behaves_like_the_stock_bytes(sid, index, flags):
        machine, cases = site(sid)
        case, ref = cases[index], refs[sid]
        differences = stock_differences(machine, case, flags)
        if ref.finding and ref.finding(case):
            try:  # exactly the known finding, nothing else: otherwise a real failure, not the expected one
                ref.finding_check(differences, case, machine)
            except AssertionError as error:
                pytest.fail(f"{sid} {case.id}: not the known finding: {error}; {describe(differences)}")
        assert not differences, describe(differences)

    @pytest.mark.parametrize("sid,index,flags", list(params(False)))
    def test_hook_differs_from_the_stock_bytes_on_purpose(sid, index, flags):
        """`check` asserts the intended difference: exactly which observables differ and the hook's values."""
        machine, cases = site(sid)
        refs[sid].check(stock_differences(machine, cases[index], flags), cases[index], machine)

    def test_every_converted_site_has_a_stock_reference_or_a_reason():
        assert not set(refs) & set(none), set(refs) & set(none)
        assert set(refs) | set(none) == set(specs), set(specs) ^ (set(refs) | set(none))
        for sid, ref in refs.items():
            cases = specs[sid][1] if not ref.variant else ref.variant(*specs[sid])[1]
            assert (ref.why and ref.check) or all(ref.six(c) for c in cases), f"{sid}: a difference needs why + check"
            assert not ref.finding or (ref.finding_check and ref.note), f"{sid}: a finding needs its check and note"
        assert all(none.values())

    return (
        test_hook_behaves_like_the_stock_bytes,
        test_hook_differs_from_the_stock_bytes_on_purpose,
        test_every_converted_site_has_a_stock_reference_or_a_reason,
    )
