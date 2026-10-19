#!/usr/bin/env python3
"""Native 8-slot tuner session (issue #18, job 3 "tuner8"): relocated copies of UI_PlayerTuner methods.

A native tuner session (features/tuner/hooks.h) keeps capacity-8 copies of the six per-string session arrays in
a tail of the grown 380h UI_PlayerTuner and its string count / tuning order next to them. The stock
methods index the 6-entry arrays at +38h.. and bound their loops by InstrumentInfo +0Bh; the copies
emitted here are the same functions with

  * the session-array displacements moved to the tail (`arr`, ARRAYS below; whole-operand checked),
  * the tuner's string count read from the session tail instead of InstrumentInfo +0Bh (`cnt`),
  * listed instruction replacements / insertions (`rows`, `pre`): sequence steps through DLL helpers
    (the low-B-first order), label tables, ...,
  * the in-place byte sites (FGR*, CM*: native FretGrid / colour matrix) that sites.json writes into the
    same range applied first, so a copy equals the patched function it replaces,

re-assembled (keystone) with every untouched instruction emitted as its original bytes. The DLL checks
the live function bytes (FNV-1a over the stock range) before it builds a copy and dispatches a call
to the copy only for a native session; every other session runs the unmodified function.

    uv run --with keystone-engine python tools/gen_tuner8s.py          # -> header
    uv run --with keystone-engine python tools/gen_tuner8s.py --list   # every edit
    uv run --with keystone-engine python tools/gen_tuner8s.py --suggest ID
"""

from __future__ import annotations

import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

import capstone

import gen_fncopy as fc

REPO = Path(__file__).resolve().parents[1]
OUT = REPO / "src" / "features" / "tuner" / "tuner8s.generated.h"
OUT_IDS = REPO / "src" / "features" / "tuner" / "tuner8s_ids.generated.h"  # the entry ids only (stubs, registry)
SITES = REPO / "sites.json"

# Session tail (features/tuner/hooks.h rs7tu): stock array base -> (tail base, stock bytes)
ARRAYS = {
    "open": (0x038, 0x280, 0x18),  # int32 target MIDI[6] -> [8]
    "off": (0x050, 0x2A0, 0x18),  # int32 string offset[6] -> [8]
    "peak": (0x080, 0x2C8, 0x18),  # float peak[6] -> [8] (open + 48h kept)
    "letters": (0x114, 0x2E8, 0x30),  # {letter, sharp}[6] -> [8]
    "picks": (0x144, 0x328, 0x18),  # pick entity[6] -> [8]
    "last": (0x160, 0x348, 0x18),  # int32 pick state[6] -> [8]
}
COUNT = 0x368  # u8 tuner string count of the session
SESSION_STOCK = 0x278
SESS_CODES = {"eax": 0, "ecx": 1, "edx": 2, "ebx": 3, "esi": 6, "edi": 7, "arg1": 8, "nt": 9, "none": 15}
COPY_BASE = 0x40  # SYN + COPY_BASE + spec index: a call into another copy

# Synthetic call targets for DLL symbols (resolved by the runtime builder, kTu8Syms order).
# kTu8Sym + k (hooks_copies.cpp SymAddress): call-in entries (g_rs7_callin_*, hooks_callin.cpp) or DLL exports
SYMS = [
    "g_rs7_callin_Tu8Advance",
    "Rs7Tu8_OnSelect",
    "g_rs7_callin_Tu8First",
    "g_rs7_tu8_labels",
    "Rs7Asset_LetterInline",
]
SYN = 0x70000000

REG = r"(?:e[abcd]x|e[sd]i|ebp|esp)"


@dataclass
class Fn:
    id: str
    start: int
    end: int  # exclusive
    sess: str  # where the entry finds the session: a register, "arg1" ([esp+4]) or "nt"
    # (EDX = the embedded NoteTuner, session + 0D0h)
    arr: tuple = ()  # instruction vas whose session-array displacement moves to the tail
    cnt: dict = field(default_factory=dict)  # va -> new memory operand text for the count byte
    rows: dict = field(default_factory=dict)  # va -> replacement asm lines ([] = delete)
    pre: dict = field(default_factory=dict)  # va -> asm lines inserted before the instruction
    sub: dict = field(default_factory=dict)  # va -> (old, new) operand text substitution
    site: bool = True  # False: no entry site; only other copies call it (COPY_<id>)


