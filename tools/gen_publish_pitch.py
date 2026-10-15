"""Relocate pitch readers, expanding stack arrays without changing their x87 code.

Run: uv run --with keystone-engine python tools/gen_publish_pitch.py
Only reads original/Rocksmith2014.unpacked.exe. Generated code is hashed against live code.
"""

from pathlib import Path
import re
import struct
import capstone
from keystone import Ks, KS_ARCH_X86, KS_MODE_32
import gen_fncopy as fc

REPO = Path(__file__).resolve().parents[1]
OUT = REPO / "src/core/publish_pitch.generated.h"
FNS = [("PUBNL", 0x4E6E90, 0x4E7B2F, 9), ("PUBAN", 0x875430, 0x8755B6, 6), ("PUBAC", 0x8755C0, 0x8756F7, 6)]
SYMS = ["Rs7_Pitch_Raw8", "Rs7_Call_PubChordTail", "Rs7_Publish_ReverseTail"]
SYN = 0x70000000


def assemble(fn):
    ident, start, end, entry = fn
    text, base = fc.load_text()
    stock = text[start - base : end - base]
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    insns = list(md.disasm(stock, start))
    assert sum(i.size for i in insns) == len(stock)
    rows = []
    for i in insns:
        a, op = i.address, i.op_str
        rows.append(f"L{a:x}:")
        if ident == "PUBNL":
            if a == 0x4E6E93:
                op = "esp, 0xb00"
            # Arrays alone move. The six-element -5Ch harmonics fallback is NOT a string array.
            if "[ebp" in op and i.disp_size:
                d = i.disp
                new = None
                if -0x44 <= d <= -0x30:
                    new = d - 0xA5C
                if -0x2C <= d <= -0x18:
                    new = d - 0xA94
                if -0xA70 <= d <= -0xA5C:
                    new = d - 0x70
                if new is not None:
                    op = replace_disp(op, d, new)
            if a == 0x4E6F20:
                op = op.replace("0x134c", "0x13c8")
            if a == 0x4E6F39:
                op = "eax, dword ptr [edi + 0x13d8]"
            if a == 0x4E6FE2:
                rows += [
                    "mov dword ptr [ebp-0xac8], 0",
                    "mov dword ptr [ebp-0xac4], 0",
                    "mov dword ptr [ebp-0xaa8], -1",
                    "mov dword ptr [ebp-0xaa4], -1",
                ]
            if a == 0x4E75D5:
                rows += [
                    "cmp dword ptr [ebp-0xaa8], 0",
                    "jle score7done",
                    "add eax, edi",
                    "score7done:",
                    "cmp dword ptr [ebp-0xaa4], 0",
                    "jle score8done",
                    "add eax, edi",
                    "score8done:",
                ]
            if a == 0x4E764D:
                rows += [
                    "cmp dword ptr [ebp-0xaa8], edi",
                    "jle result7done",
                    "inc esi",
                    "result7done:",
                    "cmp dword ptr [ebp-0xaa4], edi",
                    "jle result8done",
                    "inc esi",
                    "result8done:",
                ]
        elif ident == "PUBAN":
            if a in (0x8754A1, 0x875504):
                op = op.replace("0x1340", "0x13b8")
            if a == 0x8754AA:
                op = hex(SYN)
            if a == 0x875513:
                op = hex(SYN + 2)
            if a == 0x875545:
                op = op.replace("0x134c", "0x13c8")
            if a == 0x87556B:
                op = "edx, dword ptr [esi + 0x13d8]"
        else:
            if a == 0x8755C3:
                op = "esp, 0xb4"
            if "[ebp" in op and i.disp_size:
                d = i.disp
                if d == -0x34:
                    op = replace_disp(op, d, -0x94)
                if d == -0x1C:
                    op = replace_disp(op, d, -0x74)
            if a == 0x875630:
                rows += ["call " + hex(SYN + 1)]  # fills input slots6/7, preserves all registers
            if a == 0x875654:
                op = "eax, 0x20"
            if a in (0x8756BE, 0x8756C8):
                op = op.replace("0x134c", "0x13c8")
            if a == 0x8756D5:
                op = "eax, dword ptr [esi + 0x13d8]"
        if i.group(capstone.CS_GRP_JUMP) or i.group(capstone.CS_GRP_CALL):
            if i.operands and i.operands[0].type == capstone.x86.X86_OP_IMM:
                target = int(op, 16) if op.startswith("0x") else i.operands[0].imm
                if start <= target < end:
                    op = f"L{target:x}"
        rows.append(i.mnemonic + " " + op)
    code, _ = Ks(KS_ARCH_X86, KS_MODE_32).asm(";\n".join(rows), addr=0)
    code = bytes(code)
    rels = []
    for i in md.disasm(code, 0):
        if (i.group(capstone.CS_GRP_JUMP) or i.group(capstone.CS_GRP_CALL)) and i.operands[
            0
        ].type == capstone.x86.X86_OP_IMM:
            target = i.operands[0].imm & 0xFFFFFFFF
            if target >= len(code):
                assert i.imm_size == 4
                rels.append((i.address + i.imm_offset, target))
    return stock, code, rels


def replace_disp(op, old, new):
    # Capstone prints signed small stack offsets as '- 0x..'.
    needle = f"- 0x{-old:x}" if old < 0 else f"+ 0x{old:x}"
    assert needle in op, (op, old)
    return op.replace(needle, f"- 0x{-new:x}")


def render():
    out = ["// Generated by tools/gen_publish_pitch.py; do not edit.", "#pragma once"]
    for ident, start, end, entry in FNS:
        stock, code, rels = assemble((ident, start, end, entry))
        out += [
            f"static const unsigned char kPitchCode_{ident}[] = {{",
            ",".join(f"0x{x:02x}" for x in code),
            "};",
            f"static const PitchRel kPitchRel_{ident}[] = {{",
        ]
        for off, target in rels:
            sym = (
                f"reinterpret_cast<uintptr_t>(&{SYMS[target - SYN]})"
                if SYN <= target < SYN + len(SYMS)
                else f"0x{target:x}"
            )
            out.append(f"{{{off}, {sym}}},")
        out += ["};"]
    out += ["static const PitchSpec kPitchSpecs[] = {"]
    for f in FNS:
        ident, start, end, entry = f
        stock, code, rels = assemble(f)
        out.append(
            f'{{"{ident}",0x{start:x},{len(stock)},0x{fc.fnv1a(stock):x},{entry},kPitchCode_{ident},sizeof kPitchCode_{ident},kPitchRel_{ident},sizeof kPitchRel_{ident}/sizeof(PitchRel)}},'
        )
    out += ["};"]
    return "\n".join(out) + "\n"


if __name__ == "__main__":
    OUT.write_text(render(), encoding="utf-8", newline="\n")
    print(OUT)
