"""Old-vs-new equivalence and stock references of the sites converted in asm reduction part 2 (stubs frozen at 72da588,
tools/tests/fixtures/midhook_72da588.json):
- PC1, PC4: the per-frame calls of 0x873F80 over a matcher's tuning (light call sites, Before placement);
- PUBZ0-PUBZ6: the standard-zero expected-pitch sites (one light callback; call sites 0 and 4-6, jmp sites 1-3 whose
  replay is the relocated `jmp 873F80h`), old stub Rs7_Publish_ZeroRaw;
- the PUBAN copy's expected-pitch call (Rs7_Mid_PubTailPitch, a light call entry), old function Rs7_Pitch_Raw8.
What is compared: midhook_oracle.py and midhook_stock.py; the tail-pitch entry by its own runs below.
"""

from __future__ import annotations

import struct

import pytest
from unicorn.x86_const import (
    UC_X86_REG_EAX,
    UC_X86_REG_EBP,
    UC_X86_REG_EBX,
    UC_X86_REG_ECX,
    UC_X86_REG_EDI,
    UC_X86_REG_EDX,
    UC_X86_REG_EFLAGS,
    UC_X86_REG_ESI,
    UC_X86_REG_ESP,
)

from emu_harness import ALL_REGS, CODE, DATA, DLL, RET_MAGIC, SNAPSHOT, STACK, Emu, site_va
from midhook_oracle import FLAG_PATTERNS, HELPER_AT, Case, Helper, Spec, oracle_tests, place_frozen
from midhook_stock import RET_AT, Ref, W, ruled, stock_tests

pytestmark = pytest.mark.skipif(
    not DLL.exists() or not (SNAPSHOT / "memory.bin").exists(), reason="needs built DLL and snapshot"
)

D = DATA + 0x2000  # scratch objects
SPECS: dict[str, tuple[Spec, list[Case]]] = {}
GBASE = (40, 45, 50, 55, 59, 64, 35, 30)
BBASE = (28, 33, 38, 43)
ZERO_TUNING = 0x1199D2C  # the game's standard (zero) tuning the PUBZ callers pass in ECX
EXPECTED_PITCH = 0x873F80  # the callee of every PUBZ site


def register(spec: Spec, cases: list[Case]):
    SPECS[spec.sid] = (spec, cases)


def ret(sid: str) -> int:
    return site_va(sid) + 5


# ---- PC1 / PC4: 0x873F80 (ECX = matcher+44h, EDX = instrument, ESI = string, [esp+4] = fret) over the tuning8 tail ----
TAIL = struct.pack("<8h", -2, 0, 1, 0, 0, 0, -1, 3)


def pitch_cases(sid: str) -> list[Case]:
    obj = D + 0x100
    out = [
        Case(
            f"s{string}-i{instrument:x}-t{tailed}",
            regs={UC_X86_REG_ECX: obj + 0x44, UC_X86_REG_EDX: instrument, UC_X86_REG_ESI: string},
            mem={obj + 0x58: TAIL},
            stack={0: ret(sid), 4: 9},
            rets={"Rs7Pitch_Tailed": tailed},
        )
        for string in (0, 5, 6, 7, 8)
        for instrument in (1, 2, 0xFF)
        for tailed in (0, 1)
    ]
    out.append(Case("null", regs={UC_X86_REG_ECX: 0, UC_X86_REG_ESI: 6}, stack={0: ret(sid), 4: 9}))
    return out


for sid in ("PC1", "PC4"):
    register(
        Spec(sid, 5, helpers={"Rs7Pitch_Tailed": Helper(2)}, flags="dead", exits=("target", "resume"), call=True),
        pitch_cases(sid),
    )


# ---- PUBZ0-PUBZ6: 0x873F80 with ECX = the zero tuning; [esp] = the caller's return address, [esp+4] = the fret -------
CALL_SITES = ("PUBZ0", "PUBZ4", "PUBZ5", "PUBZ6")


