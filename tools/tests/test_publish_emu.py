"""Unicorn executes game code, DLL helpers, entry guards and the generated native copies.

No live process or game folder. Fake CRT block sizes exercise legacy-allocation fallbacks.
"""

import gc
import struct
from functools import lru_cache
from pathlib import Path

import pefile
import pytest
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_PROT_ALL, UC_HOOK_CODE, UC_HOOK_MEM_READ
from unicorn.x86_const import *

import gen_fncopy as fc
import gen_publish as pub
import sitetable

DLL = pub.REPO / "bin/Release/dinput8.dll"
pytestmark = pytest.mark.skipif(not DLL.exists() or not fc.EXE.exists(), reason="needs offline exe and built DLL")
DATA, STACK, RET, COPIES, FAKE = 0x20000000, 0x21000000, 0x22000000, 0x23000000, 0x24000000
TRACKER, INPUT, MAT, DET, REC = DATA, DATA + 0x50000, DATA + 0x51000, DATA + 0x52000, DATA + 0x55000
GRID, GUITAR, INFO = DATA + 0x60000, DATA + 0x70000, DATA + 0x71000


@lru_cache(maxsize=1)
def sections():
    pe = pefile.PE(str(fc.EXE), fast_load=True)
    return [
        (pe.OPTIONAL_HEADER.ImageBase + s.VirtualAddress, bytes(s.get_data()[: s.Misc_VirtualSize]))
        for s in pe.sections
        if s.Name.rstrip(b"\0") in (b".text", b".rdata", b".data")
    ]


class Machine:
    def __init__(self):
        gc.collect()
        self.uc = u = Uc(UC_ARCH_X86, UC_MODE_32)
        # Map only the code under test; keep this emulator independent of the full memory dump.
        windows = [(f.start & ~0xFFF, (f.end + 0xFFF) & ~0xFFF) for f in pub.FNS]
        windows += [(0x40E000, 0x40F000), (0xF5A000, 0xF5B000)]
        for va, data in sections():
            spans = windows if va == 0x401000 else [(va, va + len(data))]
            for lo, hi in spans:
                u.mem_map(lo, (hi - lo + 0xFFF) & ~0xFFF, UC_PROT_ALL)
                u.mem_write(lo, data[lo - va : hi - va])
        for va, n in ((DATA, 0x100000), (STACK, 0x20000), (RET, 0x1000), (COPIES, 0x10000), (FAKE, 0x1000)):
            u.mem_map(va, n, UC_PROT_ALL)
        pe = pefile.PE(str(DLL))
        base = pe.OPTIONAL_HEADER.ImageBase
        u.mem_map(base, (pe.OPTIONAL_HEADER.SizeOfImage + 0xFFF) & ~0xFFF)
        u.mem_write(base, pe.header)
        for s in pe.sections:
            u.mem_write(base + s.VirtualAddress, s.get_data())
        self.exports = {s.name.decode(): base + s.address for s in pe.DIRECTORY_ENTRY_EXPORT.symbols if s.name}
        self.sizes = {MAT: 0x6C, DET: 0x13E0}
        self.reads = []
        self.u32(self.exports["g_rs7_publish_ports"], 1)
        self.u32(self.exports["g_rs7_fn_msize"], FAKE)
        u.mem_write(FAKE, b"\xc3")
        u.mem_write(FAKE + 0x100, bytes.fromhex("DB E3"))  # FNINIT between independent calls
        u.hook_add(UC_HOOK_CODE, self.msize, begin=FAKE, end=FAKE)
        u.hook_add(UC_HOOK_MEM_READ, lambda uc, access, a, size, v, d: self.reads.append((a, size)))
        # Controlled external abs(float) dependency, preserving x87 ABI (same in stock and native).
        u.mem_write(0x40E570, bytes.fromhex("D9 44 24 04 D9 E1 C3"))
        u.mem_write(0xF5AB08, b"\xc3")
        self.copies = {}
        for i, (fn, code, edits, rels, pro, insns) in enumerate(pub.results()):
            va = COPIES + i * 0x1000
            out = bytearray(code)
            for e in edits:
                wr = (
                    e.write
                    if not e.sym
                    else b"\xe8"
                    + struct.pack("<i", self.exports[e.sym] - (va + e.off + 5))
                    + b"\x90" * (len(e.expect) - 5)
                )
                out[e.off : e.off + len(e.expect)] = wr
            for off, target in rels:
                out[off : off + 4] = struct.pack("<i", target - (va + off + 4))
            u.mem_write(va, bytes(out))
            self.copies[fn.id] = va
            tramp = va + 0xF00
            u.mem_write(tramp, code[:pro] + b"\xe9" + struct.pack("<i", fn.start + pro - (tramp + pro + 5)))
            self.u32(self.exports["g_rs7_pubcopy_" + fn.id], va)
            self.u32(self.exports["g_rs7_pubtramp_" + fn.id], tramp)
        self.u32(TRACKER + 0x4C19C, INPUT)
        self.u32(INPUT + 8, MAT)
        self.u32(MAT + 4, DET)
        self.u32(MAT + 0x68, 0x38503752)
        self.u32(DET + 0x13DC, 0x38503752)
        self.u32(DET + 0x13D8, 6)
        self.u32(DET + 0x11EC, 1)

    def msize(self, u, a, size, d):
        sp = u.reg_read(UC_X86_REG_ESP)
        p = self.get32(sp + 4)
        u.reg_write(UC_X86_REG_EAX, self.sizes.get(p, 0))

    def u32(self, a, v):
        self.uc.mem_write(a, struct.pack("<I", v & 0xFFFFFFFF))

    def get32(self, a):
        return struct.unpack("<I", bytes(self.uc.mem_read(a, 4)))[0]

    def call(self, va, args=(), regs=None, until=RET):
        self.uc.emu_start(FAKE + 0x100, FAKE + 0x102)
        sp = STACK + 0x18000
        self.uc.mem_write(sp, struct.pack("<I", RET) + b"".join(struct.pack("<I", x & 0xFFFFFFFF) for x in args))
        self.uc.reg_write(UC_X86_REG_ESP, sp)
        for r in (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESI, UC_X86_REG_EDI):
            self.uc.reg_write(r, 0)
        self.uc.reg_write(UC_X86_REG_EBP, STACK + 0x10000)
        self.uc.reg_write(UC_X86_REG_EFLAGS, 0x202)
        for r, v in (regs or {}).items():
            self.uc.reg_write(r, v)
        self.reads.clear()
        self.uc.emu_start(va, until, count=100000)
        assert self.uc.reg_read(UC_X86_REG_EIP) == until