def tail_disp(d: int) -> int | None:
    for _, (old, new, n) in ARRAYS.items():
        if old <= d < old + n:
            return new + (d - old)
    return None


# ---------------------------------------------------------------------------------------------
# The ported functions. Addresses are the September 2022 exe (image base 400000h). Evidence and
# the per-function reasoning: docs/ARCHITECTURE.md "Native session ports".
FNS: list[Fn] = []


def fn(*a, **k):
    FNS.append(Fn(*a, **k))


ADV_NEXT = ["push eax", "push 1", "push {s}", "call SYM_g_rs7_callin_Tu8Advance", "test eax, eax", "pop eax"]
ADV_PREV = ["push eax", "push -1", "push {s}", "call SYM_g_rs7_callin_Tu8Advance", "test eax, eax", "pop eax"]


def adv(lines, s, then):
    return [x.format(s=s) for x in lines] + [then]


# UI_PlayerTuner_SelectString (ESI session, EDI string): targets read from the tail; the selection
# hook (labels, detector low range, log) runs at the common exit 0x5B055D.
fn(
    "TS8SEL",
    0x5B0490,
    0x5B0561,
    "esi",
    arr=(0x5B0493, 0x5B04C4, 0x5B04CB, 0x5B050D, 0x5B0545),
    sub={
        0x5B0508: ("0x5b4de0", "COPY_TS8FLA"),
        0x5B053D: ("0x5b4de0", "COPY_TS8FLA"),
        0x5B0558: ("0x85db30", "COPY_TS8NTI"),
    },
    pre={0x5B055D: ["pushad", "push edi", "push esi", "call SYM_Rs7Tu8_OnSelect", "add esp, 8", "popad"]},
)

# UI_PlayerTuner_StepStringA / B (EAX session; arrow keys): steps through the tuning order.
fn(
    "TS8STA",
    0x5B03F0,
    0x5B0437,
    "eax",
    rows={
        0x5B0409: adv(ADV_NEXT, "esi", "je L5b0433"),
        0x5B040B: [],
        0x5B040D: ["mov edi, dword ptr [esi + 0x74]"],
        0x5B0410: [],
        0x5B041C: adv(ADV_PREV, "esi", "je L5b0433"),
        0x5B041E: [],
        0x5B0424: [],
        0x5B0426: [],
        0x5B0428: ["mov edi, dword ptr [esi + 0x74]"],
        0x5B042B: [],
    },
)
fn(
    "TS8STB",
    0x5B0440,
    0x5B0487,
    "eax",
    rows={
        0x5B0454: adv(ADV_PREV, "esi", "je L5b0483"),
        0x5B0456: [],
        0x5B045C: [],
        0x5B045E: [],
        0x5B0460: ["mov edi, dword ptr [esi + 0x74]"],
        0x5B0463: [],
        0x5B0474: adv(ADV_NEXT, "esi", "je L5b0483"),
        0x5B0476: [],
        0x5B0478: ["mov edi, dword ptr [esi + 0x74]"],
        0x5B047B: [],
    },
)

# 0x5B00C0 (mode 2 entry; [ebp+8] session = EBX, [ebp+0Ch] string, EAX = EDI sub mode): the string bound
# uses the session count; a menu start (sub mode 0, string 0; the five callers 0x5AE950, 0x73FA50,
# 0x73FFA0, 0x7441B0, 0x744650 pass 0, or 5 for the reverse pass) begins at the first string of the
# order. Mode 0's fall-back (sub mode 1, the current string) keeps its string.
fn(
    "TS8M2E",
    0x5B00C0,
    0x5B0265,
    "arg1",
    cnt={0x5B00DF: "byte ptr [ebx + 0x368]"},
    rows={
        0x5B0135: [
            "test edi, edi",
            "mov edi, dword ptr [ebp + 0xc]",
            "jne tu8_m2e_keep",
            "test edi, edi",
            "jne tu8_m2e_keep",
            "movzx edi, byte ptr [ebx + 0x36c]",
            # Mode 5 entry selects again from this argument at 0x5B020E.
            "mov dword ptr [ebp + 0xc], edi",
            "tu8_m2e_keep:",
        ]
    },
)

