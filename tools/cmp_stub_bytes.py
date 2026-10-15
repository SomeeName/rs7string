#!/usr/bin/env python3
"""Prove that a refactor of the hook stubs changed no code: compare two builds of dinput8.dll.

    uv run python tools/cmp_stub_bytes.py OLD.dll NEW.dll [--per-function]

1. Every section is compared byte for byte, with the debug directory (build timestamp, PDB id) masked.
   Equal = "BYTE-IDENTICAL": the refactor produced the same image (comments, macros, renamed locals,
   re-indented files).
2. If the images differ (stubs moved between files change the link order and with it every address), each
   exported function that lies in .text is decoded and compared instruction by instruction with
   address-like operands masked: branch targets inside the function become offsets from its start, anything
   that points into this DLL's image (its data and code addresses) becomes IMG, game addresses
   (outside the image) and every other operand stay literal. "SAME INSTRUCTIONS" means the code is the same
   and only its placement changed.

Exit status 0 for both outcomes above, 1 when a function differs or the export sets differ.
A function ends at the first `ret` / unconditional jump past its farthest forward branch target, at an
`int3`, or at the next export, whichever comes first.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

import capstone
import pefile

_HEX = re.compile(r"0x[0-9a-fA-F]+")
_END = {capstone.x86.X86_INS_RET, capstone.x86.X86_INS_JMP}


def _mask_hex(text: str, image: range) -> str:
    return _HEX.sub(lambda m: "IMG" if int(m.group(0), 16) in image else m.group(0), text)


def normalize(code: bytes, base: int, image: range, limit: int | None = None) -> list[str]:
    """Instruction list of one function at virtual address `base` (see the module docstring)."""
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    end_limit = base + (limit if limit is not None else len(code))
    out: list[str] = []
    farthest = base
    for ins in md.disasm(code, base):
        if ins.address >= end_limit or ins.mnemonic == "int3":
            break
        text = ins.op_str
        if ins.group(capstone.CS_GRP_JUMP) or ins.group(capstone.CS_GRP_CALL):
            if ins.operands and ins.operands[0].type == capstone.x86.X86_OP_IMM:
                target = ins.operands[0].imm
                if base <= target < end_limit:
                    text = f"L+{target - base:#x}"
                    if ins.id != capstone.x86.X86_INS_CALL:
                        farthest = max(farthest, target)
                elif target in image:
                    text = "IMG"
        else:
            text = _mask_hex(text, image)
        out.append(f"{ins.mnemonic} {text}".strip())
        if ins.id in _END and ins.address + ins.size > farthest:
            break
    return out


def _debug_ranges(pe: pefile.PE) -> list[tuple[int, int]]:
    d = pe.OPTIONAL_HEADER.DATA_DIRECTORY[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_DEBUG"]]
    ranges = [(d.VirtualAddress, d.VirtualAddress + d.Size)]
    for e in getattr(pe, "DIRECTORY_ENTRY_DEBUG", []):
        ranges.append((e.struct.AddressOfRawData, e.struct.AddressOfRawData + e.struct.SizeOfData))
    return ranges


def _section_bytes(pe: pefile.PE, sec) -> bytes:
    data = bytearray(sec.get_data())
    lo, hi = sec.VirtualAddress, sec.VirtualAddress + len(data)
    for a, b in _debug_ranges(pe):
        for r in range(max(a, lo), min(b, hi)):
            data[r - lo] = 0
    return bytes(data)


def images_identical(a: pefile.PE, b: pefile.PE) -> tuple[bool, list[str]]:
    """(identical, differing section names) with the debug directory masked."""
    diff = []
    names_a = [s.Name for s in a.sections]
    if names_a != [s.Name for s in b.sections]:
        return False, ["section table"]
    for sa, sb in zip(a.sections, b.sections):
        if _section_bytes(a, sa) != _section_bytes(b, sb):
            diff.append(sa.Name.rstrip(b"\0").decode())
    return not diff, diff


def exported_functions(pe: pefile.PE) -> dict[str, tuple[int, int]]:
    """name -> (rva, max length): exports inside .text, the length is the distance to the next export."""
    text = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
    lo, hi = text.VirtualAddress, text.VirtualAddress + text.Misc_VirtualSize
    items = sorted(
        (e.address, e.name.decode()) for e in pe.DIRECTORY_ENTRY_EXPORT.symbols if e.name and lo <= e.address < hi
    )
    out = {}
    for i, (rva, name) in enumerate(items):
        nxt = items[i + 1][0] if i + 1 < len(items) else hi
        out[name] = (rva, max(nxt - rva, 1))
    return out


def compare_functions(a: pefile.PE, b: pefile.PE) -> tuple[int, list[str]]:
    """(functions compared, one line per problem)."""
    fa, fb = exported_functions(a), exported_functions(b)
    problems = []
    if set(fa) != set(fb):
        problems.append(
            "export sets differ: only old %s, only new %s"
            % (sorted(set(fa) - set(fb))[:5], sorted(set(fb) - set(fa))[:5])
        )
    ra = range(a.OPTIONAL_HEADER.ImageBase, a.OPTIONAL_HEADER.ImageBase + a.OPTIONAL_HEADER.SizeOfImage)
    rb = range(b.OPTIONAL_HEADER.ImageBase, b.OPTIONAL_HEADER.ImageBase + b.OPTIONAL_HEADER.SizeOfImage)
    n = 0
    for name in sorted(set(fa) & set(fb)):
        (rva_a, len_a), (rva_b, len_b) = fa[name], fb[name]
        ia = normalize(a.get_data(rva_a, len_a), a.OPTIONAL_HEADER.ImageBase + rva_a, ra)
        ib = normalize(b.get_data(rva_b, len_b), b.OPTIONAL_HEADER.ImageBase + rva_b, rb)
        n += 1
        if ia != ib:
            k = next((i for i, (x, y) in enumerate(zip(ia, ib)) if x != y), min(len(ia), len(ib)))
            problems.append(
                f"{name}: differs at instruction {k}: old {ia[k] if k < len(ia) else '<end>'!r}"
                f" new {ib[k] if k < len(ib) else '<end>'!r}"
            )
    return n, problems


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("old", type=Path)
    ap.add_argument("new", type=Path)
    ap.add_argument("--per-function", action="store_true", help="compare per function even if the images are identical")
    args = ap.parse_args(argv)
    a, b = pefile.PE(str(args.old)), pefile.PE(str(args.new))
    same, diff = images_identical(a, b)
    if same:
        print("sections identical (debug directory masked): BYTE-IDENTICAL")
        if not args.per_function:
            return 0
    else:
        print("images differ in: " + ", ".join(diff))
    n, problems = compare_functions(a, b)
    for p in problems:
        print("  " + p)
    if problems:
        print(f"{len(problems)} problem(s) in {n} functions")
        return 1
    print(f"{n} exported functions: SAME INSTRUCTIONS" + ("" if same else " (placement differs)"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
