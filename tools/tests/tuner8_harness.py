"""Emulator harness for the native tuner session copies (core/tuner8.h, tools/gen_tuner8s.py).

The unpacked game image and the built DLL are mapped; a copied UI_PlayerTuner method runs either as the
stock function (session arrays at +38h.., count at InstrumentInfo +0Bh) or as its generated copy (arrays
in the 380h session's tail, count and order there; Rs7Tu8_Advance is the DLL's). Every call that leaves
the function into game code is recorded (target, registers, stack arguments) and returns at once with
a chosen EAX and the callee's own stack cleanup (its first `ret n`). Equivalence: the same calls and the
same session state with the tail in place of the stock arrays.
"""

from __future__ import annotations

import re
import struct
from functools import lru_cache
from pathlib import Path

import capstone
import pefile
from unicorn import UC_ARCH_X86, UC_HOOK_CODE, UC_HOOK_MEM_UNMAPPED, UC_MODE_32, UC_PROT_ALL, Uc
from unicorn.x86_const import (
    UC_X86_REG_EAX,
    UC_X86_REG_EBP,
    UC_X86_REG_EBX,
    UC_X86_REG_ECX,
    UC_X86_REG_EDI,
    UC_X86_REG_EDX,
    UC_X86_REG_EIP,
    UC_X86_REG_ESI,
    UC_X86_REG_ESP,
)

REPO = Path(__file__).resolve().parents[2]
ROOT = REPO.parent
EXE = ROOT / "original" / "Rocksmith2014.unpacked.exe"
DLL = REPO / "bin" / "Release" / "dinput8.dll"
HEADER = REPO / "src" / "core" / "tuner8s.generated.h"
TEXT_LO, TEXT_HI = 0x401000, 0x10B7000

SESSION = 0x20000000
GUITAR, INFO, GS, OWNER = 0x20010000, 0x20011000, 0x20012000, 0x20013000
WORLD = 0x20100000  # zero-filled scratch the remaining pointers lead to
STACK = 0x21000000
COPY = 0x22000000
RET = 0x23000000  # the caller's return address: hlt
SYN = 0x70000000
SYMS = ["Rs7Tu8_Advance", "Rs7Tu8_OnSelect", "Rs7Tu8_First", "g_rs7_tu8_labels", "Rs7Asset_LetterInline"]

ARRAYS = [
    (0x38, 0x280, 0x18),
    (0x50, 0x2A0, 0x18),
    (0x80, 0x2C8, 0x18),
    (0x114, 0x2E8, 0x30),
    (0x144, 0x328, 0x18),
    (0x160, 0x348, 0x18),
]
COUNT, NATIVE, ORDER = 0x368, 0x369, 0x36C
REGS = {
    "eax": UC_X86_REG_EAX,
    "ebx": UC_X86_REG_EBX,
    "ecx": UC_X86_REG_ECX,
    "edx": UC_X86_REG_EDX,
    "esi": UC_X86_REG_ESI,
    "edi": UC_X86_REG_EDI,
    "ebp": UC_X86_REG_EBP,
}


@lru_cache(maxsize=1)
def specs() -> dict:
    """The generated copies: id -> (va, stock length, code, [(offset, target)])."""
    text = HEADER.read_text(encoding="utf-8")
    out = {}
    for m in re.finditer(r'\{"(\w+)", 0x([0-9a-f]+), (\d+), 0x[0-9a-f]+u, (\d+), (\d+), kTu8Code_\w+', text):
        fid = m.group(1)
        code = re.search(r"kTu8Code_%s\[\] = \{(.*?)\};" % fid, text, re.S).group(1)
        rels = re.search(r"kTu8Rel_%s\[\] = \{(.*?)\};" % fid, text, re.S).group(1)
        relist = []
        for r in re.finditer(r"\{(\d+), (kTu8Sym \+ (\d+)|0x([0-9a-f]+)), (\d)\}", rels):
            relist.append(
                (int(r.group(1)), SYN + int(r.group(3)) if r.group(3) else int(r.group(4), 16), int(r.group(5)))
            )
        n = int(
            re.search(
                r'\{"%s", 0x[0-9a-f]+, \d+, 0x[0-9a-f]+u, \d+, \d+, kTu8Code_\w+, sizeof kTu8Code_\w+, '
                r"kTu8Rel_\w+, (\d+)\}" % fid,
                text,
            ).group(1)
        )
        out[fid] = (
            int(m.group(2), 16),
            int(m.group(3)),
            bytes(int(x, 16) for x in re.findall(r"0x([0-9a-f]{2})", code)),
            relist[:n],
        )
    return out


