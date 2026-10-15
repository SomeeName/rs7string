#!/usr/bin/env python3
"""Native N-slot GuitarStrings (issue #18 stage 4): the relocated function copies.

The stock GuitarStrings loops (docs/ARCHITECTURE.md) iterate the instrument's string
count InstrumentInfo +0Bh over GuitarStrings main[6] (+4) / ghost[6] (+1Ch). A native GuitarStrings
(core/gsnative.h) has capacity-8 arrays at +40h / +60h and its InstrumentInfo has the counts the loops
run at +40h (active) / +41h (loaded). Each function below is copied byte for byte from the unpacked
exe (the code the game runs), with only its GuitarStrings array displacements and count bytes moved
(same instruction lengths) plus a few listed instruction replacements, and its rel32 branches out of the
function relocated. The DLL builds the copies at startup from live memory (after checking the bytes
equal these, FNV-1a), and the function's entry jumps to the copy for a native GuitarStrings only.

    uv run python rs7string/tools/gen_fncopy.py            # -> src/core/fncopy_specs.generated.h
    uv run python rs7string/tools/gen_fncopy.py --list     # print every edit (review)
"""

from __future__ import annotations

import sys
from dataclasses import dataclass, field
from pathlib import Path

import capstone
import pefile

ROOT = Path(__file__).resolve().parents[2]
REPO = Path(__file__).resolve().parents[1]
EXE = ROOT / "original" / "Rocksmith2014.unpacked.exe"
OUT = REPO / "src" / "core" / "fncopy_specs.generated.h"  # tracked: the build needs no game exe

GS_MAIN, GS_GHOST = 0x40, 0x60  # GuitarStrings native arrays (stock +4 / +1Ch)
INFO_ACTIVE, INFO_LOADED = 0x40, 0x41  # InstrumentInfo native count bytes (stock +0Bh)


@dataclass
class Fn:
    id: str
    start: int
    end: int  # exclusive
    count: str | None = None  # "active" / "loaded": every byte [reg+0Bh] -> the native count
    arrays: bool = True  # every dword [reg+idx*4+4h / +1Ch] -> +40h / +60h
    edits: dict = field(default_factory=dict)  # va -> ("bytes", expect, write) | ("call", sym, length)
    #       | ("imm32", sym, offset in insn)
    calls: dict = field(default_factory=dict)  # callee va -> DLL symbol (rel32 calls retargeted)
    skip: tuple = ()  # instruction vas the automatic rules must leave alone


FNS = [
    # Rnd_GuitarStrings_Free (EAX = GS): the paired release over 8 slots of the native arrays.
    Fn(
        "GSFR",
        0x599D70,
        0x599E1A,
        arrays=False,
        edits={
            0x599D76: ("bytes", "8D 78 1C", "8D 78 60"),  # lea edi,[eax+60h] (ghost[0])
            0x599D79: ("bytes", "C7 45 FC 06 00 00 00", "C7 45 FC 08 00 00 00"),
            0x599D80: ("bytes", "8B 77 E8", "8B 77 E0"),  # main = [edi-20h]
            0x599DC0: ("bytes", "C7 47 E8 00 00 00 00", "C7 47 E0 00 00 00 00"),
        },
    ),
    Fn("GSBD", 0x599E20, 0x599EA0, count="loaded"),  # owner bind
    Fn("GSUL", 0x599EA0, 0x599F81, count="loaded"),  # unload (models back to the pool)
    Fn(
        "GSLD",
        0x599F90,
        0x59A711,
        count="loaded",  # Rnd_GuitarStrings_Load
        calls={0x594530: "Rs7_Call_Attach", 0x5996A0: "Rs7_Call_Shape"},
    ),
    Fn("GSFO", 0x59A720, 0x59A81F, count="active"),  # FadeOut
    Fn("GSFI", 0x59A820, 0x59A91F, count="active"),  # FadeIn
    Fn("GSXF", 0x59A920, 0x59AA53, count="loaded"),  # UpdateTransforms (mode guard < 6 kept)
    Fn("GSWD", 0x59AA60, 0x59AAD7, count="loaded"),  # entity pass 0x7BF8C0
    Fn("GSUP", 0x59AAE0, 0x59AC1D, count="active"),  # Update (per frame)
    Fn("GSRS", 0x59AC20, 0x59AD2D, count="loaded"),  # ResetState
    Fn("GSPK", 0x59AD30, 0x59AD72, count="active"),  # Pluck
    Fn("GSVB", 0x59AD80, 0x59ADBB, count="active"),  # vibration wrapper
    Fn("GSIC", 0x59ADC0, 0x59AE53, count="loaded"),  # InitColors
    # Rnd_GuitarStrings_UpdateSectionDimming: dim/lit + next-note glow choice over the active count.
    # TP2 / DM1 (stage-2 N-slot chord templates) are part of the copy: TP2 as a call, DM1 as bytes.
    Fn(
        "GSDM",
        0x460550,
        0x4609E3,
        count="active",
        edits={
            0x4608AC: ("call", "Rs7_Call_TP2", 6),
            0x4608E3: ("bytes", "80 7C 38 04 FF", "80 7C 38 48 FF"),
        },
    ),
    # Rnd_StringRenderer_UpdateGlow: the glow ramp over 8 slots of a glow[8] in the native
    # GuitarStrings (+80h), the mesh loop's glow colour (state 3) and glow for mesh index >= 6.
    Fn(
        "GSGL",
        0x5441A0,
        0x54460C,
        arrays=False,
        edits={
            0x54423C: ("bytes", "BF 54 FF FF FF", "90 90 90 90 90"),  # (EDI set by GlowSetup)
            0x544243: ("call", "Rs7_Call_GlowSetup", 6),  # ESI = &glow[0], EDI = -ESI
            0x544249: ("bytes", "2B FB", "90 90"),
            0x54424B: ("bytes", "C7 45 0C 06 00 00 00", "C7 45 0C 08 00 00 00"),
            0x544257: ("bytes", "8B 4C 30 04", "8B 4C 30 40"),  # main[k] at +40h
            0x544366: ("bytes", "83 FA 06", "83 FA 08"),
            0x544384: ("call", "Rs7_Call_GlowColour", 7),
            0x5443B3: ("call", "Rs7_Call_GlowRead", 7),
        },
        calls={},
    ),
    # Rnd_Headstock_UpdateSegmentColours (stack headstock, guitar): segment colour = string colour,
    # over the active count; segment 6 = the edited model's string_size_7 (core/assetload.h).
    Fn(
        "GSHC",
        0x58E4F0,
        0x58E5B7,
        count="active",
        edits={
            0x58E516: ("call", "Rs7_Call_HsLoaded", 5),
            0x58E52D: ("call", "Rs7_Call_HsSeg", 7),
        },
    ),
]