def zero_cases(sid: str) -> list[Case]:
    back = ret(sid) if sid in CALL_SITES else RET_AT
    return [
        Case(
            f"p{ports:x}-s{string}-i{instrument:x}",
            regs={UC_X86_REG_ECX: ZERO_TUNING, UC_X86_REG_EDX: instrument, UC_X86_REG_ESI: string},
            stack={0: back, 4: 9},
            words={"g_rs7_publish_ports": ports},
        )
        for ports in (0, 1, 2, 0xFFFFFFFF)
        for string in (0, 5, 6, 7, 8)
        for instrument in (0, 1, 2, 0xFF)
    ]


for n in range(7):
    sid = f"PUBZ{n}"
    call = sid in CALL_SITES
    register(
        Spec(
            sid,
            5,
            stub="Rs7_Publish_ZeroRaw",
            flags="dead",  # the old stub left its compare / add flags; the stock entry and the caller rewrite them
            clobbers=("ecx",),  # the old stub returned with ECX = g_rs7_publish_zero8, the hook keeps the caller's
            exits=("target", "resume") if call else (EXPECTED_PITCH, RET_AT),  # a jmp site: its jmp target, or returned
            call=call,
        ),
        zero_cases(sid),
    )

ALWAYS_CALLS: set[str] = set()
(
    test_mid_hook_matches_frozen_stub,
    test_dead_flags_are_rewritten_before_any_read,
    test_every_registered_site_calls_its_helper_at_least_once,
) = oracle_tests(SPECS, ALWAYS_CALLS)


def expected_pitch8(tuning: tuple, instrument: int, string: int, fret: int) -> int:
    """rs7p::ExpectedPitch8: tuning[s] + base[s] + fret, past the table fret - 1 (32-bit)."""
    bass = instrument == 2
    if string >= (4 if bass else 8):
        return (fret - 1) & 0xFFFFFFFF
    return (tuning[string] + (BBASE if bass else GBASE)[string] + fret) & 0xFFFFFFFF


def answered(case: Case, eip, pitch: int) -> object:
    """The hook answered for the callee (stock: the callee's entry, never run here): it returns to the caller with
    EAX = the pitch, EDX = instrument - 2 and the return address popped."""
    instrument = case.regs[UC_X86_REG_EDX]
    return W(
        "eip", "regs", "final_esp", "stack", eip=eip, esp=4, regs={"eax": pitch, "edx": (instrument - 2) & 0xFFFFFFFF}
    )


def pc_native(case: Case):
    """PC1 / PC4, a matcher with the native tail (Rs7Pitch_Tailed stubbed: the registry is DLL state): strings >= 6 are
    answered over the tail, everything else runs the stock callee."""
    string, ecx = case.regs.get(UC_X86_REG_ESI), case.regs.get(UC_X86_REG_ECX)
    if string < 6 or not ecx or not case.rets.get("Rs7Pitch_Tailed"):
        return None
    tail = struct.unpack("<8h", TAIL)
    return answered(case, 5, expected_pitch8(tail, case.regs[UC_X86_REG_EDX], string, 9))


def zero_native(sid: str):
    """With the ports on, guitar strings 6/7 get the base of the eight-entry table (stock: -1 past the six entries)."""

    def rule(case: Case):
        ports, string, instrument = (
            case.words["g_rs7_publish_ports"],
            case.regs[UC_X86_REG_ESI],
            case.regs[UC_X86_REG_EDX],
        )
        if not ports or string not in (6, 7) or instrument == 2:
            return None
        return answered(case, 5 if sid in CALL_SITES else ("abs", RET_AT), GBASE[string] + 9)

    return rule


