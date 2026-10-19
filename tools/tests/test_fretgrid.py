"""Native capacity-8 FretGrid (tools/gen_fretgrid.py, features/fretgrid/fretgrid.h): the patched game code against the stock
code in Unicorn, 6 strings. The stock functions run on a stock-layout grid, the patched ones (the generated
edits applied to the same exe bytes) on a native-layout grid; every value must agree cell by cell.
"""

from __future__ import annotations

import gc
import json
import random
import struct

import capstone
import pytest
from unicorn import UC_ARCH_X86, UC_HOOK_CODE, UC_MODE_32, UC_PROT_ALL, Uc
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP

import game_files
import gen_fncopy
import gen_fretgrid as fg
from emu_common import CRT_MEMSET, crt_memset, exe_sections, needs_exe

pytestmark = needs_exe

ROW_S, ROW_N = fg.CELL, fg.CELL_N
WIRE_S, WIRE_N = fg.WIRE, fg.WIRE_N
BASE_S, BASE_N = fg.BASE, fg.BASE_N
GRID, GUITAR, INFO, OUT = 0x10000000, 0x11000000, 0x11001000, 0x11002000
STACK, RET = 0x12000000, 0x13000000
CELL = 0x4C


def patched_edits():
    text, base = gen_fncopy.load_text()
    return fg.all_edits(text, base)


def machine(native: bool, hooks=None):
    gc.collect()  # each Uc commits tens of MB: free the previous ones first
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    for va, data in exe_sections():
        size = (len(data) + 0xFFF) & ~0xFFF
        uc.mem_map(va, size, UC_PROT_ALL)
        uc.mem_write(va, data)
    if native:
        for va, exp, wr, _, _ in patched_edits():
            assert bytes(uc.mem_read(va, len(exp))) == exp
            uc.mem_write(va, wr)
    for a, n in ((GRID, 0x20000), (GUITAR, 0x4000), (STACK - 0x10000, 0x20000), (RET, 0x1000)):
        uc.mem_map(a, n, UC_PROT_ALL)
    uc.mem_write(RET, b"\xf4")
    uc.hook_add(UC_HOOK_CODE, crt_memset, begin=CRT_MEMSET, end=CRT_MEMSET)
    return uc


def call(uc, entry, args=(), ecx=0, eax=0, limit=2_000_000, until=None):
    """cdecl-style frame: [esp] = RET, then the stack arguments."""
    sp = STACK
    uc.mem_write(sp, struct.pack("<I", RET) + b"".join(struct.pack("<I", a) for a in args))
    uc.reg_write(UC_X86_REG_ESP, sp)
    uc.reg_write(UC_X86_REG_ECX, ecx)
    uc.reg_write(UC_X86_REG_EAX, eax)
    uc.emu_start(entry, RET if until is None else until, count=limit)


def cell_off(native: bool, f: int, s: int) -> int:
    return 4 + f * (ROW_N if native else ROW_S) + 4 + s * CELL


def test_generated_sites_are_current():
    doc = json.loads((fg.REPO / "sites.json").read_text(encoding="utf-8"))
    have = {s["id"]: s for s in doc["sites"] if (s["id"].startswith("FGR") or s["id"] in ("FGHK", "FGCT"))}
    want = {s["id"]: s for s in fg.build_sites() + fg.build_stub_sites()}
    assert have == want
    inc = fg.INC.read_text(encoding="utf-8")
    assert inc.count("RS7_FGR(") == len(fg.build_sites())


def test_no_stock_layout_constant_is_left_in_the_fretgrid_code():
    """Every 1CC / 1C0 / 2EC0 / 72A0 operand of the listed functions is edited, except V2's (the stub)."""
    ledger = game_files.RESEARCH / "7-string/nslot/evidence/fretgrid-tables/functions.tsv"
    if not ledger.exists():
        pytest.skip("needs the maintainer's local function ledger (not in this repository)")
    text, base = gen_fncopy.load_text()
    edited = {va for va, *_ in fg.all_edits(text, base)}
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    vals = {fg.CELL, fg.WIRE, fg.BASE, fg.SIZE}
    left = []
    for line in ledger.read_text().split("\n"):
        if not line:
            continue
        a, b, name = line.split("\t")
        a, b = int(a, 16), int(b, 16)
        if name in ("Rnd_ColorMgr_Ctor", "FUN_00518590"):  # ColourManager: unrelated 1C0 / 1CC fields
            continue
        for i in md.disasm(text[a - base : b - base], a):
            for op in i.operands:
                v = (
                    op.imm
                    if op.type == capstone.x86.X86_OP_IMM
                    else op.mem.disp
                    if op.type == capstone.x86.X86_OP_MEM
                    else None
                )
                if v in vals:
                    off = i.imm_offset if op.type == capstone.x86.X86_OP_IMM else i.disp_offset
                    if i.address + off not in edited:
                        left.append(i.address)
    assert left == [0x7A69C1]