def load_text():
    pe = pefile.PE(str(EXE), fast_load=True)
    sec = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
    base = pe.OPTIONAL_HEADER.ImageBase + sec.VirtualAddress
    return sec.get_data()[: sec.Misc_VirtualSize], base


def fnv1a(data: bytes) -> int:
    h = 0x811C9DC5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


@dataclass
class Edit:
    off: int
    kind: str  # bytes / call / imm32
    expect: bytes
    write: bytes = b""
    sym: str = ""
    text: str = ""


def build(fn: Fn, text: bytes, base: int):
    code = text[fn.start - base : fn.end - base]
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    insns = []
    pos = 0
    while pos < len(code):
        got = list(md.disasm(code[pos:], fn.start + pos, count=1))
        if not got:
            raise SystemExit(f"{fn.id}: undecodable byte at {fn.start + pos:08X}")
        insns.append(got[0])
        pos += got[0].size
    starts = {i.address for i in insns}
    edits: list[Edit] = []
    rel32: list[tuple[int, int]] = []

    def add(e: Edit):
        for o in edits:
            if o.off < e.off + len(e.expect) and e.off < o.off + len(o.expect):
                raise SystemExit(f"{fn.id}: edits overlap at {fn.start + e.off:08X}")
        edits.append(e)

    for va, spec in fn.edits.items():
        if va not in starts:
            raise SystemExit(f"{fn.id}: edit {va:08X} is not an instruction start")
        off = va - fn.start
        if spec[0] == "bytes":
            exp, wr = bytes.fromhex(spec[1]), bytes.fromhex(spec[2])
            if code[off : off + len(exp)] != exp or len(wr) != len(exp):
                raise SystemExit(
                    f"{fn.id}: {va:08X} expected {exp.hex(' ')} found {code[off : off + len(exp)].hex(' ')}"
                )
            if va + len(exp) not in starts and fn.start + off + len(exp) != fn.end:
                raise SystemExit(f"{fn.id}: edit {va:08X} ends inside an instruction")
            add(Edit(off, "bytes", exp, wr))
        elif spec[0] == "call":
            n = spec[2]
            if va + n not in starts:
                raise SystemExit(f"{fn.id}: call edit {va:08X}+{n} ends inside an instruction")
            add(Edit(off, "call", code[off : off + n], sym=spec[1]))
        else:
            raise SystemExit(f"{fn.id}: unknown edit kind {spec[0]}")

    for i in insns:
        off = i.address - fn.start
        if i.address in fn.skip or any(e.off <= off < e.off + len(e.expect) for e in edits):
            continue
        # relative branches
        if i.group(capstone.CS_GRP_JUMP) or i.group(capstone.CS_GRP_CALL):
            op = i.operands[0] if i.operands else None
            if op is not None and op.type == capstone.x86.X86_OP_IMM:
                tgt = op.imm
                inside = fn.start <= tgt < fn.end
                if i.size >= 5 and not inside:
                    if i.bytes[0] == 0xE8 and tgt in fn.calls:
                        add(
                            Edit(
                                off,
                                "call",
                                bytes(i.bytes),
                                sym=fn.calls[tgt],
                                text=f"{i.mnemonic} {i.op_str} -> {fn.calls[tgt]}",
                            )
                        )
                    else:
                        rel32.append((off + i.size - 4, tgt))
                elif not inside:
                    raise SystemExit(f"{fn.id}: short branch out of the function at {i.address:08X}")
                elif inside and tgt not in starts:
                    raise SystemExit(f"{fn.id}: branch into an instruction at {i.address:08X}")
                continue
        for op in i.operands:
            if op.type != capstone.x86.X86_OP_MEM:
                continue
            m = op.mem
            if fn.start <= (m.disp & 0xFFFFFFFF) < fn.end and m.base == 0:
                raise SystemExit(f"{fn.id}: absolute reference into the function at {i.address:08X}")
            dispoff = i.disp_offset
            if fn.count and op.size == 1 and m.disp == 0xB and m.index == 0 and m.base != 0:
                new = INFO_ACTIVE if fn.count == "active" else INFO_LOADED
                b = bytearray(i.bytes)
                if i.disp_size != 1 or b[dispoff] != 0x0B:
                    raise SystemExit(f"{fn.id}: count operand at {i.address:08X} is not disp8")
                b[dispoff] = new
                add(Edit(off, "bytes", bytes(i.bytes), bytes(b), text=f"{i.mnemonic} {i.op_str}  [count {fn.count}]"))
            elif fn.arrays and op.size == 4 and m.scale == 4 and m.index != 0 and m.base != 0 and m.disp in (4, 0x1C):
                b = bytearray(i.bytes)
                if i.disp_size != 1:
                    raise SystemExit(f"{fn.id}: array operand at {i.address:08X} is not disp8")
                b[dispoff] = GS_MAIN if m.disp == 4 else GS_GHOST
                add(
                    Edit(
                        off,
                        "bytes",
                        bytes(i.bytes),
                        bytes(b),
                        text=f"{i.mnemonic} {i.op_str}  [{'main' if m.disp == 4 else 'ghost'}]",
                    )
                )
        # data symbol edits could go here (imm32); none needed so far
    # the first whole instructions of at least 5 bytes: the entry jmp's length and the trampoline
    pro = 0
    for i in insns:
        if pro >= 5:
            break
        if i.group(capstone.CS_GRP_JUMP) or i.group(capstone.CS_GRP_CALL) or i.group(capstone.CS_GRP_RET):
            raise SystemExit(f"{fn.id}: branch in the entry bytes at {i.address:08X}")
        if any(op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_EIP for op in i.operands):
            raise SystemExit(f"{fn.id}: eip-relative operand in the entry bytes")
        pro += i.size
    edits.sort(key=lambda e: e.off)
    return code, edits, rel32, pro, insns