@lru_cache(maxsize=1)
def image():
    pe = pefile.PE(str(EXE), fast_load=True)
    return pe, [(pe.OPTIONAL_HEADER.ImageBase + s.VirtualAddress, s.get_data()) for s in pe.sections]


@lru_cache(maxsize=1)
def dll_image():
    pe = pefile.PE(str(DLL))
    base = pe.OPTIONAL_HEADER.ImageBase
    blobs = [(base, pe.header)] + [(base + s.VirtualAddress, s.get_data()) for s in pe.sections]
    exports = {e.name.decode(): base + e.address for e in pe.DIRECTORY_ENTRY_EXPORT.symbols if e.name}
    return base, (pe.OPTIONAL_HEADER.SizeOfImage + 0xFFF) & ~0xFFF, blobs, exports


@lru_cache(maxsize=None)
def cleanup(target: int) -> int:
    """Bytes the callee pops: its first `ret n` (MSVC functions return one way)."""
    pe, secs = image()
    hit = next(((b, d) for b, d in secs if b <= target < b + len(d)), None)
    if hit is None:  # a call through a NULL / fake pointer: plain ret
        return 0
    base, data = hit
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    for i in md.disasm(data[target - base : target - base + 0x3000], target):
        if i.mnemonic == "ret":
            return int(i.op_str, 16) if i.op_str else 0
    return 0