def make_guitar(uc, strings, frets):
    uc.mem_write(GUITAR + 0x24, struct.pack("<I", INFO))
    uc.mem_write(INFO + 0xB, bytes([strings, frets]))
    uc.mem_write(GUITAR + 0x2C, struct.pack("<3f", 1.5, -2.25, 100.0))


def fill_local(uc, native, frets, seed=7):
    rnd = random.Random(seed)
    local = {}
    for f in range(frets):
        for s in range(6):
            pts = [round(rnd.uniform(-50, 50), 3) for _ in range(9)]
            local[f, s] = pts
            c = GRID + cell_off(native, f, s)
            uc.mem_write(c, b"\x01")
            uc.mem_write(c + 4, struct.pack("<9f", *pts))
    return local


def test_apply_offset_matches_stock_at_six_strings():
    frets = 9
    worlds = []
    for native in (False, True):
        uc = machine(native)
        make_guitar(uc, 6, frets)
        local = fill_local(uc, native, frets)
        before = bytes(uc.mem_read(GRID, 0x4000))
        call(uc, 0x58FCF0, (GRID,), ecx=GUITAR)
        w = {}
        for f in range(frets):
            for s in range(6):
                c = GRID + cell_off(native, f, s)
                w[f, s] = struct.unpack("<9f", bytes(uc.mem_read(c + 0x28, 36)))
                assert struct.unpack("<9f", bytes(uc.mem_read(c + 4, 36))) == struct.unpack(
                    "<9f", struct.pack("<9f", *local[f, s])
                )
        after = bytes(uc.mem_read(GRID, 0x4000))
        if native:
            # the world floats of cells 6/7 and everything between the rows stay untouched
            for f in range(frets):
                for s in (6, 7):
                    o = cell_off(True, f, s)
                    assert after[o : o + CELL] == before[o : o + CELL]
        worlds.append(w)
    stock, nat = worlds
    assert stock == nat
    ox, oy, oz = 1.5, -2.25, 100.0
    # and the rule itself: world = local + the guitar offset
    uc = machine(True)
    make_guitar(uc, 6, 2)
    loc = fill_local(uc, True, 2)
    call(uc, 0x58FCF0, (GRID,), ecx=GUITAR)
    got = struct.unpack("<9f", bytes(uc.mem_read(GRID + cell_off(True, 1, 3) + 0x28, 36)))
    exp = [loc[1, 3][i] + (ox, oy, oz)[i % 3] for i in range(9)]
    assert all(abs(a - b) < 1e-3 for a, b in zip(got, exp))


def test_apply_offset_eight_strings_fills_every_native_cell():
    """Capacity check: the patched loop, run over 8 strings, writes cells 6 and 7 (the later publication step)."""
    uc = machine(True)
    make_guitar(uc, 8, 4)
    for f in range(4):
        for s in range(8):
            c = GRID + cell_off(True, f, s)
            uc.mem_write(c + 4, struct.pack("<9f", *[float(100 * f + 10 * s + i) for i in range(9)]))
    call(uc, 0x58FCF0, (GRID,), ecx=GUITAR)
    for f in range(4):
        for s in range(8):
            got = struct.unpack("<9f", bytes(uc.mem_read(GRID + cell_off(True, f, s) + 0x28, 36)))
            exp = [100 * f + 10 * s + i + (1.5, -2.25, 100.0)[i % 3] for i in range(9)]
            assert all(abs(a - b) < 1e-3 for a, b in zip(got, exp)), (f, s)


def test_get_cell_pos_matches_stock():
    frets = 26
    res = []
    for native in (False, True):
        uc = machine(native)
        make_guitar(uc, 6, frets)
        fill_local(uc, native, frets)
        call(uc, 0x58FCF0, (GRID,), ecx=GUITAR)
        out = {}
        for fret in range(0, 28):
            for s in range(6):
                for k in range(3):
                    uc.mem_write(OUT, b"\xaa" * 12)
                    call(uc, 0x58FE00, (GRID, k, s), ecx=OUT, eax=fret)
                    ok = uc.reg_read(UC_X86_REG_EAX) & 0xFF
                    out[fret, s, k] = (ok, bytes(uc.mem_read(OUT, 12)) if ok else b"")
        res.append(out)
    assert res[0] == res[1]
    assert res[1][1, 0, 0][0] == 1 and res[1][27, 0, 0][0] == 0