def render(results) -> str:
    syms = sorted({e.sym for _, _, edits, _, _, _ in results for e in edits if e.sym})
    out = [
        "// Generated by tools/gen_fncopy.py from the unpacked exe - do not edit.",
        "#pragma once",
        '#include "core/fncopy.h"',
        "",
        'extern "C" {',
    ]
    out += [f"void {s}();" for s in syms]
    out += ["}", ""]
    for fn, code, edits, rel32, pro, _ in results:
        out.append(f"static const Rs7FnEdit kRs7FnEdits_{fn.id}[] = {{")
        for e in edits:
            exp = e.expect.hex(" ").upper()
            wr = e.write.hex(" ").upper()
            sym = f"reinterpret_cast<const void*>(&{e.sym})" if e.sym else "nullptr"
            out.append(f'    {{0x{e.off:04X}, {len(e.expect)}, "{exp}", "{wr}", {sym}}},')
        out.append("};")
        out.append(f"static const Rs7FnRel kRs7FnRels_{fn.id}[] = {{")
        for off, tgt in rel32:
            out.append(f"    {{0x{off:04X}, 0x{tgt:08X}u}},")
        if not rel32:
            out.append("    {0, 0},")
        out.append("};")
    out.append("static const Rs7FnSpec kRs7FnSpecs[] = {")
    for fn, code, edits, rel32, pro, _ in results:
        out.append(
            f'    {{"{fn.id}", 0x{fn.start:08X}u, 0x{len(code):04X}u, 0x{fnv1a(code):08X}u, {pro}, '
            f"kRs7FnEdits_{fn.id}, {len(edits)}, kRs7FnRels_{fn.id}, {len(rel32)}}},"
        )
    out.append("};")
    out.append(f"static const int kRs7FnSpecCount = {len(results)};")
    out.append("")
    return "\n".join(out)


def main(argv: list[str]) -> int:
    text, base = load_text()
    results = []
    for fn in FNS:
        code, edits, rel32, pro, insns = build(fn, text, base)
        results.append((fn, code, edits, rel32, pro, insns))
    if "--list" in argv:
        for fn, code, edits, rel32, pro, _ in results:
            print(f"== {fn.id} {fn.start:08X}..{fn.end:08X} ({len(code)} bytes, entry {pro}, {len(rel32)} rel32)")
            for e in edits:
                what = e.text or (f"-> call {e.sym}" if e.kind == "call" else "")
                print(
                    f"  {fn.start + e.off:08X} {e.expect.hex(' '):24} -> {e.write.hex(' ') if e.write else e.sym:24} {what}"
                )
        return 0
    out = Path(argv[0]) if argv else OUT
    out.write_text(render(results), encoding="utf-8", newline="\n")
    print(f"{out}: {len(results)} functions")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
