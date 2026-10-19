#!/usr/bin/env python3
"""Run the built mid-hook relocator (Rs7Mid_Relocate of bin/Release/dinput8.dll, in Unicorn) over the displaced
bytes of every jmp site in sites.json and list the sites it rejects and why.

Reason: the first displaced instruction that the relocator rejects on its own (NOP-padded to 5 bytes), as
mnemonic + opcode form; "span" when every instruction passes alone but the whole span does not (internal branch
or call, code after a RET). Game image access as tools/sitecheck.py. No game bytes are written anywhere.
    uv run python tools/midhook_survey.py [--list] [sites.json] [exe]
"""

from __future__ import annotations

import collections
import struct
import sys
from pathlib import Path

import capstone
import pefile
from unicorn import UC_ARCH_X86, UC_MODE_32, UC_PROT_ALL, Uc
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESP

import sitecheck
from sitetable import load

REPO = Path(__file__).resolve().parents[1]
DLL = REPO / "bin" / "Release" / "dinput8.dll"
SCRATCH, STACK, RET_MAGIC = 0x10000000, 0x10100000, 0x10200000


class Relocator:
    """Rs7Mid_Relocate of a built DLL, executed in Unicorn (the function is pure: no imports, no state)."""

    def __init__(self, dll: Path = DLL):
        pe = pefile.PE(str(dll))
        base = pe.OPTIONAL_HEADER.ImageBase
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.uc.mem_map(base, (pe.OPTIONAL_HEADER.SizeOfImage + 0xFFF) & ~0xFFF, UC_PROT_ALL)
        self.uc.mem_write(base, pe.header)
        for s in pe.sections:
            self.uc.mem_write(base + s.VirtualAddress, s.get_data())
        exports = {e.name.decode(): base + e.address for e in pe.DIRECTORY_ENTRY_EXPORT.symbols if e.name}
        self.entry = exports["Rs7Mid_Relocate"]
        for region in (SCRATCH, STACK, RET_MAGIC):
            self.uc.mem_map(region, 0x10000, UC_PROT_ALL)

    def __call__(self, raw: bytes, source_va: int, dest_va: int = 0x20000000) -> bytes | None:
        """The relocated copy, or None when rejected."""
        src, out = SCRATCH, SCRATCH + 0x1000
        self.uc.mem_write(src, raw)
        esp = STACK + 0x8000
        self.uc.mem_write(esp, struct.pack("<7I", RET_MAGIC, out, 256, src, source_va, len(raw), dest_va))
        self.uc.reg_write(UC_X86_REG_ESP, esp)
        self.uc.emu_start(self.entry, RET_MAGIC, count=100000)
        n = self.uc.reg_read(UC_X86_REG_EAX)
        return bytes(self.uc.mem_read(out, n)) if n else None


def form(insn) -> str:
    """Mnemonic and opcode form of an instruction, e.g. 'imul 0F AF', 'dec FE /1', 'mov 66 89'."""
    b = bytes(insn.bytes)
    i = 0
    while b[i] in (0x26, 0x2E, 0x36, 0x3E, 0x64, 0x65, 0x66, 0x67, 0xF0, 0xF2, 0xF3):
        i += 1
    opcode = b[: i + (2 if b[i] == 0x0F else 1)]
    text = f"{insn.mnemonic} {opcode.hex(' ').upper()}"
    if opcode[-1] in (0x80, 0x81, 0x83, 0xC0, 0xC1, 0xD0, 0xD1, 0xD3, 0xF6, 0xF7, 0xFE, 0xFF) and len(b) > len(opcode):
        text += f" /{(b[len(opcode)] >> 3) & 7}"
    return text


def survey(text: bytes, text_va: int, sites, relocate: Relocator) -> list[tuple[str, int, int, str | None]]:
    """[(site id, va, length, reason or None when relocatable)] for every resolvable jmp site."""
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    rows = []
    for s in sites:
        if s.kind != "jmp":
            continue
        ok, _, va, _ = sitecheck.check_site(text, text_va, s)
        if not ok:
            continue
        raw = text[va - text_va : va - text_va + s.length]
        if relocate(raw, va) is not None:
            rows.append((s.id, va, s.length, None))
            continue
        reason = "span"
        for insn in md.disasm(raw, va):
            alone = bytes(insn.bytes) + b"\x90" * max(0, 5 - insn.size)
            if relocate(alone, insn.address) is None:
                reason = form(insn)
                break
        rows.append((s.id, va, s.length, reason))
    return rows


def game_text(exe: Path) -> tuple[bytes, int]:
    pe = pefile.PE(str(exe), fast_load=True)
    sec = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
    return sec.get_data()[: sec.Misc_VirtualSize], pe.OPTIONAL_HEADER.ImageBase + sec.VirtualAddress


def main(argv: list[str]) -> int:
    listing = "--list" in argv
    args = [a for a in argv if a != "--list"]
    _, sites = load(Path(args[0]) if args else sitecheck.DEFAULT_SITES)
    text, text_va = game_text(Path(args[1]) if len(args) > 1 else sitecheck.DEFAULT_EXE)
    rows = survey(text, text_va, sites, Relocator())
    rejected = [r for r in rows if r[3]]
    for reason, n in collections.Counter(r[3] for r in rejected).most_common():
        print(f"{n:4}  {reason}")
    if listing:
        for sid, va, length, reason in rejected:
            print(f"  {sid:10} {va:08X} {length:2}  {reason}")
    print(f"{len(rejected)} of {len(rows)} jmp sites rejected")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
