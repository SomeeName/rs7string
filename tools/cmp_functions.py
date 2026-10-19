#!/usr/bin/env python3
"""Prove that a source change left every function of the DLL (or test exe) as it was: compare two builds.

    uv run python tools/cmp_functions.py OLD.dll OLD.map NEW.dll NEW.map [--data] [--alias NAMESPACE]...

Link both builds with /MAP (`$env:_LINK_ = '/MAP'` before building: no source or project change;
scripts/compare_builds.ps1 does that). The map files give the symbols: every function (publics and "Static
symbols", flag `f`) with its address; its extent runs to the next symbol. A function of one build is decoded
with capstone and compared with the function of the same name in the other (not of the same object file: moved
or split code changes the object file). For moved or split source files build with optimisation and inlining off
(compare_builds.ps1 -NoOpt, /Od /Ob0): in the Release build a split changes inlining and frame layout.

  * decoding follows the code from the entry (branches, jump tables); what that does not reach (exception
    funclets) is decoded from its first byte on; int3 / zero padding is dropped;
  * a branch, call or memory operand that points into the image becomes a symbol name (`name` or
    `name+0xOFF`), a branch inside the function the position of its target instruction (`L12`): moved code
    compares equal, a changed call, data reference or instruction does not;
  * addresses outside the image (the game's, the OS's) and every other operand stay literal; an immediate
    counts as an address when it points into code or starts a symbol, and when that does not make the two
    builds agree, as a plain constant (the test exe loads at 0x400000, where constants look like addresses);
  * names that move with the source layout are normalised: a decorated name is demangled (dbghelp; the
    compressed form depends on where the namespaces are), the anonymous namespace (`?A0x<path hash>`) and the
    --alias namespaces are dropped, lambda positions and the scope numbers of function-local statics are
    masked, string literals (`$SG1234`, `??_C@_..`) are named after their content; functions that then share
    a name (lambdas, file-local functions of several objects) are compared as sorted lists;
  * a library function (inline / COMDAT code of the CRT) that the linker took from a different library member
    in the two builds is ignored and counted in a note: which member comes first depends on the objects before
    it;
  * functions folded by the linker (/OPT:ICF) are one address with several names; the first name stands
    for it; the line number argument of tests/test.h `rs7test::Fail` is masked.

Every function is compared, exported or not (CRT and ImGui included). With --data the data symbols
(.data / .rdata / .bss contents between two map symbols) are compared the same way: image pointers become
names (or RVAs, in tables of RVAs); of a name that exists once per object file (file-local data) the distinct
contents are compared, trailing zero bytes (alignment) are dropped, a word that looks like an address may
also agree as a number, and strings and the import tables (their order follows the first use) are not
compared; the __DATE__ / __TIME__ strings, the debug directory and the export directory's timestamp are masked.

Exit status 0 when every function (and with --data every data symbol) is equal, 1 otherwise.
"""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import re
import sys
from bisect import bisect_right
from dataclasses import dataclass, replace
from pathlib import Path

import capstone
import capstone.x86 as x86
import pefile

_PUSH_IMM = re.compile(r"push (0x[0-9a-f]+|-?\d+)$")
_MARK = re.compile(r"([0-9a-f]+)")  # an in-function address, resolved to a position later
_HEX = re.compile(r"0x[0-9a-fA-F]+")
_LINE = re.compile(r"^\s*([0-9a-f]{4}):([0-9a-f]{8})\s+(\S+)\s+([0-9a-f]{8,16})\s+((?:f|i|f i)\s+)?(\S+)\s*$")
_SECTION = re.compile(r"^\s*([0-9a-f]{4}):([0-9a-f]{8})\s+([0-9a-f]{8})H\s+(\S+)\s+(CODE|DATA)\s*$")
# Names that move with the source layout: lambda_<hash of the source position>, ?A0x<hash of the path> (anonymous
# namespace) and the scope number of a function-local static (`?logged@?4??F@@9@4HA`: the 4th block of F).
_UNSTABLE = re.compile(r"lambda_[0-9a-f]{32}|\?A0x[0-9a-f]{8}|(?<=@)\?[0-9A-Z]+(?=@?\?\?)")
_LAMBDA_HASH = re.compile(rb"lambda_[0-9a-f]{32}")  # inside RTTI type descriptors
STRING_PREFIX = "<string #"
_IMPORT = re.compile(r"__imp_|__NULL_IMPORT_DESCRIPTOR|__IMPORT_DESCRIPTOR_|.*NULL_THUNK_DATA|___volatile_metadata")
_STRING = re.compile(r"\$SG\d+$|\?\?_C@_")  # string literals: `$SG1234` (no /GF) or `??_C@_0<len>@<hash>@<text>@`
_FUNCLET = re.compile(r"(__\w+\$)(\?.*?)((?:\$\d+)?)$")  # exception funclets are named after their function
_DM_UNSTABLE = r"<lambda_[0-9a-f]{32}>|`\d+'"  # the same, in a demangled name (see Names.canon)
_DM_SCOPE = r"\bA0x[0-9a-f]{8}::|`anonymous namespace'::"  # the anonymous namespace, as a type's scope or a function's
_TIME = re.compile(rb"^\d\d:\d\d:\d\d\x00$")
_DATE = re.compile(rb"^[A-Z][a-z]{2} [ 0-9]\d \d{4}\x00$")