# UI_PlayerTuner_UpdateMode2Precision (EBX session): forward / reverse steps, targets.
fn(
    "TS8MD2",
    0x5B3570,
    0x5B37FE,
    "arg1",
    arr=(0x5B3698, 0x5B36E7, 0x5B36EB),
    rows={
        0x5B35E0: adv(ADV_PREV, "ebx", "je L5b3635"),
        0x5B35E3: [],
        0x5B35EF: [],
        0x5B3610: adv(ADV_NEXT, "ebx", "je L5b3635"),
        0x5B3613: [],
        0x5B361F: [],
    },
)

# UI_PlayerTuner_UpdateMode0Check (ESI session) and mode 1 (capo, ESI session): forward steps, targets.
fn(
    "TS8MD0",
    0x5B3800,
    0x5B3950,
    "eax",
    arr=(0x5B38B0,),
    rows={0x5B3904: adv(ADV_NEXT, "esi", "je L5b3929"), 0x5B3906: [], 0x5B3912: []},
)
fn(
    "TS8MD1",
    0x5B3950,
    0x5B3AAC,
    "eax",
    arr=(0x5B3A2A,),
    rows={0x5B3A81: adv(ADV_NEXT, "esi", "je L5b3a9e"), 0x5B3A83: [], 0x5B3A87: ["mov edi, dword ptr [esi + 0x74]"]},
)

# Mode 3 (EBX session): forward / reverse steps, targets.
fn(
    "TS8MD3",
    0x5B3AB0,
    0x5B3D28,
    "arg1",
    arr=(0x5B3BBA, 0x5B3C0C),
    rows={
        0x5B3B03: adv(ADV_PREV, "ebx", "je L5b3b59"),
        0x5B3B07: [],
        0x5B3B13: [],
        0x5B3B34: adv(ADV_NEXT, "ebx", "je L5b3b59"),
        0x5B3B37: [],
        0x5B3B43: [],
    },
)


# --- visual methods (letters, picks, peaks, fades) ---
GS = ("+ 4]", "+ 0x40]")  # GuitarStrings main[s]: stock +4, native +40h (features/strings/strings.h)


def tc(reg, disp=COUNT):
    return f"byte ptr [{reg} + 0x{disp:x}]"


