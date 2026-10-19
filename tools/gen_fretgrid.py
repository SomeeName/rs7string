#!/usr/bin/env python3
"""Native capacity-8 Rnd_FretGrid (issue #18 stage 5 area 1): the in-place layout patches.

. Stock layout: 26 cell rows of 4 + 6 * 4Ch (stride
1CCh) at +4, 26 fret-wire rows of A0h + 6 * 30h (stride 1C0h) at +2EC0h, owner size 72A0h. Native layout
(capacity 8, [neck] native_fretgrid=1): cell rows 4 + 8 * 4Ch (264h) at +4, wire rows A0h + 8 * 30h (220h) at
+72A0h, owner size A9E0h; the non-string tail (+5C40h .. +729Fh) is unchanged.

Every edit is a same-length byte patch of one immediate / displacement / count inside the stock function (a
"bytes" site in sites.json, written only with the switch on). The layout is global (every FretGrid), so no
per-object dispatch is needed and no function copy is built. The active string counts (info +0Bh) stay as
they are; the active-count bounds (setters, error trigger, record update) are NOT touched here.

    uv run python tools/gen_fretgrid.py --list      # print every edit (review)
    uv run python tools/gen_fretgrid.py --write     # append the missing sites to sites.json, write
                                                    # src/features/fretgrid/sites.generated.inc
"""

from __future__ import annotations

import json
import re
import struct
import sys
from pathlib import Path

import capstone

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_fncopy  # noqa: E402

REPO = Path(__file__).resolve().parents[1]
SITES = REPO / "sites.json"
INC = REPO / "src" / "features" / "fretgrid" / "sites.generated.inc"

CELL, CELL_N = 0x1CC, 0x264  # cell row stride (4 + 6*4C -> 4 + 8*4C)
WIRE, WIRE_N = 0x1C0, 0x220  # fret-wire row stride (A0 + 6*30 -> A0 + 8*30)
BASE, BASE_N = 0x2EC0, 0x72A0  # fret-wire base
SIZE, SIZE_N = 0x72A0, 0xA9E0  # owner allocation / sized free
DTOR, DTOR_N = 0x5CE0, 0xAA80  # reverse destructor anchor (end of the wire rows + A0)
SHIFT = CELL - CELL_N  # ApplyOffset displacements relative to the post-incremented row pointer

# (va, old, new, function, role). The value must be one operand (imm or disp) of the instruction at va.
EDITS = [
    # Rnd_FretGrid_Ctor 0x58F090
    (0x58F0B5, 6, 8, "58F090 ctor", "cells per row 6 -> 8"),
    (0x58F0C5, CELL, CELL_N, "58F090 ctor", "cell row stride"),
    (0x58F0CE, BASE, BASE_N, "58F090 ctor", "wire base"),
    (0x58F0E5, 6, 8, "58F090 ctor", "wire records per row 6 -> 8"),
    (0x58F125, WIRE, WIRE_N, "58F090 ctor", "wire row stride"),
    # allocation / release of the owner
    (0x592660, SIZE, SIZE_N, "5925F0 guitar ctor", "FretGrid allocation size"),
    (0x5927E6, SIZE, SIZE_N, "592770 guitar release", "FretGrid sized free"),
    # reverse destructor 0x592900
    (0x59294D, DTOR, DTOR_N, "592900 dtor", "wire rows end anchor"),
    (0x592958, 6, 8, "592900 dtor", "records destroyed per row 6 -> 8"),
    (0x59295C, WIRE, WIRE_N, "592900 dtor", "wire row stride"),
    # build 0x58F8A0
    (0x58F93D, CELL, CELL_N, "58F8A0 build", "cell row stride"),
    # ApplyOffset 0x58FCF0: the stride add; the displacements are generated below
    (0x58FD23, CELL, CELL_N, "58FCF0 ApplyOffset", "cell row stride (post-increment)"),
    # GetCellPos 0x58FE00
    (0x58FE25, CELL, CELL_N, "58FE00 GetCellPos", "cell row stride"),
    # unload 0x58F5E0 / row unload 0x58E5F0
    (0x58F643, WIRE, WIRE_N, "58F5E0 unload", "wire row stride"),
    (0x58F649, BASE, BASE_N, "58F5E0 unload", "wire base"),
    (0x58E5FD, 6, 8, "58E5F0 row unload", "records per row 6 -> 8"),
    # flush 0x58FEF0 / row flush 0x58E730
    (0x58FF36, BASE, BASE_N, "58FEF0 flush", "wire base"),
    (0x58FF51, WIRE, WIRE_N, "58FEF0 flush", "wire row stride"),
    (0x58E746, 6, 8, "58E730 row flush", "records per row 6 -> 8"),
    # 0x590460 wrapper
    (0x59048E, BASE, BASE_N, "590460 wrapper", "wire base"),
    (0x5904A3, WIRE, WIRE_N, "590460 wrapper", "wire row stride"),
    # 0x5904F0 state reset
    (0x5904FE, BASE, BASE_N, "5904F0 state reset", "wire base"),
    (0x590531, CELL, CELL_N, "5904F0 state reset", "cell row stride"),
    (0x590537, WIRE, WIRE_N, "5904F0 state reset", "wire row stride"),
    # setters 0x590370 (the `cmp al,6` bound is the active count: unchanged)
    (0x5903D4, WIRE, WIRE_N, "590370 setters", "wire row stride"),
    (0x5903DB, BASE, BASE_N, "590370 setters", "wire base"),
    # updater 0x590550 (row update 0x58E790 keeps its active-count loop)
    (0x590569, WIRE, WIRE_N, "590550 updater", "wire row stride"),
    (0x59056F, CELL, CELL_N, "590550 updater", "cell row stride"),
    (0x59057A, BASE, BASE_N, "590550 updater", "wire base"),
    # error trigger 0x7E3380, both branches (the `cmp,6` bounds are the active count: unchanged)
    (0x7E349D, WIRE, WIRE_N, "7E3380 error trigger", "wire row stride"),
    (0x7E34A3, BASE, BASE_N, "7E3380 error trigger", "wire base"),
    (0x7E368E, WIRE, WIRE_N, "7E3380 error trigger", "wire row stride (second branch)"),
    (0x7E3694, BASE, BASE_N, "7E3380 error trigger", "wire base (second branch)"),
]
# Inline cell readers (1CC -> 264). 0x7A69C1 (V2's span) is rewritten by the V2 stub instead.
READERS = {
    "40CAA0 NoteFx": (0x40CD96, 0x40CDD5, 0x40CE13),
    "4431F0 marker helper": (0x443224, 0x443252),
    "445740 marker renderer": (0x445CC2,),
    "58CDC0 finger": (0x58CE50,),
    "5B1E50 pick create": (0x5B1EA6,),
    "5B1FE0 pick transform": (0x5B2049, 0x5B2083),
    "5B2680 tuner helper": (0x5B2786, 0x5B283D),
    "79E470 note head": (0x79E720,),
    "7A52C0 sustain": (0x7A5520, 0x7A5591),
    "7A6720 slide": (0x7A697A,),
    "7A6FE0 feedback": (0x7A7013, 0x7A7048, 0x7A707B),
    "7A8B10 lane offset": (0x7A8C21,),
}
for _fn, _vas in READERS.items():
    for _va in _vas:
        EDITS.append((_va, CELL, CELL_N, _fn, "cell row stride"))