@dataclass(frozen=True)
class Sym:
    section: int
    offset: int
    va: int
    name: str
    func: bool
    obj: str


def _dm_unstable(m: re.Match) -> str:
    text = m.group(0)
    return "<lambda_#>" if text[1] == "l" else "`#'" if text[0] == "`" and text[1].isdigit() else ""


def _unstable(m: re.Match) -> str:
    text = m.group(0)
    return "lambda_#" if text[0] == "l" else "?A0x#" if text.startswith("?A0x") else "?#"


try:
    _UNDNAME = ctypes.WinDLL("dbghelp").UnDecorateSymbolName
except (AttributeError, OSError):  # not Windows: names stay decorated
    _UNDNAME = None


def _demangle(name: str) -> str | None:
    buf = ctypes.create_string_buffer(4096)
    return buf.value.decode(errors="replace") if _UNDNAME and _UNDNAME(name.encode(), buf, len(buf), 0) else None


class Names:
    """Normalises symbol names; `alias` namespaces count as the anonymous namespace (code moved out of
    `namespace {}`: `?f@@YAXXZ` becomes `?f@ns@@YAXXZ`)."""

    def __init__(self, alias: tuple[str, ...] = ()):
        group = "|".join(map(re.escape, alias))
        self.raw = re.compile(rf"(?<=@)(?:{group})(?=@)") if alias else None
        scopes = _DM_SCOPE + (rf"|\b(?:{group})::" if alias else "")
        self.demangled = re.compile(f"{_DM_UNSTABLE}|{scopes}")

    def canon(self, name: str) -> str:
        """The name without what moves with the source layout.

        A decorated name is compared demangled: its compressed form (`Head@1@` or `Head@ns@@`, depending on
        where the namespaces are) differs between two builds that mean the same type. The anonymous namespace
        and the `alias` namespaces are dropped (no path hash, and moving a symbol in or out of `namespace {}`
        is not a code change), lambda positions and the scope numbers of local statics are masked. What does
        not demangle is masked in its decorated form (see _UNSTABLE)."""
        m = _FUNCLET.match(name)
        if m:  # __ehhandler$?f@@YAXXZ, __catch$?f@@YAXXZ$0: the decorated name inside
            return m.group(1) + self.canon(m.group(2)) + m.group(3)
        if name.startswith("?"):
            text = _demangle(name)
            if text and text != name:
                return self.demangled.sub(_dm_unstable, text)
        return _UNSTABLE.sub(_unstable, self.raw.sub("?A0x#", name) if self.raw else name)


def parse_map(path: Path, alias: tuple[str, ...] = ()) -> tuple[list[Sym], dict[int, str]]:
    """(symbols, {section number: class CODE or DATA}) of an MSVC /MAP file.

    The namespaces in `alias` count as the anonymous namespace (code moved out of one: `?f@@YAXXZ` in
    `namespace {}` becomes `?f@ns@@YAXXZ`)."""
    names = Names(alias)
    syms: list[Sym] = []
    classes: dict[int, str] = {}
    for line in path.read_text(errors="replace").splitlines():
        m = _SECTION.match(line)
        if m:
            classes.setdefault(int(m.group(1), 16), m.group(5))
            continue
        m = _LINE.match(line)
        if m and int(m.group(1), 16) != 0:
            sec, off, name, va, flags, obj = m.groups()
            name = names.canon(name)
            syms.append(Sym(int(sec, 16), int(off, 16), int(va, 16), name, bool(flags and "f" in flags), obj))
    return syms, classes


