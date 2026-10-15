"""Native N-slot GuitarStrings copies (tools/gen_fncopy.py, core/gsnative.h).

The generated header must match the unpacked exe; a copy must behave like the stock function on a
native GuitarStrings with six strings and drive string 6 too when the count is seven (Unicorn).
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

import pytest
from unicorn import UC_ARCH_X86, UC_MODE_32, UC_PROT_ALL, Uc
from unicorn.x86_const import UC_X86_REG_EDI, UC_X86_REG_ESP

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import gen_fncopy  # noqa: E402

pytestmark = pytest.mark.skipif(not gen_fncopy.EXE.exists(), reason="needs original/Rocksmith2014.unpacked.exe")


def test_generated_header_is_current():
    text, base = gen_fncopy.load_text()
    results = []
    for fn in gen_fncopy.FNS:
        code, edits, rel32, pro, insns = gen_fncopy.build(fn, text, base)
        results.append((fn, code, edits, rel32, pro, insns))
    assert gen_fncopy.render(results) == gen_fncopy.OUT.read_text(encoding="utf-8")


def test_every_count_byte_of_a_counted_copy_is_moved():
    text, base = gen_fncopy.load_text()
    for fn in gen_fncopy.FNS:
        if not fn.count:
            continue
        code, edits, _, _, insns = gen_fncopy.build(fn, text, base)
        moved = {e.off for e in edits if e.write}
        for i in insns:
            for op in i.operands:
                if op.type == 3 and op.size == 1 and op.mem.disp == 0xB and op.mem.index == 0 and op.mem.base:
                    assert i.address - fn.start in moved, f"{fn.id} {i.address:08X} not moved"


# ---- ResetState 0x59AC20 (EDI = GuitarStrings): stock vs copy ----
GS, INFO, OWNER, KIDS = 0x10000, 0x11000, 0x12000, 0x20000
CODE_STOCK, CODE_COPY, STACK, RET = 0x00400000, 0x30000000, 0x31000000, 0x32000000


def build_copy(fid: str) -> tuple[gen_fncopy.Fn, bytes]:
    text, base = gen_fncopy.load_text()
    fn = next(f for f in gen_fncopy.FNS if f.id == fid)
    code, edits, rel32, _, _ = gen_fncopy.build(fn, text, base)
    buf = bytearray(code)
    for e in edits:
        assert not e.sym, "this test runs copies without DLL calls"
        buf[e.off : e.off + len(e.write)] = e.write
    for off, tgt in rel32:
        buf[off : off + 4] = struct.pack("<i", tgt - (CODE_COPY + off + 4))
    return fn, bytes(buf)


def run_reset(native: bool, count: int, loaded: int):
    text, base = gen_fncopy.load_text()
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    for a, n in (
        (GS, 0x1000),
        (INFO, 0x1000),
        (OWNER, 0x1000),
        (KIDS, 0x10000),
        (STACK - 0x10000, 0x20000),
        (RET, 0x1000),
    ):
        uc.mem_map(a, n, UC_PROT_ALL)
    uc.mem_write(RET, b"\xf4")  # hlt
    fn, copy = build_copy("GSRS")
    if native:
        uc.mem_map(CODE_COPY, 0x10000, UC_PROT_ALL)
        uc.mem_write(CODE_COPY, copy)
        entry = CODE_COPY
    else:
        uc.mem_map(CODE_STOCK, (len(text) + 0x2000) & ~0xFFF, UC_PROT_ALL)
        uc.mem_write(base, text)
        entry = fn.start
    kid = [KIDS + 0x200 * i for i in range(16)]  # main 0..7, ghost 8..15; entity +4 = 0: no calls
    for k in kid:
        uc.mem_write(k, b"\x00" * 0x110)
        uc.mem_write(k + 0x70, b"\x01\x01")  # dimming on, dimmed
        uc.mem_write(k + 0x78, struct.pack("<f", 3.0))
        uc.mem_write(k + 0x6C, b"\x01")
    uc.mem_write(GS, struct.pack("<I", OWNER))
    uc.mem_write(OWNER + 0x24, struct.pack("<I", INFO))
    uc.mem_write(INFO + 0x0B, bytes([count]))
    for s in range(6):
        uc.mem_write(GS + 4 + 4 * s, struct.pack("<I", kid[s]))
        uc.mem_write(GS + 0x1C + 4 * s, struct.pack("<I", kid[8 + s]))
    if native:
        for s in range(8):
            uc.mem_write(GS + 0x40 + 4 * s, struct.pack("<I", kid[s]))
            uc.mem_write(GS + 0x60 + 4 * s, struct.pack("<I", kid[8 + s]))
        uc.mem_write(INFO + 0x40, bytes([count, loaded]))
    uc.mem_write(STACK, struct.pack("<I", RET))
    uc.reg_write(UC_X86_REG_ESP, STACK)
    uc.reg_write(UC_X86_REG_EDI, GS)
    uc.emu_start(entry, RET, count=100000)
    return [bytes(uc.mem_read(k + 0x6C, 0x10)) for k in kid]


def test_reset_copy_equals_stock_for_six_strings():
    stock = run_reset(False, 6, 6)
    copy = run_reset(True, 6, 6)
    assert copy == stock
    assert stock[6] == run_reset(False, 6, 6)[6] and stock[6][0x05] == 1  # string 6 untouched (still dimmed)


def test_reset_copy_resets_string_six_with_seven_loaded():
    copy = run_reset(True, 6, 7)
    stock = run_reset(False, 6, 6)
    assert copy[:6] == stock[:6] and copy[8:14] == stock[8:14]
    assert copy[6] == stock[0]  # main string 6 reset like string 0
    assert copy[14] == stock[8]  # its ghost like ghost 0
    assert copy[7] != stock[0]  # string 7 (not loaded) untouched