@lru_cache(maxsize=None)
def caller_pops(ret: int) -> int:
    """A cdecl call's argument count: the caller's `add esp, n` at the return address (game code)."""
    if not TEXT_LO <= ret < TEXT_HI:
        return 4
    pe, secs = image()
    base, data = next((b, d) for b, d in secs if b <= ret < b + len(d))
    i = next(
        capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32).disasm(data[ret - base : ret - base + 8], ret), None
    )
    if i is not None and i.mnemonic == "add" and i.op_str.startswith("esp, "):
        return min(4, int(i.op_str[5:], 16) // 4)
    return 0


class Machine:
    def __init__(self, native: bool):
        self.native = native
        uc = self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        # The game image is mapped page by page on first touch (_zero_page).
        for a, n in ((SESSION, 0x800000), (STACK - 0x10000, 0x20000), (COPY, 0x10000), (RET, 0x1000)):
            uc.mem_map(a, n, UC_PROT_ALL)
        uc.mem_write(RET, b"\xf4")
        self.exports = self._load_dll()
        # InterlockedIncrement / InterlockedDecrement (IAT 10B7068h / 10B706Ch): stdcall, one argument.
        for slot in (0x10B7068, 0x10B706C):
            FAKE_POPS[struct.unpack("<I", self.read(slot, 4))[0]] = 4
        uc.hook_add(UC_HOOK_MEM_UNMAPPED, self._zero_page)
        uc.hook_add(UC_HOOK_CODE, self._code)  # everything outside the function, the DLL and RET is a callee
        self.calls: list[tuple] = []
        self.ret: dict[int, int] = {}
        self.effects: dict = {}  # callee -> fn(machine, regs, args): out-parameters etc.
        self.default_ret: dict[int, int] = {}
        self.spawned = 0
        self.fn_range = (0, 0)
        self.put(self.exports["g_rs7_native_tuner"], 1)
        # Rs7Tu8_OnSelect logs / reads the detector: a plain `ret` here (cdecl, its own tests in C++).
        uc.mem_write(self.exports["Rs7Tu8_OnSelect"], b"\xc3")

    def _load_dll(self):
        base, size, blobs, exports = dll_image()
        self.dll_lo, self.dll_hi = base, base + size
        self.uc.mem_map(base, size, UC_PROT_ALL)
        for a, d in blobs:
            self.uc.mem_write(a, d)
        return dict(exports)

    def _zero_page(self, uc, access, address, size, value, user):
        for page in {address & ~0xFFF, (address + max(size, 1) - 1) & ~0xFFF}:
            try:
                uc.mem_map(page, 0x1000, UC_PROT_ALL)
            except Exception:
                continue
            _, secs = image()
            for b, d in secs:
                if b <= page + 0xFFF and page < b + len(d):
                    lo = max(page, b)
                    hi = min(page + 0x1000, b + len(d))
                    uc.mem_write(lo, d[lo - b : hi - b])
        return True

    def _code(self, uc, address, size, user):
        lo, hi = self.fn_range
        if lo <= address < hi or self.dll_lo <= address < self.dll_hi or address == RET:
            return
        esp = uc.reg_read(UC_X86_REG_ESP)
        ret, *args = struct.unpack("<5I", uc.mem_read(esp, 20))
        regs = tuple(
            uc.reg_read(r)
            for r in (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI)
        )
        pops = FAKE_POPS.get(address, cleanup(address) if address not in FAKE_POPS else 0)
        n = pops // 4 or caller_pops(ret)
        self.calls.append((address, regs, tuple(args), n))
        uc.reg_write(UC_X86_REG_EAX, self.ret.get(address, 0))
        if address in self.effects:
            self.effects[address](self, regs, args)
        uc.reg_write(UC_X86_REG_ESP, esp + 4 + pops)
        uc.reg_write(UC_X86_REG_EIP, ret)

    # memory
    def read(self, a: int, n: int) -> bytes:
        """Game image / scratch bytes, mapping the pages on first use (as the emulated code would)."""
        for page in range(a & ~0xFFF, a + n, 0x1000):
            self._zero_page(self.uc, 0, page, 1, 0, None)
        return bytes(self.uc.mem_read(a, n))

    def put(self, a: int, v: int, fmt: str = "<I"):
        self.uc.mem_write(a, struct.pack(fmt, v & 0xFFFFFFFF if fmt in ("<I",) else v))

    def get(self, a: int, fmt: str = "<I") -> int:
        return struct.unpack(fmt, self.uc.mem_read(a, struct.calcsize(fmt)))[0]

    def session(self, n: int, fields: dict[int, int], arrays: list[bytes], lefty: int = 0, stock_count: int = 6):
        """A session with the per-string arrays `arrays` (six stock + two extra entries each), its
        string count n, the guitar/info chain and the given dword fields."""
        uc = self.uc
        uc.mem_write(SESSION, b"\0" * 0x380)
        self.put(SESSION + 0x2C, GUITAR)
        self.put(GUITAR + 0x24, INFO)
        self.put(GUITAR + 0x0C, GS)
        self.put(GUITAR + 0x10, WORLD)
        self.put(GUITAR + 0x18, WORLD)
        uc.mem_write(INFO + 8, bytes([lefty, 0, 0, stock_count]))
        for off, v in fields.items():
            self.put(SESSION + off, v)
        for (old, new, size), data in zip(ARRAYS, arrays):
            full = data[: size * 8 // 6]
            if self.native:
                uc.mem_write(SESSION + old, bytes([0xA5]) * size)  # poison: the copy must not read it
                uc.mem_write(SESSION + new, full)
            else:
                uc.mem_write(SESSION + old, full[:size])
        if self.native:
            self.put(SESSION + 0x278, 0x38545552)
            uc.mem_write(SESSION + COUNT, bytes([n, 1]))
            uc.mem_write(SESSION + ORDER, order(n))
            # Register the session in the DLL (TU8INIT's C entry; native_tuner=1).
            self.call_dll("Rs7Tuner8_InitSession", [SESSION], keep_tail=True)
            uc.mem_write(SESSION + COUNT, bytes([n, 1]))
            uc.mem_write(SESSION + ORDER, order(n))

    def call_dll(self, name: str, args: list[int], keep_tail: bool = False):
        saved = bytes(self.uc.mem_read(SESSION + 0x278, 0x108)) if keep_tail else None
        esp = STACK - 0x100
        self.uc.mem_write(esp, struct.pack("<I", RET) + b"".join(struct.pack("<I", a) for a in args))
        self.uc.reg_write(UC_X86_REG_ESP, esp)
        self.fn_range = (0, 0xFFFFFFFF)
        self.uc.emu_start(self.exports[name], RET, count=100000)
        if saved is not None:
            self.uc.mem_write(SESSION + 0x278, saved)

    def run(self, fid: str, regs: dict[str, int], stack: list[int] = (), ret: dict[int, int] = None, count=200000):
        va, n, code, rels = specs()[fid]
        self.ret = dict(self.default_ret)
        self.ret.update(ret or {})
        self.calls = []
        if self.native:
            patched = bytearray(code)
            ordered = list(specs())
            for off, t, ab in rels:
                if t >= SYN + 0x40:  # a call into another copy: its stock function (recorded)
                    t = specs()[ordered[t - SYN - 0x40]][0]
                elif t >= SYN:
                    t = self.exports[SYMS[t - SYN]]
                if ab:
                    patched[off : off + 4] = struct.pack("<I", t)
                else:
                    patched[off : off + 4] = struct.pack("<i", t - (COPY + off + 4))
            self.uc.mem_write(COPY, bytes(patched))
            entry = COPY
            self.fn_range = (COPY, COPY + len(code))
        else:
            entry = va
            self.fn_range = (va, va + n)
        esp = STACK - 0x400
        self.uc.mem_write(esp, struct.pack("<I", RET) + b"".join(struct.pack("<I", a) for a in stack))
        for r in REGS.values():
            self.uc.reg_write(r, 0)
        for k, v in regs.items():
            self.uc.reg_write(REGS[k], v)
        self.uc.reg_write(UC_X86_REG_ESP, esp)
        self.uc.reg_write(UC_X86_REG_EBP, STACK - 0x200)
        self.uc.emu_start(entry, RET, count=count)
        assert self.uc.reg_read(UC_X86_REG_EIP) == RET, f"{fid} stopped at {self.uc.reg_read(UC_X86_REG_EIP):#x}"
        return self.calls

    def arrays(self) -> list[bytes]:
        """The per-string arrays as the function left them (stock: +38h..; native: the tail)."""
        out = []
        for old, new, size in ARRAYS:
            out.append(
                bytes(self.uc.mem_read(SESSION + (new if self.native else old), size * 8 // 6 if self.native else size))
            )
        return out

    def fields(self) -> bytes:
        """The session's stock prefix without the six arrays."""
        b = bytearray(self.uc.mem_read(SESSION, 0x278))
        for old, _, size in ARRAYS:
            b[old : old + size] = b"\0" * size
        return bytes(b)


RET_FN = 0x5B0A90  # a plain `ret` in game code: fake vtable slots point here (recorded calls)
RET4_FN = 0x23000100  # a fake thiscall method with one argument (vtable slot 58h: node by name)
FAKE_POPS = {RET4_FN: 4}
ENTITY = 0x20200000  # fake entities: [e] = vtable (every slot RET_FN)
VTABLE = 0x201FF000


def spawn_effect(m, regs, args):
    """0x498AC0 (entity spawn, EDX = &out): a new fake entity."""
    e = ENTITY + 0x100 * (m.spawned + 1)
    m.spawned += 1
    m.put(e, VTABLE)
    m.put(regs[2], e)


def with_world(m, n_gs: int, pegs: int, n_strings: int = 8):
    """GuitarStrings (stock main[6] at +4, native main[8] at +40h, owner chain), the native InstrumentInfo
    counts, a PegSet with `pegs` taken slots (locator entities), fake entity vtables."""
    for k in range(0, 0x80, 4):
        m.put(VTABLE + k, RET4_FN if k == 0x58 else RET_FN)
    m.put(GS, OWNER)
    m.put(OWNER + 0x24, INFO)
    for s in range(8):
        child = 0x20300000 + 0x200 * s
        m.put(child + 4, 1)
        if s < 6:
            m.put(GS + 4 + 4 * s, child)
        m.put(GS + 0x40 + 4 * s, child)
    m.uc.mem_write(INFO + 0x40, bytes([n_gs, n_gs]))
    head = 0x20400000
    pegset = 0x20410000
    m.put(GUITAR + 0x18, head)
    m.put(head + 0x0C, pegset)
    m.uc.mem_write(pegset + 4, struct.pack("<H", pegs))
    world = 0x20430000  # World_GetRenderWorld 0x411130: a world with a scene (+10Ch)
    m.put(world + 0x10C, 1)
    m.default_ret[0x411130] = world
    for s in range(pegs):
        slot = pegset + 0xC + 0x28 * s
        m.uc.mem_write(slot, b"\x01")
        ent = 0x20420000 + 0x400 * s
        m.put(slot + 4, ent)
        m.put(ent, VTABLE)
        m.uc.mem_write(ent + 0xD8, b"\x01")  # its scene node at +0DCh (the locator search root)
        m.put(ent + 0xDC, ent + 0x200)
        m.put(ent + 0x200, VTABLE)


def order(n: int) -> bytes:
    if n <= 6:
        return bytes(range(8))
    return bytes(list(range(n - 1, 5, -1)) + list(range(6)) + list(range(n, 8)))


def arrays_for(open_midi: list[int], offs: list[int], seed: int = 1) -> list[bytes]:
    """Eight entries per array: targets, offsets, peaks, letter pairs, picks, pick states."""

    def ints(v):
        return b"".join(struct.pack("<i", x) for x in v)

    letters = b"".join(struct.pack("<II", 0x30000000 + 0x100 * s + 1, 0x30000000 + 0x100 * s + 2) for s in range(8))
    picks = b"".join(struct.pack("<I", 0x31000000 + 0x100 * s) for s in range(8))
    peaks = b"".join(struct.pack("<f", 0.125 * (s + seed)) for s in range(8))
    return [ints(open_midi), ints(offs), peaks, letters, picks, ints([-2] * 8)]


def normalize(calls, native: bool):
    """Tail addresses -> the stock array addresses, so both runs compare equal."""

    def fix(v):
        for old, new, size in ARRAYS:
            if native and SESSION + new <= v < SESSION + new + size * 8 // 6:
                return SESSION + old + (v - SESSION - new)
        return v

    out = []
    for target, regs, args, n in calls:
        out.append((target, tuple(fix(r) for r in regs), tuple(fix(a) for a in args), n))
    return out


def same_calls(stock, native):
    """Same callees, registers and arguments (as many as the stock call site passes)."""
    assert [c[0] for c in stock] == [c[0] for c in native], "callees differ"
    for a, b in zip(stock, native):
        assert a[1] == b[1], f"registers at the call of {a[0]:#x} differ: {a[1]} / {b[1]}"
        n = min(a[3], 4)
        for x, y in zip(a[2][:n], b[2][:n]):
            # Uninitialised bytes of by-value objects can hold a stale return address (stock code /
            # the copy): not a difference.
            if TEXT_LO <= x < TEXT_HI and COPY <= y < COPY + 0x10000:
                continue
            assert x == y, f"arguments of {a[0]:#x} differ: {a[2][:n]} / {b[2][:n]}"
