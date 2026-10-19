"""Freeze compiled naked stubs of a built dinput8.dll into a symbolic JSON fixture.

Used to keep the old hand-written stubs as an executable oracle after they are replaced by C++
mid-hook callbacks (tools/tests/test_midhook_cold.py runs old and new on identical inputs in
Unicorn). Only code emitted by this repository's own build is stored; no game bytes.

    uv run python tools\\freeze_stubs.py [--array=SYMBOL ...] [--map=DLL.map --local=NAME ...] DLL OUT.json "<source tag>" NAME [NAME ...]

--array=SYMBOL names an exported array that `[SYMBOL + index*4 - k]` operands point just below (k <= 64).
--map=DLL.map --local=NAME: a stub calls NAME, a function the old DLL does not export; its entry is taken from the
linker map (`$env:_LINK_ = '/MAP'`, no image change) and the fixup is named NAME, so the oracle links it against the
DLL under test, which must export it.

NAME is an exported function (e.g. Rs7_Stub_TK or Rs7_PegOwnedZF). The stub's extent is found by
recursive descent (conditional/unconditional relative branches inside the image, stopping at ret,
jmp reg, jmp [mem]); calls are not followed, except to a label of the stub itself (before the next
export). Every 32-bit field that points into the image becomes
a fixup: {"offset", "symbol", "addend", "relative"}. A target that is the entry of another frozen
NAME is written as "@NAME". The result is verified by re-linking the blob against the same DLL.
"""

from __future__ import annotations

import bisect
import json
import struct
import sys
from pathlib import Path

import capstone
import pefile
from capstone.x86_const import X86_OP_IMM, X86_OP_MEM

MAX_ADDEND = 0x400


class Image:
    def __init__(self, path: Path):
        self.pe = pefile.PE(str(path))
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.size = self.pe.OPTIONAL_HEADER.SizeOfImage
        self.mem = bytearray(self.size)
        for s in self.pe.sections:
            data = s.get_data()
            self.mem[s.VirtualAddress : s.VirtualAddress + len(data)] = data
        self.exports = {
            e.name.decode(): self.base + e.address for e in self.pe.DIRECTORY_ENTRY_EXPORT.symbols if e.name
        }
        self.by_addr = sorted((a, n) for n, a in self.exports.items())
        self.addrs = [a for a, _ in self.by_addr]
        self.cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.cs.detail = True
        self.arrays: list[str] = []  # exported arrays referenced by `[array + i*4 - k]` operands

    def is_code(self, va: int) -> bool:
        rva = va - self.base
        return any(
            s.VirtualAddress <= rva < s.VirtualAddress + s.Misc_VirtualSize and s.Characteristics & 0x20000000
            for s in self.pe.sections
        )

    def inside(self, va: int) -> bool:
        return self.base <= va < self.base + self.size

    def read(self, va: int, n: int) -> bytes:
        return bytes(self.mem[va - self.base : va - self.base + n])

    def symbol(self, va: int) -> tuple[str, int]:
        # `[array + index*4 - 4]` operands point just below the array: a hinted array wins, unless an export starts at va.
        exact = [n for a, n in self.by_addr if a == va]
        for name in self.arrays if not exact else ():
            if 0 < self.exports[name] - va <= 64:
                return name, va - self.exports[name]
        i = bisect.bisect_right(self.addrs, va) - 1
        if i < 0:
            raise ValueError(f"{va:#x}: no symbol below")
        addr, name = self.by_addr[i]
        # exact names win over a same-address alias
        for a, n in self.by_addr[i::-1]:
            if a != addr:
                break
            if n.startswith(("Rs7", "g_rs7")):
                name = n
        if va - addr > MAX_ADDEND:
            raise ValueError(f"{va:#x}: nearest export {name} is {va - addr:#x} below")
        return name, va - addr


def add_map_symbols(img: Image, map_path: Path, names: list[str]) -> None:
    """Name the entries of the non-exported functions `names` (extern "C" cdecl: `_NAME` in the map) like exports."""
    found = {}
    for line in map_path.read_text(errors="replace").splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[1].lstrip("_") in names and parts[1] == "_" + parts[1].lstrip("_"):
            found[parts[1][1:]] = int(parts[2], 16)
    missing = set(names) - set(found)
    assert not missing, f"not in {map_path}: {sorted(missing)}"
    for name, va in found.items():
        assert img.is_code(va), f"{name} at {va:#x} is not code"
        img.exports[name] = va
    img.by_addr = sorted((a, n) for n, a in img.exports.items())
    img.addrs = [a for a, _ in img.by_addr]


def extent(img: Image, start: int) -> tuple[int, list]:
    """Instructions reachable from start; returns (end, [insn]). A call before the next export is a local subroutine
    of the stub (a label it calls, e.g. the replayed prologue of a function it runs as a subroutine) and is followed."""
    later = [a for a in img.addrs if a > start]
    local_end = min(later) if later else start + 0x400
    todo, seen, insns = [start], set(), {}
    while todo:
        pc = todo.pop()
        while pc not in seen:
            seen.add(pc)
            code = img.read(pc, 16)
            ins = next(img.cs.disasm(code, pc), None)
            if ins is None:
                raise ValueError(f"cannot decode at {pc:#x}: {code.hex()}")
            insns[pc] = ins
            m = ins.mnemonic
            if m == "ret":
                break
            if m == "jmp":
                op = ins.operands[0]
                if op.type == X86_OP_IMM:
                    tgt = op.imm
                    if start <= tgt < start + 0x400 and tgt not in seen:
                        todo.append(tgt)
                    if not (start <= tgt < start + 0x400):
                        pass  # tail jump out of the stub (e.g. to a helper)
                break
            if m == "call" and ins.operands[0].type == X86_OP_IMM and start < ins.operands[0].imm < local_end:
                todo.append(ins.operands[0].imm)
            if m.startswith("j") and ins.operands[0].type == X86_OP_IMM:
                tgt = ins.operands[0].imm
                if tgt not in seen:
                    todo.append(tgt)
            pc += ins.size
    order = sorted(insns)
    end = max(a + insns[a].size for a in order)
    return end, [insns[a] for a in order]