# UI_PlayerTuner_FadeIn (ESI session): letters, sharps and picks of every string fade in.
fn(
    "TS8FIN",
    0x5AEB50,
    0x5AEC93,
    "esi",
    arr=(0x5AEBC6, 0x5AEBF8, 0x5AEC25, 0x5AEC52),
    cnt={0x5AEBBA: tc("esi"), 0x5AEC83: tc("esi")},
)
# UI_PlayerTuner_FadeOut (EDI session): the same fading out.
fn(
    "TS8FOU",
    0x5AECA0,
    0x5AEF33,
    "edi",
    arr=(0x5AED10, 0x5AED42, 0x5AED6F, 0x5AED9C),
    cnt={0x5AED04: tc("edi"), 0x5AEDCD: tc("edi")},
)
# 0x5B0A50 (EDI session): stops the pluck loop of every string (GuitarStrings main[s], native count).
fn(
    "TS8PLS",
    0x5B0A50,
    0x5B0A92,
    "edi",
    cnt={0x5B0A5B: tc("edi"), 0x5B0A6D: "byte ptr [ecx + 0x40]", 0x5B0A84: tc("edi")},
    sub={0x5B0A75: GS},
)
# 0x5B0AA0 (EDI session): per-string peak level -> string vibration; peaks + 48h = targets kept.
fn(
    "TS8PEK",
    0x5B0AA0,
    0x5B0C14,
    "edi",
    cnt={
        0x5B0B15: tc("edi"),
        0x5B0BBB: tc("edi"),
        0x5B0BDA: tc("edi"),
        0x5B0C04: tc("edi"),
        0x5B0B6D: "byte ptr [edx + 0x40]",
        0x5B0BA0: "byte ptr [edx + 0x40]",
        0x5B0BED: "byte ptr [edx + 0x40]",
    },
    sub={0x5B0B1E: ("[edi + 0x80]", "[edi + 0x2c8]"), 0x5B0B75: GS, 0x5B0BA8: GS, 0x5B0BF5: GS},
)
# UI_PlayerTuner_SetPegNoteLetters (ESI session): a letter (+ sharp) per string from its target.
fn(
    "TS8LET",
    0x5B1820,
    0x5B1A10,
    "esi",
    arr=(0x5B1858, 0x5B185B, 0x5B1883, 0x5B18F9, 0x5B1912, 0x5B1968, 0x5B1981, 0x5B19B6, 0x5B19D4),
    cnt={0x5B186D: tc("esi"), 0x5B19FA: tc("esi")},
)
# UI_PlayerTuner_PlacePegLetters ([esp+4] session): each letter at its peg's locator. The side split
# (count >> 1) keeps the stock count (strings 0-5 as stock, 6 / 7 on the side of 3-5, as the legacy
# 7th letter); a string without a taken peg slot (no 8th peg) is skipped instead of reading slot 0's
# NULL entity (the stock code never reaches that). The in-line byte of the letter turn rule (0x5B1B55, the
# stock site LT1) comes from Rs7Asset_LetterInline: a guitar showing the 7-string in-line headstock on a 3+3
# frame uses the in-line rule (features/headstock/assetload.h); AL stays 0 (popad), ZF = "not in-line" for the stock jz.
fn(
    "TS8LPL",
    0x5B1A10,
    0x5B1D95,
    "arg1",
    arr=(0x5B1CDF, 0x5B1D1D, 0x5B1D32, 0x5B1D48),
    rows={
        0x5B1A9E: ["jmp L5b1d74"],
        0x5B1B55: ["pushad", "push edi", "call SYM_Rs7Asset_LetterInline", "add esp, 4", "test eax, eax", "popad"],
        0x5B1D82: ["mov edx, dword ptr [ebp + 8]", "cmp al, byte ptr [edx + 0x368]"],
    },
)
# UI_PlayerTuner_ReleasePegLetters (EDI session; teardown).
fn(
    "TS8LRL",
    0x5B1DA0,
    0x5B1E50,
    "edi",
    arr=(0x5B1DC3, 0x5B1DE4, 0x5B1DF6, 0x5B1DFD, 0x5B1E1A, 0x5B1E2C),
    cnt={0x5B1DB5: tc("edi"), 0x5B1E41: tc("edi")},
)
# UI_PlayerTuner_SetupPicks ([esp+4] session = EBX): a pick marker per string on the native FretGrid.
fn(
    "TS8PKS",
    0x5B1E50,
    0x5B1F72,
    "arg1",
    cnt={0x5B1E60: tc("ebx"), 0x5B1F58: tc("ebx")},
    sub={0x5B1EE4: ("+ 0x144]", "+ 0x328]")},
)
# Pick release (EDI session; teardown).
fn(
    "TS8PKR",
    0x5B1F80,
    0x5B1FDC,
    "edi",
    arr=(0x5B1F93,),
    cnt={0x5B1F8B: tc("edi"), 0x5B1FD2: tc("edi")},
    sub={0x5B1F9A: ("+ 0x144]", "+ 0x328]")},
)
# UI_PlayerTuner_PlacePicks (ESI session; per frame).
fn("TS8PKP", 0x5B1FE0, 0x5B20DC, "esi", arr=(0x5B20A6,), cnt={0x5B1FFC: tc("esi"), 0x5B20CC: tc("esi")})
# UI_PlayerTuner_PickColourTuned (args session, string) and its sister 0x5B2340 (ECX session, EAX
# string): only UI_PlayerTuner_PickTunedLogo calls them; its copy calls these copies.
fn("TS8PKC", 0x5B20E0, 0x5B233D, "none", site=False, arr=(0x5B210E,), cnt={0x5B2102: tc("ecx")})
fn("TS8PKA", 0x5B2340, 0x5B2556, "none", site=False, arr=(0x5B2368,), cnt={0x5B235C: tc("ecx")})
# UI_PlayerTuner_PickTunedLogo (ECX session = EDI, EDX string = ESI).
fn(
    "TS8PKL",
    0x5B2560,
    0x5B2624,
    "ecx",
    arr=(0x5B2594, 0x5B25C2),
    cnt={0x5B2574: tc("edi")},
    sub={0x5B2584: ("0x5b20e0", "COPY_TS8PKC"), 0x5B258F: ("0x5b2340", "COPY_TS8PKA")},
)
# 0x5B2630 (ESI session): the tuned logo off on every string.
fn("TS8LOF", 0x5B2630, 0x5B2678, "esi", cnt={0x5B2639: tc("esi"), 0x5B2670: tc("esi")})
# 0x5B3130 (ECX session = EDI): the sharp / flat hint from the current target.
fn("TS8HNT", 0x5B3130, 0x5B3567, "ecx", arr=(0x5B315A, 0x5B315E))
# UI_PlayerTuner_InitFlashTuner (ECX = the StringTuner component, [esp+4] string; only SelectString
# calls it, its copy calls this copy): the 3D peg widget at the string's peg. The string is clamped
# to the PegSet's active pegs (stock: InstrumentInfo +0Bh - 1), so string 6 / 7 reaches peg 7 and a
# missing 8th peg never reads slot NULL.
fn(
    "TS8FLA",
    0x5B4DE0,
    0x5B540F,
    "none",
    site=False,
    rows={
        0x5B4E0F: ["mov ecx, dword ptr [eax + 0x18]", "mov ecx, dword ptr [ecx + 0xc]"],
        0x5B4E12: ["movzx ecx, word ptr [ecx + 4]"],
        0x5B4E18: ["jl L5b4e1d"],
    },
)
# UI_NoteTuner_Initialize (EDX = session + 0D0h; only SelectString calls it): string clamp by the
# session count, labels from the 8-entry table (strings 6 / 7: their open note), FretGrid lane of the
# string (native cells 6 / 7).
fn(
    "TS8NTI",
    0x85DB30,
    0x85DF66,
    "none",
    site=False,
    cnt={0x85DB4F: tc("edx", COUNT - 0xD0)},
    sub={0x85DD94: ("0x12e8644", "SYM_g_rs7_tu8_labels"), 0x85DE55: ("0x12e8644", "SYM_g_rs7_tu8_labels")},
)