APPLY = (0x58FD2F, 0x58FDDA)  # ApplyOffset: [eax + disp32] after `add eax, stride`
EXCLUDED = {0x7A69C1: "inside the V2 site (0x7A69BE, 15 bytes): the V2 stub carries the stride"}


def decode(text: bytes, base: int, va: int):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    return next(md.disasm(text[va - base : va - base + 16], va, count=1))


def field(i, old: int):
    """(offset in insn, size) of the one imm/disp operand equal to old (as stored)."""
    hits = []
    if i.imm_size and i.imm_offset:
        for op in i.operands:
            if op.type == capstone.x86.X86_OP_IMM and (op.imm & ((1 << (8 * i.imm_size)) - 1)) == old:
                hits.append((i.imm_offset, i.imm_size))
    for op in i.operands:
        if op.type == capstone.x86.X86_OP_MEM and i.disp_size and (op.mem.disp & ((1 << (8 * i.disp_size)) - 1)) == old:
            hits.append((i.disp_offset, i.disp_size))
    if len(hits) != 1:
        raise SystemExit(f"{i.address:08X} {i.mnemonic} {i.op_str}: value {old:#x} found {len(hits)} times")
    return hits[0]


def all_edits(text: bytes, base: int):
    """[(field va, expect bytes, write bytes, function, role)]."""
    out = []
    for va, old, new, fn, role in EDITS:
        i = decode(text, base, va)
        off, size = field(i, old)
        fmt = {1: "<B", 4: "<I"}[size]
        out.append((va + off, struct.pack(fmt, old), struct.pack(fmt, new), fn, role))
    # ApplyOffset: every disp32 off EAX between the two addresses moves by SHIFT
    va = APPLY[0]
    while va <= APPLY[1]:
        i = decode(text, base, va)
        for op in i.operands:
            if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_EAX and i.disp_size == 4:
                old = op.mem.disp & 0xFFFFFFFF
                new = (op.mem.disp + SHIFT) & 0xFFFFFFFF
                out.append(
                    (
                        va + i.disp_offset,
                        struct.pack("<I", old),
                        struct.pack("<I", new),
                        "58FCF0 ApplyOffset",
                        "displacement relative to the post-incremented row pointer",
                    )
                )
        va += i.size
    out.sort(key=lambda e: e[0])
    return out