def ins_at(insns: list, va: int) -> int:
    """The address of the instruction that holds the field at `va`."""
    return max(i.address for i in insns if i.address <= va)


def check_target(img: Image, name: str, at: int, sym: str, add: int, relative: bool) -> None:
    """A fixup must name what the instruction means: a branch or call goes to an export's entry (addend 0; a
    non-exported function would be relinked to whatever follows the export in another build), a data operand to an
    exported variable or a field inside it (addend >= 0, the export not code) or just below a hinted array."""
    where = f"{name} at {at:#x}: {sym}{add:+#x}"
    if relative:
        assert add == 0, f"{where}: the branch target is not an exported function (export it)"
    elif add < 0:
        assert sym in img.arrays, f"{where}: below an export that is not a hinted array (--array={sym}?)"
    elif add > 0:
        assert not img.is_code(img.exports[sym]), f"{where}: inside a function, not a variable"


def freeze_one(img: Image, name: str, frozen_starts: dict[int, str]) -> dict:
    start = img.exports[name]
    end, insns = extent(img, start)
    raw = bytearray(img.read(start, end - start))
    fixups = []
    for ins in insns:
        off = ins.address - start
        fields = []  # (offset in blob, size, relative)
        m = ins.mnemonic
        op0 = ins.operands[0] if ins.operands else None
        if (m == "call" or m.startswith("j")) and op0 is not None and op0.type == X86_OP_IMM:
            tgt = op0.imm
            if start <= tgt < end:
                continue  # internal branch: relative, unchanged
            size = ins.size - (1 if m in ("call", "jmp") else 2)
            if size != 4:
                raise ValueError(f"{name}: short branch leaves the stub at {ins.address:#x}")
            fields.append((off + ins.size - 4, 4, True, tgt))
        else:
            # an absolute disp32 into the image, also with a base / index register (`fld [array + edx - 18h]`)
            if ins.disp_size == 4 and any(o.type == X86_OP_MEM for o in ins.operands):
                val = struct.unpack_from("<I", raw, off + ins.disp_offset)[0]
                if img.inside(val):
                    fields.append((off + ins.disp_offset, 4, False, val))
            if ins.imm_size == 4:
                val = struct.unpack_from("<I", raw, off + ins.imm_offset)[0]
                if img.inside(val):
                    fields.append((off + ins.imm_offset, 4, False, val))
        for foff, _, rel, tgt in fields:
            if tgt in frozen_starts and frozen_starts[tgt] != name:
                sym, add = "@" + frozen_starts[tgt], 0
            else:
                sym, add = img.symbol(tgt)
                check_target(img, name, ins_at(insns, start + foff), sym, add, rel)
            fixups.append({"offset": foff, "symbol": sym, "addend": add, "relative": rel})
    return {"hex": bytes(raw).hex(" "), "fixups": fixups}


def relink(blob: dict, img: Image, va: int, frozen_vas: dict[str, int]) -> bytes:
    raw = bytearray.fromhex(blob["hex"])
    for fx in blob["fixups"]:
        sym = fx["symbol"]
        target = (frozen_vas[sym[1:]] if sym.startswith("@") else img.exports[sym]) + fx["addend"]
        value = target - (va + fx["offset"] + 4) if fx["relative"] else target
        struct.pack_into("<I", raw, fx["offset"], value & 0xFFFFFFFF)
    return bytes(raw)


def main(argv: list[str]) -> int:
    if len(argv) < 5:
        print(__doc__)
        return 2
    arrays = [a[len("--array=") :] for a in argv if a.startswith("--array=")]
    maps = [Path(a[len("--map=") :]) for a in argv if a.startswith("--map=")]
    local = [a[len("--local=") :] for a in argv if a.startswith("--local=")]
    argv = [a for a in argv if not a.startswith(("--array=", "--map=", "--local="))]
    assert bool(maps) == bool(local) and len(maps) <= 1, "--local=NAME needs exactly one --map=DLL.map"
    dll, out, source, names = Path(argv[1]), Path(argv[2]), argv[3], argv[4:]
    img = Image(dll)
    img.arrays = arrays
    if local:
        add_map_symbols(img, maps[0], local)
    frozen_starts = {img.exports[n]: n for n in names}
    result = {}
    for n in names:
        blob = freeze_one(img, n, frozen_starts)
        blob["source"] = source
        result[n] = blob
        if not all(f["symbol"].startswith(("Rs7", "g_rs7", "@")) for f in blob["fixups"]):
            print("  note: non-Rs7 symbol in", n, [f["symbol"] for f in blob["fixups"]])
        # round trip: the relinked blob must equal the DLL bytes
        va = img.exports[n]
        want = img.read(va, len(bytes.fromhex(blob["hex"])))
        got = relink(blob, img, va, {k: img.exports[k] for k in names})
        assert got == want, f"{n}: relink mismatch"
        print(f"{n}: {len(want)} bytes, {len(blob['fixups'])} fixups")
    out.write_text(json.dumps(result, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
