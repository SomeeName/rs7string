"""Generate stage-5 ColourManager exact-address instruction ports (no game access).

Reads only the section-4 evidence ledger/listings and the generated unpacked PE.
    uv run python tools/gen_colournative.py
The six authored keys, shared-state arities, texture denominators and auxiliary
59AE60 guards are deliberately unchanged. All edits assert the evidence bytes.
"""

from dataclasses import dataclass
import json
from pathlib import Path
import re
import struct

import capstone
import gen_fncopy

REPO = Path(__file__).resolve().parents[1]
EVIDENCE = (
    REPO.parent / "research/7-string/nslot/evidence/fretgrid-tables"
)  # maintainer-local evidence ledger (not in this repository)


def move(d):
    base, new = (0x350, 0xDD0) if d < 0x890 else (0x890, 0x14D0)
    index, remainder = divmod(d - base, 4)
    assert remainder == 0 and 0 <= index < 336
    mode, rest = divmod(index, 168)
    state, slot = divmod(rest, 6)
    return new + 4 * ((mode * 28 + state) * 8 + slot)


@dataclass
class Port:
    va: int
    function: int
    old: bytes
    new: bytes
    reason: str


# CONFIRMED matrix instructions from section 4. Not a numeric-displacement scan:
# e.g. 40CD45/4611D0/7A0A68/7A3214 are OTHER objects and must not move.
READS = {
    0x40CAA0: [0x40CAFD],
    0x4420D0: [0x442252],
    # fret-number lane colour (FUN_004b0e70, ptr7/8; caller 4B14D0): was missing from READS, so the
    # stock 6-column displacement hit the wrong state/slot of the 8-column manager
    0x4B0E70: [0x4B0EA1, 0x4B0EF3],
    # Guitarcade ResolveExtraDataColor (0048EBD0, UV scalar; not in the section-4 ledger, found by
    # docs/ARCHITECTURE.md): FLD [eax+edx*4+0x890], edx = 6*q+slot. The *6 is synthesized at 48EC14
    # (see the 'synthesized state index' list below); the slot guard 48EBF7 stays 6 (the binding keeps exposing the six authored columns).
    0x48EBD0: [0x48EC1A],
    0x4611C0: [0x461319, 0x46133F, 0x461365, 0x46138B],
    0x5441A0: [0x544384],
    0x58E960: [0x58E9E7],
    0x58EC10: [0x58EC41],
    0x598120: [0x598166, 0x59819E],
    0x59AE60: [0x59AE91, 0x59AEE3],  # displacement only; no index reinterpretation
    0x59BD70: [0x59BDA2, 0x59BDE0, 0x59BE1E, 0x59BE5C, 0x59BE9A, 0x59BED8, 0x59BF0E, 0x59BF44],
    0x5B20E0: [0x5B22BD],
    0x5B4DE0: [0x5B521F],
    0x758320: [0x758384],
    0x79BB10: [0x79BB3C, 0x79BB63],
    0x79BB70: [0x79BB96, 0x79BBBE],
    0x79BBD0: [0x79BBF3, 0x79BC25],
    0x7A0950: [0x7A11BC, 0x7A11EC],
    0x7A3200: [0x7A3580, 0x7A35A4],
    0x7A8490: [0x7A84DA, 0x7A851B, 0x7A8565],
    0x7A8590: [0x7A85C0, 0x7A85EF],
    0x7F2F10: [0x7F3005],
    0x85DB30: [0x85DCB1, 0x85DD01],
}
GUARDS = {
    0x40CADE: 0x40CAA0,
    0x442234: 0x4420D0,
    0x58EC22: 0x58EC10,
    0x598147: 0x598120,
    0x59817F: 0x598120,
    0x544366: 0x5441A0,
    0x7A119D: 0x7A0950,
    0x7A11CD: 0x7A0950,
    0x7A84B3: 0x7A8490,
    0x7A84FC: 0x7A8490,
    0x7A8546: 0x7A8490,
    0x7A85A1: 0x7A8590,
    0x7A85D0: 0x7A8590,
    0x79BC06: 0x79BBD0,
    0x7F2FE6: 0x7F2F10,
    0x85DC98: 0x85DB30,
    0x85DCE8: 0x85DB30,
    0x5B229C: 0x5B20E0,
    0x5B5200: 0x5B4DE0,
    0x758366: 0x758320,
}