@pytest.fixture(scope="module")
def m():
    return Machine()


def tuning(m, t, n):
    b = struct.pack("<8h", *t)
    m.uc.mem_write(MAT + 0x44, b[:12])
    m.uc.mem_write(MAT + 0x58, b)
    m.u32(DET + 0x13D8, n)


def classify(m, native, original, fret, midi, initial, t, n=6):
    tuning(m, t, n)
    bp = STACK + 0x10000
    m.uc.mem_write(REC, b"\x00" * 0xC2)
    m.uc.mem_write(REC + 0xC0, bytes((original, fret)))
    m.u32(bp - 0x38, initial)
    m.u32(bp - 0x40, midi)
    m.u32(bp - 0x48, TRACKER)
    copy = m.copies["PUBCL"] if native else 0x8750C0
    m.call(
        copy + 0x16C,
        regs={UC_X86_REG_ESI: REC, UC_X86_REG_EDI: TRACKER, UC_X86_REG_EBX: 0x12E8050, UC_X86_REG_EBP: bp},
        until=copy + 0x312,
    )
    return m.get32(bp - 0x38), bytes(m.uc.mem_read(REC + 0x14, 8))


@pytest.mark.parametrize("t", [[0] * 8, [-2, 1, 0, 3, -1, -3, 0, 0]])
def test_classification_six_equals_stock_three_per_pass_body(m, t):
    for original in range(6):
        for fret in (0, 5, 24):
            for midi in (-1, *range(20, 94)):
                for initial in (3, 7):
                    assert classify(m, True, original, fret, midi, initial, t) == classify(
                        m, False, original, fret, midi, initial, t
                    )


@pytest.mark.parametrize("n", [7, 8])
def test_classification_stops_exactly_at_active_n(m, n):
    t = [0] * 8
    t[n - 1] = -3
    midi = (35 if n == 7 else 30) - 3 + 5
    got = classify(m, True, 0, 5, midi, 7, t, n)
    assert got == (2, struct.pack("<II", 5, n - 1))
    accessed = {a for a, size in m.reads if MAT + 0x58 <= a < MAT + 0x68}
    assert MAT + 0x58 + 2 * (n - 1) in accessed
    assert all(a < MAT + 0x58 + 2 * n for a in accessed)