# ---------------------------------------------------------------------------------------------
_BYTE_SITES: dict | None = None


def load_byte_sites(lo: int, hi: int) -> dict[int, tuple[bytes, bytes, str]]:
    """In-place `bytes` sites (FGR*, CM*) inside [lo, hi): va -> (expect, write, id)."""
    global _BYTE_SITES
    if _BYTE_SITES is None:
        _BYTE_SITES = _all_byte_sites()
    return {va: v for va, v in _BYTE_SITES.items() if lo <= va < hi}


def _all_byte_sites():
    import sitecheck
    import sitetable

    text, base = fc.load_text()
    _, sites = sitetable.load(SITES)
    out = {}
    for s in sites:
        if s.kind != "bytes" or not (s.id.startswith("FGR") or s.id.startswith("CM")):
            continue
        ok, msg, va, _ = sitecheck.check_site(text, base, s)
        assert ok, f"{s.id}: {msg}"
        out[va] = (s.expect, s.write, s.id)
    return out


def mem_operand(op_str: str) -> str | None:
    m = re.search(r"(?:byte|word|dword|qword|tbyte) ptr \[[^\]]*\]", op_str)
    return m.group(0) if m else None


def retarget_disp(op_str: str, d: int, new: int) -> str:
    needle = f"+ 0x{d:x}]"
    assert needle in op_str, (op_str, d)
    return op_str.replace(needle, f"+ 0x{new:x}]")


