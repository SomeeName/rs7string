#!/usr/bin/env python3
"""Native capacity-8 next-note / target grid (issue #18 stage 5, area 2): the in-place patches.

. Stock Rnd_NextNoteGrid (the owner lives on the stack of
Rnd_NeckTargets_Update 0x7E4870, G = ebp-0D20h): pointer[26][26] at +0 (row 68h), time[6][26] at +0A90h,
u16 string / fret counts at +0D00h / +0D02h, the time-set tree at +0D04h..+0D1Bh, /GS cookie right behind
(ebp-4). Native layout ([neck] nextgrid=1): the prefix is kept and time[8][26] (340h bytes, row 68h) is
appended at +0D1Ch, so the grid is 105Ch bytes and the frame of 0x7E4870 grows by 340h (0D34h -> 1074h):

    stock  G = ebp-0D20h, locals ebp-0D24h..-0D34h        native  G = ebp-1060h, locals ebp-1064h..-1074h
    tree header G+0D04h = ebp-1Ch / -18h / -0Ch           tree header G+0D04h = ebp-35Ch / -358h / -34Ch

Edits (same-length byte patches, "bytes" sites NGB<va>, plus three jmp stubs; no function is copied: Set /
the tree release hold sites that must stay hooked, and a copy built from stock bytes would bypass them):
  Init  7AA0B0  clear 340h at +0D1Ch, fill all 8 rows (not the active count) with 2000.0f
  Set   7AA140  NG1 (stub)    time cell address = G + 0D1Ch + (s*26+f)*4, s < 8 and f < 26 else the reject exit
  Prune 7AA1B0  time cell address of the stock loop (the pointer index s*26+f is already in ECX/ESI/EAX)
  Update 7E4870 NGP (stub) probe + frame; every ebp-0D20h..-0D34h displacement moves by -340h; NG9 (stub) the
                tree release with the moved displacements, and the string-6 targets (Rs7_NeckRow6N)
  Register 7A5D50: no edit (it only calls Set with the grid it is given).

    uv run python tools/gen_nextgrid.py --list     # print every edit (review)
    uv run python tools/gen_nextgrid.py --write    # append the missing sites to sites.json, write
                                                   # src/features/nextgrid/sites.generated.inc
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
INC = REPO / "src" / "features" / "nextgrid" / "sites.generated.inc"

T_STOCK, T_NATIVE = 0xA90, 0xD1C  # time[][] base: stock rows 0-5, native rows 0-7 behind the tree
ROWS_N = 8
GRID_STOCK, GRID_NATIVE = 0xD1C, 0xD1C + ROWS_N * 0x68  # owner size: D1Ch -> 105Ch
SHIFT = 0x340  # frame growth: 8 * 68h
FRAME_STOCK, FRAME_NATIVE = 0xD34, 0xD34 + SHIFT
UPDATE = (0x7E4870, 0x7E49F9)  # Rnd_NeckTargets_Update
NGP_VA, NG9_VA, NG9_LEN = 0x7E4873, 0x7E49D6, 15
OLD_LOW, OLD_HIGH = 0xD34, 0xD20  # ebp-0D34h .. ebp-0D20h: the locals and the grid base

# the 14 bytes at 7AA21E: movzx ebx,bx / add ebx,1Ah / imul ebx,ebx,1Ah / add ebx,eax / fld [edi+ebx*4]
PRUNE_OLD = "0FB7DB83C31A6BDB1A03D8D9049F"
PRUNE_NEW = "8D1C30D9849F1C0D000090909090"

# (va, expect hex, write hex, function, role). Operands inside Update are generated (frame_edits).
EDITS = [
    (0x7AA0F5, "70020000", "40030000", "7AA0B0 Init", "clear the time block: 270h -> 340h bytes"),
    (0x7AA0FB, "900A0000", "1C0D0000", "7AA0B0 Init", "time block base +0A90h -> +0D1Ch"),
    (
        0x7AA111,
        "0FB7D7",
        "6A085A",
        "7AA0B0 Init",
        "movzx edx,di -> push 8 / pop edx: fill 8 time rows (not the active count)",
    ),
    (
        0x7AA21E,
        PRUNE_OLD,
        PRUNE_NEW,
        "7AA1B0 Prune",
        "time cell: lea ebx,[eax+esi] (= s*26+f), fld [edi+ebx*4+0D1Ch]; ebx is reloaded at 7AA23B",
    ),
]


def decode(text: bytes, base: int, va: int):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    return next(md.disasm(text[va - base : va - base + 16], va, count=1))


def frame_edits(text: bytes, base: int):
    """Every disp32 of an ebp operand in Update that lies in ebp-0D34h..-0D20h, minus the stub spans."""
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    out = []
    va = UPDATE[0]
    skip = ((NGP_VA, NGP_VA + 6), (NG9_VA, NG9_VA + NG9_LEN))
    for i in md.disasm(text[UPDATE[0] - base : UPDATE[1] - base + 16], UPDATE[0]):
        if i.address > UPDATE[1]:
            break
        if any(a <= i.address < b for a, b in skip):
            continue
        for op in i.operands:
            if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_EBP:
                d = op.mem.disp
                if -OLD_LOW <= d <= -OLD_HIGH:
                    assert i.disp_size == 4, f"{i.address:X}: disp8 in the grid range"
                    new = d - SHIFT
                    out.append(
                        (
                            i.address + i.disp_offset,
                            struct.pack("<i", d).hex().upper(),
                            struct.pack("<i", new).hex().upper(),
                            "7E4870 Update",
                            f"ebp{d:+#x} -> ebp{new:+#x}",
                        )
                    )
    return out


def all_edits(text: bytes, base: int):
    out = [(va, bytes.fromhex(e), bytes.fromhex(w), fn, role) for va, e, w, fn, role in EDITS]
    out += [(va, bytes.fromhex(e), bytes.fromhex(w), fn, role) for va, e, w, fn, role in frame_edits(text, base)]
    for va, exp, wr, *_ in out:
        assert text[va - base : va - base + len(exp)] == exp, f"{va:X}: stock bytes differ"
    out.sort(key=lambda e: e[0])
    return out


def unique_pattern(text: bytes, base: int, va: int, n: int):
    """Shortest context window around [va, va+n) that occurs once in .text (exact bytes)."""
    for pre in (4, 6, 8, 10, 12, 16, 24):
        for post in (4, 6, 8, 10, 12, 16, 24):
            a = va - base - pre
            b = va - base + n + post
            if text.count(text[a:b]) == 1:
                return a + base, text[a:b]
    raise SystemExit(f"{va:08X}: no unique pattern")


def site_id(va: int) -> str:
    return f"NGB{va:06X}"


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
                "note": f"0x{va:06X} in {fn}: native next-note grid (issue #18 stage 5), {role}",
                "nextgrid": "native",
            }
        )
    return sites


def stub_site(sid, va, length, note, target=None):
    text, base = gen_fncopy.load_text()
    exp = text[va - base : va - base + length]
    start, win = unique_pattern(text, base, va, length)
    s = {
        "id": sid,
        "kind": "jmp",
        "length": length,
        "pattern": win.hex(" ").upper(),
        "offset": f"0x{va - start:X}",
        "expect": exp.hex(" ").upper(),
    }
    if target:
        s["target"] = target
    s["note"] = note
    s["nextgrid"] = "native"
    return s


def build_stub_sites():
    return [
        stub_site(
            "NG1",
            0x7AA166,
            16,
            "0x7AA166 in Rnd_NextNoteGrid_Set 0x7AA140 (same bytes as R7b): native next-note grid (issue #18 stage 5), "
            "EAX = (s*26+f) + 347h so stock `lea edi,[esi+eax*4]` = G+0D1Ch+(s*26+f)*4; s >= 8 or f >= 26: the stock reject exit",
        ),
        stub_site(
            "NGP",
            NGP_VA,
            6,
            "0x7E4873 in Rnd_NeckTargets_Update 0x7E4870, `sub esp,0D34h`: native next-note grid (issue #18 stage 5), "
            "frame 1074h (> one page: probe it first)",
        ),
        stub_site(
            "NG9",
            NG9_VA,
            NG9_LEN,
            "0x7E49D6 in Rnd_NeckTargets_Update 0x7E4870 (replaces N9): native next-note grid (issue #18 stage 5), "
            "string-6 targets (Rs7_NeckRow6N, only while the active count is 6) and the tree release `cmp [size],0; pop edi; "
            "pop esi; je; mov edx,[root]; push edx; lea ecx,[tree]` with the moved displacements; the `call 0x7D06C0` follows",
            target={"rel_at": 7, "rel_size": 1, "insn_end": 8},
        ),
    ]


def fmt_site(s: dict) -> str:
    return "    " + json.dumps(s, separators=(", ", ": "))


def main(argv):
    sites = build_sites()
    extra = build_stub_sites()
    if "--list" in argv:
        for s in sites:
            print(f"{s['id']} {s['expect']:30} -> {s['write']:30} {s['note']}")
        print(len(sites), "bytes sites +", len(extra), "stub sites")
        return 0
    if "--write" in argv:
        raw = SITES.read_text(encoding="utf-8")
        have = set(re.findall(r'"id": "(NGB[0-9A-F]{6})"', raw)) | set(re.findall(r'"id": "(NG[1P9])"', raw))
        new = [s for s in sites + extra if s["id"] not in have]
        if new:
            end = raw.rstrip().rfind("]")  # the sites array ends the file: `  ]\n}`
            body = raw[:end].rstrip()
            raw = body + ",\n" + ",\n".join(fmt_site(s) for s in new) + "\n  " + raw[end:]
            SITES.write_text(raw, encoding="utf-8", newline="\n")
        INC.write_text(
            "// Generated by tools/gen_nextgrid.py - do not edit. Native next-note grid in-place patches\n"
            "// ([neck] native_nextgrid=1, features/nextgrid/nextgrid.h): bytes sites, no stub.\n"
            + "".join(f"    RS7_NGB({s['id']}),\n" for s in sites),
            encoding="utf-8",
            newline="\n",
        )
        print(f"{len(new)} sites appended ({len(sites)} + {len(extra)} total), {INC}")
        return 0
    print(__doc__)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