def ports():
    text, base = gen_fncopy.load_text()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    result = {}

    def ins(va):
        return next(md.disasm(text[va - base : va - base + 16], va, 1))

    def put(va, fn, new, reason):
        i = ins(va)
        new = bytes.fromhex(new) if isinstance(new, str) else bytes(new)
        assert len(new) == i.size, f"{va:X} length"
        assert bytes(i.bytes) != new
        assert va not in result
        result[va] = Port(va, fn, bytes(i.bytes), new, reason)

    def field(va, fn, value, displacement=False, reason="matrix stride/address"):
        i = ins(va)
        b = bytearray(i.bytes)
        off, size = (i.disp_offset, i.disp_size) if displacement else (i.imm_offset, i.imm_size)
        assert size in (1, 4)
        b[off : off + size] = int(value).to_bytes(size, "little", signed=value < 0)
        put(va, fn, b, reason)

    ledger = {int(r["address"], 16): r for r in json.loads((EVIDENCE / "instruction-ledger.json").read_text())}
    for fn, reads in READS.items():
        for va in reads:
            i = ins(va)
            assert i.disp_size == 4
            if va in ledger:  # audit-found readers (48EC1A) have no ledger entry
                assert bytes.fromhex(ledger[va]["bytes"]) == i.bytes
            field(va, fn, move(i.disp), True)
        # Only the mode multiplies recorded in the colour ledger for this function.
        for va, r in ledger.items():
            if (
                int(r["function"], 16) == fn
                and r["instruction"].startswith("imul ")
                and any("colour" in c for c in r["categories"])
            ):
                i = ins(va)
                old = i.operands[-1].imm
                assert old in (0xA8, 0x2A0)
                field(va, fn, 0xE0 if old == 0xA8 else 0x380)

    for va, fn in GUARDS.items():
        i = ins(va)
        assert i.mnemonic == "cmp" and i.operands[-1].imm == 6
        field(va, fn, 8, reason="capacity guard (proven string index)")

    # Preserve the stock six-column ABI for external writers (RSMods). Native tables are appended.
    field(0x518B26, 0x518AF0, 0xDD0, True, "FillStateColors appended pointer table")
    field(0x519725, 0x5196F0, 0xDD0, True, "GetColor appended pointer table")

    # Constructor: two modes * 28 states * eight pointers/UVs, shared prefix untouched.
    field(0x5184F6, 0x517F70, 0x14D0, True)
    field(0x518504, 0x517F70, 8)
    field(0x51850B, 0x517F70, -0x700, True)
    field(0x518AC4, 0x518AC0, 0x1BD0, reason="creation allocation")
    field(0x518571, 0x518550, 0x1BD0, reason="scalar-delete sized free")

    # mode*28+state is unchanged. *6 -> *8 via same-length IMUL reg,reg,4
    # followed by the existing scale2/scale8. Key/source walk remains six-long.
    for va, fn, b in (
        (0x518B1E, 0x518AF0, "6B C8 04"),
        (0x518BAA, 0x518B50, "6B C2 04"),
        (0x518C2A, 0x518B50, "6B F6 04"),
        (0x518CA4, 0x518C70, "6B C2 04"),
        (0x51971F, 0x5196F0, "6B D0 04"),
        (0x48EC14, 0x48EBD0, "6B C9 04"),
    ):
        put(va, fn, b, "synthesized state index *6 -> *8")
    # Equal-range fifth store was synthesized separately: (q*4+14D0/8+2)*8.
    put(0x518BAF, 0x518B50, "8D 14 95 9C 02 00 00", "scalar fifth UV address")
    put(0x518CB0, 0x518C70, "8D 14 95 9C 02 00 00", "scalar fifth index address")
    for fn, vas in (
        (0x518B50, [0x518BC0, 0x518BC7, 0x518BCE, 0x518BD5, 0x518BDF, 0x518C2D]),
        (0x518C70, [0x518CA9, 0x518CB7, 0x518CBF, 0x518CC6, 0x518CD0]),
    ):
        for va in vas:
            field(va, fn, ins(va).disp + 0xC40, True)
    field(0x518CF0, 0x518C70, 0x380)
    field(0x518CF6, 0x518C70, move(0x9E0), True)
    field(0x5196FE, 0x5196F0, 8)

    # BuildTables fixed stores all have ESI=this; source-list addresses (<350) stay.
    for i in md.disasm(text[0x518D30 - base : 0x519672 - base], 0x518D30):
        if i.mnemonic == "mov" and i.operands[0].type == capstone.x86.X86_OP_MEM:
            m = i.operands[0].mem
            if m.base == capstone.x86.X86_REG_ESI and 0x350 <= m.disp < 0x890:
                field(i.address, 0x518D30, move(m.disp), True, "BuildTables fixed pointer store")

    return sorted(result.values(), key=lambda p: p.va)