REFS: dict[str, Ref] = {
    **{
        sid: Ref(**ruled(pc_native, why="strings >= 6 of a tailed matcher: the eight-entry pitch"), real=False)
        for sid in ("PC1", "PC4")
    },
    **{
        f"PUBZ{n}": Ref(**ruled(zero_native(f"PUBZ{n}"), why="guitar strings 6/7 with the ports: the bases 35 / 30"))
        for n in range(7)
    },
}
(
    test_hook_behaves_like_the_stock_bytes,
    test_hook_differs_from_the_stock_bytes_on_purpose,
    test_every_converted_site_has_a_stock_reference_or_a_reason,
) = stock_tests(SPECS, REFS, {})


# ---- the PUBAN copy's expected-pitch call: Rs7_Mid_PubTailPitch (light call entry) against the frozen Rs7_Pitch_Raw8 --
RAW_TUNING = D + 0x700
RAW_TAIL = (3, -1, 0, 2, -5, 1, -2, 4)


@pytest.fixture(scope="module")
def raw8_machine():
    emu = Emu(False)
    old = place_frozen(emu, "Rs7_Pitch_Raw8", HELPER_AT, {}, {})
    out, esp = CODE + 0xE8000, STACK + 0x70000  # below RET_MAGIC (CODE + 0xF0000)
    args = (RET_MAGIC, out, 256, out, EXPECTED_PITCH, out, emu.exports["Rs7_Mid_PubTailPitch"], 1, 0)
    emu.uc.mem_write(esp, struct.pack("<9I", *args))
    emu.run(emu.exports["Rs7Mid_BuildCall"], RET_MAGIC, {UC_X86_REG_ESP: esp})
    assert emu.uc.reg_read(UC_X86_REG_EAX) > 0, "the call entry was rejected"
    return emu, old, out


def run_raw(emu: Emu, start: int, tuning: int, instrument: int, string: int, flags: int, misalign: int):
    esp = STACK + 0x80000 + misalign
    emu.uc.mem_write(RAW_TUNING, struct.pack("<8h", *RAW_TAIL))
    emu.uc.mem_write(esp, struct.pack("<II", RET_MAGIC, 7))
    regs = {
        UC_X86_REG_ESP: esp,
        UC_X86_REG_ECX: tuning,
        UC_X86_REG_EDX: instrument,
        UC_X86_REG_ESI: string,
        UC_X86_REG_EAX: 0xA1A1,
        UC_X86_REG_EBX: 0xB1B1,
        UC_X86_REG_EDI: 0xD1D1,
        UC_X86_REG_EBP: STACK + 0x90000,
        UC_X86_REG_EFLAGS: flags,
    }
    emu.run(start, RET_MAGIC, regs)
    return emu.regs(*ALL_REGS), emu.uc.reg_read(UC_X86_REG_EFLAGS), emu.read(esp - 0x40, 0x48)


@pytest.mark.parametrize("tuning", [RAW_TUNING, 0])
@pytest.mark.parametrize("instrument", [0, 1, 2, 3, 0xFF])
@pytest.mark.parametrize("string", [0, 3, 4, 5, 6, 7, 8, 0xFFFFFFFF])
def test_tail_pitch_entry_matches_the_frozen_raw8(raw8_machine, tuning, instrument, string):
    """Every GPR (EAX = the pitch, EDX = instrument - 2 or kept for a null tuning, ECX / ESI kept, ESP past the return),
    under three incoming flag patterns and two stack alignments. The old function left its `add` flags, which the
    caller's `add esp,4` rewrites (dead); the entry leaves every incoming flag."""
    emu, old, new = raw8_machine
    for flags in FLAG_PATTERNS:
        for misalign in (0, 4):
            want = run_raw(emu, old, tuning, instrument, string, flags, misalign)
            got = run_raw(emu, new, tuning, instrument, string, flags, misalign)
            assert got[0] == want[0]
            assert got[1] == flags


def test_tail_pitch_callback_uses_no_fp():
    """Rs7_Mid_PubTailPitch runs in the light thunk but is not a site (LIGHT_CALLBACKS lists sites): the same check."""
    from test_midhook_emu import _fp_instructions

    emu = Emu(False)
    assert _fp_instructions(emu, emu.exports["Rs7_Mid_PubTailPitch"]) == []