def is_branch(i) -> bool:
    return i.group(capstone.CS_GRP_JUMP) or i.group(capstone.CS_GRP_CALL)


def rows_for(f: Fn, text: bytes, base: int):
    stock = bytearray(text[f.start - base : f.end - base])
    applied = []
    for va, (expect, write, sid) in load_byte_sites(f.start, f.end).items():
        o = va - f.start
        assert bytes(stock[o : o + len(expect)]) == expect, sid
        stock[o : o + len(write)] = write
        applied.append(sid)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    insns = list(md.disasm(bytes(stock), f.start))
    assert sum(i.size for i in insns) == len(stock), f"{f.id}: disassembly does not cover the range"
    last = [i for i in insns if i.mnemonic != "int3"][-1]
    assert last.mnemonic in ("ret", "jmp"), f"{f.id}: the range ends inside the function ({last.mnemonic})"
    vas = {i.address for i in insns}
    for va in list(f.arr) + list(f.cnt) + list(f.rows) + list(f.pre) + list(f.sub):
        assert va in vas, f"{f.id}: {va:#x} is not an instruction boundary"
    targets = set()
    rows, log = [], []
    for i in insns:
        if is_branch(i) and i.operands and i.operands[0].type == capstone.x86.X86_OP_IMM:
            t = i.operands[0].imm & 0xFFFFFFFF
            if f.start <= t < f.end:
                assert t in vas, f"{f.id}: branch {i.address:#x} into an instruction"
                targets.add(t)
        if i.mnemonic == "jmp" and i.operands and i.operands[0].type == capstone.x86.X86_OP_MEM:
            raise AssertionError(f"{f.id}: indirect jmp at {i.address:#x} (jump table?)")
    for i in insns:
        a = i.address
        rows.append(("label", a))
        for line in f.pre.get(a, []):
            rows.append(("asm", line, a))
            log.append(f"{a:08X} + {line}")
        if a in f.rows:
            for line in f.rows[a]:
                rows.append(("asm", line, a))
            log.append(f"{a:08X} {i.mnemonic} {i.op_str} -> {'; '.join(f.rows[a]) or '(removed)'}")
            continue
        op = i.op_str
        edited = False
        if a in f.arr:
            mem = mem_operand(op)
            assert mem, f"{f.id}: {a:#x} has no memory operand"
            m = re.search(r"\+ 0x([0-9a-f]+)\]", mem)
            d = int(m.group(1), 16)
            new = tail_disp(d)
            assert new is not None, f"{f.id}: {a:#x} displacement {d:#x} is not a session array"
            op = retarget_disp(op, d, new)
            edited = True
        if a in f.cnt:
            mem = mem_operand(op)
            assert mem and mem.startswith("byte ptr [") and mem.endswith("+ 0xb]"), (f.id, hex(a), op)
            op = op.replace(mem, f.cnt[a])
            edited = True
        if a in f.sub:
            old, new = f.sub[a]
            assert old in op, (f.id, hex(a), op, old)
            op = op.replace(old, new)
            edited = True
        if edited:
            rows.append(("asm", f"{i.mnemonic} {op}", a))
            log.append(f"{a:08X} {i.mnemonic} {i.op_str} -> {op}")
        elif is_branch(i) and i.operands and i.operands[0].type == capstone.x86.X86_OP_IMM:
            t = i.operands[0].imm & 0xFFFFFFFF
            dest = f"L{t:x}" if f.start <= t < f.end else f"0x{t:x}"
            rows.append(("branch", f"{i.mnemonic} {dest}", a, t))
        else:
            rows.append(("raw", bytes(i.bytes), a))
    return bytes(text[f.start - base : f.end - base]), rows, log, applied