def _literal(raw: bytes) -> bytes:
    """The characters of a string literal in `raw` (up to the symbol's end, which may include alignment padding
    and a following unnamed item): up to the first terminator, 1 or 2 bytes wide (a wide string starts with a
    character below 0x100)."""
    unit = 2 if len(raw) >= 2 and raw[0] and not raw[1] else 1
    for i in range(0, len(raw) - unit + 1, unit):
        if not any(raw[i : i + unit]):
            return raw[:i]
    return raw


class Image:
    """A PE image with its map symbols."""

    def __init__(self, dll: Path, map_path: Path, alias: tuple[str, ...] = ()):
        self.pe = pefile.PE(str(dll))
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.size = self.pe.OPTIONAL_HEADER.SizeOfImage
        mem = bytearray(self.pe.get_memory_mapped_image())
        d = self.pe.OPTIONAL_HEADER.DATA_DIRECTORY[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_DEBUG"]]
        spans = [(d.VirtualAddress, d.Size)]
        spans += [
            (e.struct.AddressOfRawData, e.struct.SizeOfData) for e in getattr(self.pe, "DIRECTORY_ENTRY_DEBUG", [])
        ]
        for a, n in spans:
            mem[a : a + n] = bytes(n)
        self.mem = bytes(mem)
        self.syms, self.classes = parse_map(map_path, alias)
        # the __DATE__ / __TIME__ strings are named after a hash of their content: one name for both
        self.syms = [
            replace(s, name="<__DATE__/__TIME__>")
            if _STRING.match(s.name) and (_TIME.match(self.read(s.va, 9)) or _DATE.match(self.read(s.va, 12)))
            else s
            for s in self.syms
        ]
        self.syms.sort(key=lambda s: (s.va, s.name))
        self.objs: dict[int, str] = {}
        for s in self.syms:
            self.objs.setdefault(s.va, s.obj)
        self._name_strings()
        # one canonical name per address (aliases come from /OPT:ICF and from aliased publics)
        self.at: dict[int, str] = {}
        for s in self.syms:
            self.at.setdefault(s.va, s.name)
        self.vas = sorted(self.at)
        self.code_ranges = [
            (self.base + sec.VirtualAddress, self.base + sec.VirtualAddress + sec.Misc_VirtualSize)
            for sec in self.pe.sections
            if sec.Characteristics & 0x20  # IMAGE_SCN_CNT_CODE
        ]
        self.sec_end: dict[int, int] = {}
        for i, sec in enumerate(self.pe.sections, 1):
            self.sec_end[i] = self.base + sec.VirtualAddress + max(sec.Misc_VirtualSize, sec.SizeOfRawData)

    def _name_strings(self) -> None:
        """Name every string literal after its content: the numbers of `$SG1234` and the hashes of `??_C@_..`
        follow the source layout, the text does not."""
        ends: dict[int, list[int]] = {}
        for s in self.syms:
            ends.setdefault(s.section, []).append(s.va)
        out = []
        for s in self.syms:
            if not _STRING.match(s.name):
                out.append(s)
                continue
            vas = sorted(set(ends[s.section]))
            k = bisect_right(vas, s.va)
            end = vas[k] if k < len(vas) else s.va + 1
            text = _literal(self.read(s.va, min(end, self.base + self.size) - s.va))
            digest = hashlib.blake2b(text, digest_size=6).hexdigest()
            shown = text.replace(bytes(1), b"").decode("ascii", "replace")[:30]
            out.append(replace(s, name=f"{STRING_PREFIX}{digest} {shown!r}>"))
        self.syms = out

    def is_code(self, va: int) -> bool:
        return any(a <= va < b for a, b in self.code_ranges)

    def inside(self, va: int) -> bool:
        return self.base <= va < self.base + self.size

    def name(self, va: int) -> str:
        i = bisect_right(self.vas, va) - 1
        if i < 0:
            return f"{va:#x}"
        sym = self.vas[i]
        name = self.at[sym]
        return name if sym == va else f"{name}+{va - sym:#x}"

    def read(self, va: int, n: int) -> bytes:
        rva = va - self.base
        return self.mem[rva : rva + n]

    def extents(self, want_funcs: bool) -> dict[str, list[tuple[int, int]]]:
        """{name: [(start va, end va), ...]} of the functions (or of the data symbols).

        The object file is not part of the key: splitting or moving a source file moves its functions to other
        object files. More than one extent per name happens for lambdas (their names hash the source position,
        which a formatting change moves), file-local functions of one name, and symbols the linker kept once per
        object file; see compare()."""
        by_sec: dict[int, list[Sym]] = {}
        for s in self.syms:
            by_sec.setdefault(s.section, []).append(s)
        out: dict[str, list[tuple[int, int]]] = {}
        for sec, items in by_sec.items():
            code = self.classes.get(sec) == "CODE"
            vas = sorted({s.va for s in items})
            for s in items:
                if code != want_funcs or (want_funcs and not s.func) or s.va < self.base + 0x1000:
                    continue  # (symbols at the image base are the import-table names of the PE header)
                k = bisect_right(vas, s.va)
                end = vas[k] if k < len(vas) else self.sec_end.get(sec, s.va + 1)
                out.setdefault(s.name, []).append((s.va, end))
        return out


def _symbolize(img: Image, ins, start: int, end: int, literal: bool = False) -> str:
    """The operand text of `ins` with image addresses as names.

    Memory displacements and branch targets are addresses; an immediate when it points into code (a label
    or function address) or is exactly the start of a symbol (the test exe loads at 0x400000, where plain
    constants such as 0x500000 lie inside the image, so data addresses need the exact match). With `literal`
    only displacements and branch targets are names; compare() falls back to it for a function whose
    immediates are plain constants that happen to look like code addresses."""
    disps = {op.mem.disp & 0xFFFFFFFF for op in ins.operands if op.type == x86.X86_OP_MEM}
    branch = bool(ins.group(capstone.CS_GRP_JUMP) or ins.group(capstone.CS_GRP_CALL))

    def sub(m: re.Match) -> str:
        v = int(m.group(0), 16)
        if (
            v in disps
            or branch
            or (not literal and (v in img.at or img.is_code(v) or img.inside(v) and v >= img.base + 0x1000))
        ):
            if start <= v < end:
                return f"{v:x}"
            if img.inside(v):
                return "<" + img.name(v) + ">"
        return m.group(0)

    return _HEX.sub(sub, ins.op_str)


def decode(img: Image, start: int, end: int, literal: bool = False) -> list[str]:
    """Normalised instruction (and data) list of the function at [start, end).

    The code is followed from the entry (branches, jump tables). What that does not reach is either data
    (jump tables, int3 padding) or code only an exception table points to (unwind / catch funclets): such a gap
    is decoded as code from its first byte on, int3 bytes and jump-table words excepted."""
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    code = img.read(start, end - start)
    done: dict[int, tuple[int, str]] = {}  # va -> (size, text)
    table_words: set[int] = set()

    def follow(va: int) -> bool:
        """Decode from `va` until the path ends; False when `va` is not code at all."""
        work, first = [va], True
        while work:
            va = work.pop()
            while start <= va < end and va not in done:
                ins = next(md.disasm(code[va - start : va - start + 16], va, 1), None)
                if ins is None or ins.id == x86.X86_INS_INT3:
                    if first:
                        return False
                    break
                first = False
                done[va] = (ins.size, f"{ins.mnemonic} {_symbolize(img, ins, start, end, literal)}".strip())
                va += ins.size
                op = ins.operands[0] if ins.operands else None
                if ins.group(capstone.CS_GRP_JUMP) and op is not None:
                    if op.type == x86.X86_OP_IMM and start <= op.imm < end:
                        work.append(op.imm)
                    elif op.type == x86.X86_OP_MEM and ins.id == x86.X86_INS_JMP:
                        t = op.mem.disp
                        while start <= t < end - 3 and t not in table_words:
                            word = int.from_bytes(code[t - start : t - start + 4], "little")
                            if not start <= word < end:
                                break
                            table_words.add(t)
                            work.append(word)
                            t += 4
                if ins.id in (x86.X86_INS_RET, x86.X86_INS_JMP) or ins.mnemonic in ("retn", "ud2"):
                    break
        return True

    follow(start)
    va = start
    while va < end:
        if va in done:
            va += done[va][0]
        elif va in table_words:
            va += 4
        else:
            if code[va - start] not in (0x00, 0xCC):  # zero and int3 bytes are padding
                follow(va)
            va += 1 if va not in done else done[va][0]
    out: list[str] = []
    index: dict[int, int] = {}  # va of an item -> its position (branch targets compare by position: an
    va = start  # instruction may be encoded shorter or longer, e.g. `push 0x7f` / `push 0x80`)
    while va < end:
        if va in done:
            size, text = done[va]
            index[va] = len(out)
            out.append(text)
            va += size
        elif va in table_words:
            word = int.from_bytes(code[va - start : va - start + 4], "little")
            out.append(f"dd {word:x}" if start <= word < end else f"dd {word:#x}")
            va += 4
        else:
            out.append(f"db {code[va - start]:#04x}")
            va += 1
    while out and out[-1] in ("db 0xcc", "db 0x00"):
        out.pop()
    out = [
        _MARK.sub(
            lambda m: (
                f"L{index[int(m.group(1), 16)]}"
                if int(m.group(1), 16) in index
                else f"L+{int(m.group(1), 16) - start:#x}"
            ),
            t,
        )
        for t in out
    ]
    for i, text in enumerate(out):
        # tests/test.h: Fail(__FILE__, __LINE__, expr) pushes the line number, which moves with the source layout
        if text.startswith("call <") and "rs7test::Fail(" in text and i >= 2 and _PUSH_IMM.match(out[i - 2]):
            out[i - 2] = "push <__LINE__>"
    return out


def data_tokens(img: Image, start: int, end: int, literal: bool = False) -> list[str]:
    """A data symbol as 4-byte words (image pointers symbolised, unless `literal`) and trailing bytes;
    __DATE__/__TIME__ masked. Trailing zero bytes are dropped: they are the alignment padding up to the next
    symbol, which moves with the layout."""
    raw = _LAMBDA_HASH.sub(lambda m: b"lambda_" + b"#" * 32, img.read(start, end - start))
    if _TIME.match(raw[:9]) or _DATE.match(raw[:12]):
        return ["<__DATE__/__TIME__>"]
    raw = raw.rstrip(bytes(1))
    out = []
    for i in range(0, len(raw) - 3, 4):
        w = int.from_bytes(raw[i : i + 4], "little")
        if literal:
            out.append(f"{w:#x}")
        elif img.inside(w) and w >= img.base + 0x1000:
            out.append("<" + img.name(w) + ">")
        elif w >= 0x1000 and img.base + w in img.at:  # a table of RVAs
            out.append("<rva " + img.at[img.base + w] + ">")
        else:
            out.append(f"{w:#x}")
    out += [f"{b:#04x}" for b in raw[len(raw) // 4 * 4 :]]
    return out


@dataclass
class Report:
    compared: int = 0
    names: int = 0
    insns: int = 0
    only_old: list[str] = None
    only_new: list[str] = None
    diffs: list[str] = None
    lib_picks: list[str] = None


def _agree(old: Image, new: Image, fa: tuple, fb: tuple, make) -> bool:
    """Two decoded functions or data symbols (items, start, end) agree item by item, each either symbolically
    or literally.

    An immediate or data word that looks like an image address is either one (it follows the layout: equal as
    names) or a plain constant (equal as numbers); the test exe, loaded at 0x400000, and tables of random
    words (CRC) have that ambiguity."""
    (ia, sa, ea), (ib, sb, eb) = fa, fb
    if len(ia) != len(ib):
        return False
    la, lb = make(old, sa, ea, True), make(new, sb, eb, True)
    return len(la) == len(lb) == len(ia) and all(x == y or p == q for x, y, p, q in zip(ia, ib, la, lb))


def _distinct(items: list[tuple]) -> list[tuple]:
    seen: dict[tuple, tuple] = {}
    for x in items:
        seen.setdefault(tuple(x[0]), x)
    return list(seen.values())


def _other_member(old: Image, new: Image, xa: list, xb: list) -> bool:
    """True when the linker took this (inline / COMDAT) function from a different library member in the two
    builds: which member it meets first depends on the objects before it, not on our code."""
    oa, ob = {old.objs[s] for s, _ in xa}, {new.objs[s] for s, _ in xb}
    return oa != ob and all(":" in o for o in oa | ob)


def compare(old: Image, new: Image, funcs: bool = True) -> Report:
    """Compare the functions (or data symbols) of two images, matched by name (not by object file).

    MSVC names a lambda after a hash of its source position, so moving a lambda by one line renames it (and
    its std::function glue). Names are therefore normalised (`lambda_#`, `?A0x#`); when several functions
    share a normalised name, the two sets are compared as sorted lists."""
    ea, eb = old.extents(funcs), new.extents(funcs)
    rep = Report(only_old=sorted(set(ea) - set(eb)), only_new=sorted(set(eb) - set(ea)))
    rep.diffs, rep.lib_picks = [], []
    seen_va: set[int] = set()
    make = decode if funcs else data_tokens
    for key in sorted(set(ea) & set(eb)):
        xa, xb = ea[key], eb[key]
        if not funcs and (key.startswith(STRING_PREFIX) or _IMPORT.match(key)):
            continue  # a string is named after its content; the import tables follow the order of first use
        if funcs and len(xa) != len(xb):
            rep.diffs.append(f"{key}: {len(xa)} of this name in old, {len(xb)} in new")
            continue
        fa = sorted((make(old, s, e), s, e) for s, e in xa)
        fb = sorted((make(new, s, e), s, e) for s, e in xb)
        if not funcs:  # one copy per object file is normal for file-local data: compare the distinct contents
            fa, fb = _distinct(fa), _distinct(fb)
            if len(fa) != len(fb):
                rep.diffs.append(f"{key}: {len(fa)} distinct contents in old, {len(fb)} in new")
                continue
        rep.names += len(xa)
        for items, start, _ in fa:
            if start not in seen_va:
                seen_va.add(start)
                rep.compared += 1
                rep.insns += len(items)
        for x, y in zip(fa, fb):
            (ia, _, _), (ib, _, _) = x, y
            if ia == ib or _agree(old, new, x, y, make):
                continue
            if funcs and _other_member(old, new, xa, xb):
                rep.lib_picks.append(key)
                break
            k = next((i for i, (p, q) in enumerate(zip(ia, ib)) if p != q), min(len(ia), len(ib)))
            rep.diffs.append(
                f"{key}{f' [one of {len(xa)}]' if len(xa) > 1 else ''}: {len(ia)} vs {len(ib)} items,"
                f" first difference at {k}: old {ia[k] if k < len(ia) else '<end>'!r}"
                f" new {ib[k] if k < len(ib) else '<end>'!r}"
            )
            break
    return rep


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("old_pe", type=Path)
    ap.add_argument("old_map", type=Path)
    ap.add_argument("new_pe", type=Path)
    ap.add_argument("new_map", type=Path)
    ap.add_argument("--data", action="store_true", help="also compare the data symbols")
    ap.add_argument(
        "--alias",
        action="append",
        default=[],
        metavar="NAMESPACE",
        help="treat this namespace as the anonymous one (code moved out of `namespace {}`); repeatable",
    )
    ap.add_argument("--limit", type=int, default=40, help="differences to print per kind")
    args = ap.parse_args(argv)
    old, new = Image(args.old_pe, args.old_map, tuple(args.alias)), Image(args.new_pe, args.new_map, tuple(args.alias))
    bad = 0
    for label, funcs in (("functions", True),) + ((("data symbols", False),) if args.data else ()):
        rep = compare(old, new, funcs)
        unit = "instructions" if funcs else "words"
        print(f"{label}: {rep.compared} compared ({rep.names} names, {rep.insns} {unit}), {len(rep.diffs)} differ")
        if rep.lib_picks:
            print(f"  note: {len(rep.lib_picks)} library function(s) come from another member (link order), ignored")
        for what, items in (("only in old", rep.only_old), ("only in new", rep.only_new), ("differs", rep.diffs)):
            if items:
                bad += len(items)
                print(f"  {what}: {len(items)}")
                for line in items[: args.limit]:
                    print("    " + line)
    print("ALL EQUAL" if not bad else f"{bad} problem(s)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