def sites():
    text, base = gen_fncopy.load_text()
    out = []
    for p in ports():
        off = p.va - base
        # Whole-instruction anchor plus following bytes; choose a unique signature.
        n = max(len(p.old), 12)
        while text.count(text[off : off + n]) != 1:
            n += 1
            assert n < 100
        out.append(
            dict(
                id=f"CM{p.va:06X}",
                kind="bytes",
                pattern=text[off : off + n].hex(" ").upper(),
                offset="0x0",
                expect=p.old.hex(" ").upper(),
                write=p.new.hex(" ").upper(),
                note=f"CONFIRMED {p.va:08X} in {p.function:08X}: {p.reason}; section 4 evidence",
            )
        )
    # Extra scalar entries and palette publication; all callbacks are integer-only.
    for sid, va, length in [
        ("CMUV", 0x518BC0, 7),
        ("CMIX", 0x518CA4, 5),
        ("CMINIT", 0x51853D, 6),
        ("CMBUILD", 0x51966C, 6),
        ("CMFALL", 0x75835A, 5),
    ]:
        b = text[va - base : va - base + length]
        # CMUV/CMIX replace arithmetic also listed above, remove its bytes site below.
        back = 0
        while text.count(text[va - base - back : va - base + 24]) != 1:
            back += 1
            assert back < 64
        out.append(
            dict(
                id=sid,
                kind="jmp",
                length=length,
                pattern=text[va - base - back : va - base + 24].hex(" ").upper(),
                offset=hex(back),
                expect=b.hex(" ").upper(),
                note=f"CONFIRMED {va:08X}: native colour publication / bounded fallback",
            )
        )
    # CMUV includes relocated first FST; CMIX includes new *8 arithmetic.
    out = [s for s in out if s["id"] not in ("CM518BC0", "CM518CA4")]
    return out


def main():
    path = REPO / "sites.json"
    raw = path.read_bytes().decode("utf-8")
    lines = raw.split("\n")
    # Only add/replace the CM lines; every other line of the shared file stays byte-identical.
    lines = [l for l in lines if not l.lstrip().startswith('{"id": "CM')]
    end = max(k for k, l in enumerate(lines) if l.strip() == "]")
    last = max(k for k in range(end) if lines[k].lstrip().startswith('{"id"'))
    if not lines[last].rstrip().endswith(","):
        lines[last] = lines[last].rstrip() + ","
    new = [f"    {json.dumps(s)}," for s in sites()]
    new[-1] = new[-1].rstrip(",")
    lines[last + 1 : last + 1] = new
    path.write_bytes(("\n").join(lines).encode("utf-8"))
    print(f"{len(sites())} ColourManager sites ({len(ports())} exact instruction ports)")
    header = ["// Generated by tools/gen_colournative.py; included inside kHooks[]."]
    for s in sites():
        if s["kind"] == "bytes":
            header.append(f'    {{"{s["id"]}", nullptr, nullptr, nullptr, false, false}},')
        else:
            header.append(
                f'    {{"{s["id"]}", nullptr, nullptr, nullptr, false, false, false, 0, 0, &Rs7Colour_{s["id"]}}},'
            )
    (REPO / "src/core/colour_hooks.generated.h").write_text(chr(10).join(header) + chr(10))


if __name__ == "__main__":
    main()