def assemble(f: Fn, text: bytes, base: int):
    from keystone import Ks, KS_ARCH_X86, KS_MODE_32

    stock, rows, log, applied = rows_for(f, text, base)
    lines = []
    for r in rows:
        if r[0] == "label":
            lines.append(f"L{r[1]:x}:")
        elif r[0] == "raw":
            lines.append(".byte " + ", ".join(f"0x{b:02x}" for b in r[1]))
        else:
            line = r[1]
            for k, name in enumerate(SYMS):
                line = line.replace(f"SYM_{name}", f"0x{SYN + k:x}")
            for k, g in enumerate(FNS):
                line = re.sub(r"\bCOPY_" + g.id + r"\b", f"0x{SYN + COPY_BASE + k:x}", line)
            lines.append(line)
    code, _ = Ks(KS_ARCH_X86, KS_MODE_32).asm("\n".join(lines), addr=0)
    code = bytes(code)
    # Verify: walk the rows against the output, one instruction per non-label row.
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    pos, labels, checks = 0, {}, []
    for r in rows:
        if r[0] == "label":
            labels[r[1]] = pos
            continue
        if r[0] == "asm" and r[1].endswith(":"):  # a local label inside replacement rows
            continue
        i = next(md.disasm(code[pos : pos + 16], pos), None)
        assert i is not None, f"{f.id}: undecodable output at {pos:#x}"
        if r[0] == "raw":
            assert i.bytes == r[1], f"{f.id}: raw row {r[2]:#x} re-encoded"
        checks.append((r, i))
        pos += i.size
    assert pos == len(code), f"{f.id}: {len(code) - pos} trailing bytes"
    rels = []
    for r, i in checks:
        if is_branch(i) and i.operands and i.operands[0].type == capstone.x86.X86_OP_IMM:
            t = i.operands[0].imm & 0xFFFFFFFF
            if r[0] == "branch":
                want = labels[r[3]] if f.start <= r[3] < f.end else r[3]
                assert t == want, f"{f.id}: branch {r[2]:#x} -> {t:#x}, want {want:#x}"
            if t >= len(code):
                assert i.size >= 5 and code[i.address + i.size - 4 : i.address + i.size] == (
                    t - (i.address + i.size)
                ).to_bytes(4, "little", signed=True), f"{f.id}: rel32 at {i.address:#x}"
                rels.append((i.address + i.size - 4, t, 0))
        else:
            # Absolute references to DLL data (SYM_g_*): the dword holding a synthetic address.
            for k in range(i.size - 3):
                v = int.from_bytes(code[i.address + k : i.address + k + 4], "little")
                if SYN <= v < SYN + 0x100:
                    rels.append((i.address + k, v, 1))
    return stock, code, rels, log, applied


def entry_len(f: Fn, text: bytes, base: int) -> int:
    """Bytes the entry jmp overwrites: the shortest whole-instruction prefix >= 5, no branch in it."""
    n = 0
    for i in capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32).disasm(
        text[f.start - base : f.end - base], f.start
    ):
        if n >= 5:
            break
        assert not (i.mnemonic.startswith("j") or i.mnemonic.startswith("call")), f"{f.id}: branch in entry"
        n += i.size
    return n


def render(text: bytes, base: int) -> str:
    out = [
        "// Generated by tools/gen_tuner8s.py from original/Rocksmith2014.unpacked.exe; do not edit.",
        "// Native 8-slot tuner session copies (features/tuner/hooks.h). Symbols: kTu8Syms order.",
        "#pragma once",
    ]
    specs = []
    for f in FNS:
        stock, code, rels, log, applied = assemble(f, text, base)
        out.append(
            f"// {f.id} {f.start:08X}..{f.end:08X}: {len(log)} edits"
            + (f"; byte sites in the copy: {', '.join(sorted(applied))}" if applied else "")
        )
        out.append(f"static const unsigned char kTu8Code_{f.id}[] = {{")
        for k in range(0, len(code), 24):
            out.append("    " + ",".join(f"0x{b:02x}" for b in code[k : k + 24]) + ",")
        out.append("};")
        out.append(f"static const Tu8Rel kTu8Rel_{f.id}[] = {{")
        for off, t, ab in rels:
            sym = f"kTu8Sym + {t - SYN}" if SYN <= t < SYN + 0x100 else f"0x{t:x}"
            out.append(f"    {{{off}, {sym}, {ab}}},")
        if not rels:
            out.append("    {0, 0, 0},")
        out.append("};")
        specs.append(
            f'    {{"{f.id}", 0x{f.start:x}, {len(stock)}, 0x{fc.fnv1a(stock):08x}u, {entry_len(f, text, base) if f.site else 0}, {SESS_CODES[f.sess]}, '
            f"kTu8Code_{f.id}, sizeof kTu8Code_{f.id}, kTu8Rel_{f.id}, {len(rels)}}},"
        )
    out.append("static const Tu8Spec kTu8Specs[] = {")
    out += specs
    out.append("};")
    out.append(f"constexpr int kTu8SpecCount = {len(FNS)};")
    return "\n".join(out) + "\n"