def unique_pattern(text: bytes, base: int, va: int, n: int):
    """Shortest context window around [va, va+n) that occurs once in .text (exact bytes)."""
    for pre in (4, 6, 8, 10, 12, 16, 24):
        for post in (4, 6, 8, 10, 12, 16, 24):
            a = va - base - pre
            b = va - base + n + post
            win = text[a:b]
            if text.count(win) == 1:
                return a + base, win
    raise SystemExit(f"{va:08X}: no unique pattern")


def site_id(va: int) -> str:
    return f"FGR{va:06X}"


def build_sites():
    text, base = gen_fncopy.load_text()
    sites = []
    for va, exp, wr, fn, role in all_edits(text, base):
        start, win = unique_pattern(text, base, va, len(exp))
        sites.append(
            {
                "id": site_id(va),
                "kind": "bytes",
                "pattern": win.hex(" ").upper(),
                "offset": f"0x{va - start:X}",
                "expect": exp.hex(" ").upper(),
                "write": wr.hex().upper(),
                "note": f"0x{va:06X} in {fn}: native FretGrid (issue #18 stage 5), {role}",
                "fretgrid": "native",
            }
        )
    return sites


def build_stub_sites():
    """FGRB: the build's "complete" exit (mov eax,2) -> Rs7_Stub_FGRB fills the local points of cells 6/7."""
    text, base = gen_fncopy.load_text()
    va = 0x58FA25
    exp = bytes.fromhex("B802000000")
    assert text[va - base : va - base + 5] == exp
    start, win = unique_pattern(text, base, va, 5)
    hk = {
        "id": "FGHK",
        "kind": "jmp",
        "length": 7,
        "pattern": "55 8B EC 53 8B 5D 08 56 8B 73 0C 85 F6 74 05",
        "offset": "0x0",
        "expect": "55 8B EC 53 8B 5D 08",
        "note": "0x592AC0: Rnd_Guitar per-frame update entry (stdcall(guitar), [esp+4]); generic mid-hook (Rs7_Mid_FGHK): the first call "
        "writes the deferred FGR sites on the game's own thread and swaps every stock-size FretGrid of the guitars passing here "
        "for a native one",
        "fretgrid": "native",
    }
    ct = {
        "id": "FGCT",
        "kind": "jmp",
        "length": 5,
        "pattern": "33 C0 89 46 10 33 C0 50 6A 38",
        "offset": "0x2",
        "expect": "89 46 10 33 C0",
        "note": "0x59267B in the Rnd_Guitar ctor 0x5925F0: `mov [esi+10h],eax` after the FretGrid ctor (eax = grid, esi = guitar); "
        "generic mid-hook (Rs7_Mid_FGCT): registers grids built after the native layout was written",
        "fretgrid": "native",
    }
    return [
        {
            "id": "FGRB",
            "kind": "jmp",
            "length": 5,
            "pattern": win.hex(" ").upper(),
            "offset": f"0x{va - start:X}",
            "expect": exp.hex(" ").upper(),
            "note": "0x58FA25 in Rnd_FretGrid_Build 0x58F8A0, the complete exit `mov eax,2` (EBP frame: [ebp+8] grid, "
            "[ebp+0Ch] guitar): native FretGrid (issue #18 stage 5), local points of cells 6/7 from cells 0/1",
            "fretgrid": "native",
        },
        hk,
        ct,
    ]


def fmt_site(s: dict) -> str:
    return "    " + json.dumps(s, separators=(", ", ": "))


def main(argv):
    sites = build_sites()
    extra = build_stub_sites()
    if "--list" in argv:
        for s in sites:
            print(f"{s['id']} {s['expect']:12} -> {s['write']:10} {s['note']}")
        print(
            len(sites), "bytes sites +", len(extra), "stub site; excluded:", {f"{k:X}": v for k, v in EXCLUDED.items()}
        )
        return 0
    if "--write" in argv:
        raw = SITES.read_text(encoding="utf-8")
        have = set(re.findall(r'"id": "(FGR[0-9A-F]{6})"', raw))
        have |= set(re.findall(r'"id": "(FGRB|FGHK|FGCT)"', raw))
        new = [s for s in sites + extra if s["id"] not in have]
        if new:
            end = raw.rstrip().rfind("]")  # the sites array ends the file: `  ]\n}`
            body = raw[:end].rstrip()
            raw = body + ",\n" + ",\n".join(fmt_site(s) for s in new) + "\n  " + raw[end:]
            SITES.write_text(raw, encoding="utf-8", newline="\n")
        INC.write_text(
            "// Generated by tools/gen_fretgrid.py - do not edit. Native FretGrid in-place layout patches\n"
            "// ([neck] native_fretgrid=1, features/fretgrid/fretgrid.h): bytes sites, no stub.\n"
            + "".join(f"    RS7_FGR({s['id']}),\n" for s in sites),
            encoding="utf-8",
            newline="\n",
        )
        print(f"{len(new)} sites appended ({len(sites)} total), {INC}")
        return 0
    print(__doc__)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