def run_ctor_and_dtor(native: bool):
    uc = machine(native)
    uc.mem_write(GRID, b"\x00" * 0xB000)
    counts = {"cell": [], "wire": []}
    uc.hook_add(
        UC_HOOK_CODE,
        lambda u, a, sz, d: counts["cell"].append(u.reg_read(UC_X86_REG_ECX)),
        begin=0x590680,
        end=0x590680,
    )
    uc.hook_add(
        UC_HOOK_CODE,
        lambda u, a, sz, d: counts["wire"].append(u.reg_read(UC_X86_REG_ECX)),
        begin=0x590730,
        end=0x590730,
    )
    dead = []
    uc.hook_add(UC_HOOK_CODE, lambda u, a, sz, d: dead.append(u.reg_read(UC_X86_REG_ECX)), begin=0x58E5C0, end=0x58E5C0)
    call(uc, 0x58F090, (GRID,), until=0x58F12E)  # cells and wire rows; the other sections need game singletons
    snap = bytes(uc.mem_read(GRID, 0xB000))
    call(uc, 0x592900, (GRID,))
    return snap, counts, dead


def test_constructor_and_reverse_destructor_cover_every_record():
    stock, cs, ds = run_ctor_and_dtor(False)
    nat, cn, dn = run_ctor_and_dtor(True)
    # cells: 26 rows x 6 vs x 8 constructed; strings 0..5 equal, 6/7 constructed like string 0
    assert len(cs["cell"]) == 26 * 6 and len(cn["cell"]) == 26 * 8
    assert sorted(cn["cell"]) == sorted(GRID + cell_off(True, f, s) for f in range(26) for s in range(8))
    assert len(cs["wire"]) == 26 * 6 and len(cn["wire"]) == 26 * 8
    assert sorted(cn["wire"]) == sorted(
        GRID + BASE_N + f * WIRE_N + 0xA0 + i * 0x30 for f in range(26) for i in range(8)
    )
    for f in range(26):
        so = 4 + f * ROW_S
        no = 4 + f * ROW_N
        assert stock[so] == nat[no] == 0  # row valid byte
        for s in range(6):
            assert stock[cell_off(False, f, s) :][:CELL] == nat[cell_off(True, f, s) :][:CELL]
        for s in (6, 7):
            assert nat[cell_off(True, f, s) :][:CELL] == nat[cell_off(True, f, 0) :][:CELL]
        sw, nw = BASE_S + f * WIRE_S, BASE_N + f * WIRE_N
        assert stock[sw : sw + 0xA0 + 6 * 0x30] == nat[nw : nw + 0xA0 + 6 * 0x30]  # prefix + six records
        assert nat[nw + 0xA0 + 6 * 0x30 : nw + WIRE_N] == nat[nw + 0xA0 : nw + 0xA0 + 0x60]
    assert stock[0x5C40:0x7298] == nat[0x5C40:0x7298]  # retained tail
    # the reverse destructor destroys every record once, rows 25..0
    assert len(ds) == 26 * 6 and len(dn) == 26 * 8
    assert sorted(dn) == sorted(GRID + BASE_N + f * WIRE_N + 0xA0 + i * 0x30 for f in range(26) for i in range(8))
    assert sorted(ds) == sorted(GRID + BASE_S + f * WIRE_S + 0xA0 + i * 0x30 for f in range(26) for i in range(6))
    assert dn[0] >= GRID + BASE_N + 25 * WIRE_N + 0xA0 and ds[0] >= GRID + BASE_S + 25 * WIRE_S + 0xA0  # last row first
    # the new cells end below the retained fretbar region, the new wires start behind the stock end
    assert max(c + CELL for c in cn["cell"]) <= GRID + 0x5C40
    assert min(cn["wire"]) - 0xA0 >= GRID + fg.SIZE


def test_fgr_sites_are_independent_of_the_other_sites():
    """The early pass (startup.cpp EarlyFretgrid) writes the FGR sites before the rest and keeps the resolve
    result of the unpatched code, so no FGR write may touch bytes another site writes."""
    text, base = gen_fncopy.load_text()
    sites = json.loads((fg.REPO / "sites.json").read_text(encoding="utf-8"))["sites"]
    spans = {"fgr": [], "other": []}
    for s in sites:
        pat = s["pattern"].split()
        if "??" in pat:
            continue
        i = text.find(bytes.fromhex("".join(pat)))
        if i < 0:
            continue
        a = base + i + (s["offset"] if isinstance(s["offset"], int) else int(s["offset"], 16))
        n = len(s["write"]) // 2 if s.get("kind") == "bytes" else int(s.get("length", 5))
        spans["fgr" if s["id"].startswith("FGR") else "other"].append((s["id"], a, a + n))
    assert len(spans["fgr"]) > 50
    for fid, fa, fb in spans["fgr"]:
        for oid, oa, ob in spans["other"]:
            if fid == "FGRB" and oid.startswith("PUB"):
                continue  # mutually exclusive: with native_publish the PUB* ports replace FGRB (Rs7_ShouldPatch)
            assert not (fa < ob and oa < fb), f"{fid} overlaps {oid}"