def render_ids() -> str:
    out = [
        "// Generated by tools/gen_tuner8s.py; do not edit.",
        "// X(id, spec index): the entry sites of the native tuner session copies (features/tuner/hooks.h).",
        "#pragma once",
        "#define RS7_TU8S_SPECS(X) \\",
    ]
    out += [f"    X({f.id}, {k}) \\" for k, f in enumerate(FNS) if f.site]
    out.append("")
    out.append(f"#define RS7_TU8S_COUNT {len(FNS)}  // every copy, also those without an entry site")
    return "\n".join(out) + "\n"


def site_lines(text: bytes, base: int) -> list[str]:
    """sites.json rows for the entries: a pattern unique in .text from the function start."""
    rows = []
    for f in FNS:
        if not f.site:
            continue
        o = f.start - base
        n = 16
        while text.count(text[o : o + n]) > 1:
            n += 4
        pat = " ".join(f"{b:02X}" for b in text[o : o + n])
        e = entry_len(f, text, base)
        exp = " ".join(f"{b:02X}" for b in text[o : o + e])
        rows.append(
            json.dumps(
                {
                    "id": f.id,
                    "kind": "jmp",
                    "length": e,
                    "pattern": pat,
                    "offset": "0x0",
                    "expect": exp,
                    "tuner": "native",
                    "note": f"CONFIRMED {f.start:08X}: [neck] native_tuner session copy entry "
                    f"(tools/gen_tuner8s.py, docs/ARCHITECTURE.md)",
                }
            )
        )
    return rows


def suggest(f: Fn, text: bytes, base: int):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    for i in md.disasm(text[f.start - base : f.end - base], f.start):
        mem = mem_operand(i.op_str)
        if not mem:
            continue
        m = re.search(r"\+ 0x([0-9a-f]+)\]", mem)
        d = int(m.group(1), 16) if m else None
        tag = ""
        if d is not None and tail_disp(d) is not None:
            tag = "ARR"
        if mem.startswith("byte ptr") and mem.endswith("+ 0xb]"):
            tag = "CNT"
        if "+ 0x74]" in mem:
            tag = "CUR"
        if tag:
            mark = "*" if i.address in f.arr or i.address in f.cnt or i.address in f.rows else " "
            print(f"{mark}{tag} {i.address:08X} {i.mnemonic} {i.op_str}")


def main(argv):
    text, base = fc.load_text()
    if "--suggest" in argv:
        fid = argv[argv.index("--suggest") + 1]
        suggest(next(f for f in FNS if f.id == fid), text, base)
        return 0
    if "--sites" in argv:
        print(",\n".join("    " + r for r in site_lines(text, base)))
        return 0
    if "--list" in argv:
        for f in FNS:
            _, _, rels, log, applied = assemble(f, text, base)
            print(f"== {f.id} {f.start:08X}..{f.end:08X} ({len(rels)} rel32; byte sites {applied})")
            for line in log:
                print("  " + line)
        return 0
    OUT.write_text(render(text, base), encoding="utf-8", newline="\n")
    OUT_IDS.write_text(render_ids(), encoding="utf-8", newline="\n")
    print(f"{OUT}: {len(FNS)} functions")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