@pytest.mark.parametrize("pitch", [20, 45, 80])
@pytest.mark.parametrize("offset", [-2, 0, 3])
def test_tolerance_copy_and_guard_preserve_six_string_formula_and_stack(m, pitch, offset):
    tuning(m, [offset] + [0] * 7, 6)
    m.uc.mem_write(DET + 0x134C, struct.pack("<h", 40 + offset))
    m.uc.mem_write(DET + 0x13C8, struct.pack("<h", 40 + offset))
    m.uc.mem_write(MAT + 0x50, struct.pack("<f", 0.45))
    out = []
    for entry in (0x4E58E0, m.exports["Rs7_Stub_PUBTO"]):
        m.call(entry, (pitch,), {UC_X86_REG_ESI: MAT})
        out.append((m.uc.reg_read(UC_X86_REG_FP0), m.uc.reg_read(UC_X86_REG_FPSW), m.uc.reg_read(UC_X86_REG_ESP)))
    assert out[0] == out[1]


@pytest.mark.parametrize("size,magic", [(0x58, 0x38503752), (0x6C, 0)])
def test_legacy_matcher_uses_stock_trampoline_without_tail_read(m, size, magic):
    m.sizes[MAT] = size
    m.u32(MAT + 0x68, magic)
    try:
        m.call(m.exports["Rs7Publish_Target"], (1, MAT))
        assert m.uc.reg_read(UC_X86_REG_EAX) == m.get32(m.exports["g_rs7_pubtramp_PUBTO"])
        if size == 0x58:
            assert not any(MAT + 0x58 <= a < MAT + 0x6C for a, s in m.reads)
    finally:
        m.sizes[MAT] = 0x6C
        m.u32(MAT + 0x68, 0x38503752)


@pytest.mark.parametrize("n", [4, 6, 7, 8])
def test_wire_update_visits_only_active_records(m, n):
    m.u32(GRID, GUITAR)
    m.u32(GUITAR + 0x24, INFO)
    m.uc.mem_write(INFO + 0xB, bytes((n,)))
    row, cells = GRID + 0x72A0, GRID + 4
    for s in range(8):
        rec = row + 0xA0 + 0x30 * s
        m.uc.mem_write(rec, b"\x00" * 0x30)
        m.uc.mem_write(rec, b"\x01")
        for off in (8, 12, 28, 32):
            m.u32(rec + off, 1)
    m.call(m.exports["Rs7_Stub_PUBWU"], (cells,), {UC_X86_REG_EAX: row, UC_X86_REG_EDI: GRID})
    for s in range(8):
        assert bytes(m.uc.mem_read(row + 0xA0 + 0x30 * s, 1)) == bytes((0 if s < n else 1,))


def test_generated_publish_copies_current():
    assert pub.OUT.read_text() == pub.render()


def test_classification_keeps_stock_signed_abs_wrap(m):
    # Stock CDQ/XOR/SUB leaves abs(INT32_MIN) negative, so its signed JLE takes near-match.
    midi = -2147483648 + 45 + 5  # delta from candidate string 1 is INT32_MIN
    assert classify(m, True, 0, 5, midi, 3, [0] * 8) == classify(m, False, 0, 5, midi, 3, [0] * 8)


@pytest.mark.parametrize("sid", ["PUBNN1", "PUBNN3", "PUBNN4"])
def test_nextgrid_capacity_guards_execute_cmp_and_reject_eight(m, sid):
    site = next(s for s in sitetable.load(pub.REPO / "sites.json")[1] if s.id == sid)
    at = FAKE + 0x500 + ("PUBNN1", "PUBNN3", "PUBNN4").index(sid) * 0x100
    m.uc.mem_write(at, site.write)
    bp = STACK + 0x10000
    for string in range(10):
        m.uc.mem_write(bp - 6, bytes((string,)))
        m.call(at, regs={UC_X86_REG_EBP: bp, UC_X86_REG_ECX: 0xCAFE0000 + string}, until=at + len(site.write))
        flags = m.uc.reg_read(UC_X86_REG_EFLAGS)
        assert bool(flags & 1) == (string < 8)  # original JC/JAE consumes CF
        assert bool(flags & 0x40) == (string == 8)
