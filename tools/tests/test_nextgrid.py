"""Native next-note grid, static and direct: generated sites, frame layout, Init / Set / Prune against stock."""

from __future__ import annotations

import json
import random
import struct

import capstone
import pytest

import gen_nextgrid as ng
from emu_common import SITES, needs_exe_and_dll
from nextgrid_harness import CELLS, GRID, NOTE_BASE, RET, ROW, Machine, cells_equal, f2u, game_text, note, u32s

pytestmark = needs_exe_and_dll


# --- static checks -----------------------------------------------------------------------------------------


def test_generated_sites_are_current():
    doc = json.loads(SITES.read_text(encoding="utf-8"))
    have = {s["id"]: s for s in doc["sites"] if s["id"].startswith("NG")}
    want = {s["id"]: s for s in ng.build_sites() + ng.build_stub_sites()}
    assert have == want
    inc = ng.INC.read_text(encoding="utf-8")
    assert inc.count("RS7_NGB(") == len(ng.build_sites())


def test_frame_displacements_of_update_are_all_accounted_for():
    """After the edits every ebp operand of Update outside the stub spans is a stack argument, the cookie, or one of
    the six new local / grid-base slots (ebp-1060h .. -1074h)."""
    text, base = game_text()
    patched = bytearray(text)
    for va, exp, wr, *_ in ng.all_edits(text, base):
        patched[va - base : va - base + len(wr)] = wr
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    seen = set()
    for i in md.disasm(bytes(patched[ng.UPDATE[0] - base : ng.UPDATE[1] - base + 16]), ng.UPDATE[0]):
        if i.address > ng.UPDATE[1]:
            break
        if ng.NG9_VA <= i.address < ng.NG9_VA + ng.NG9_LEN or i.address == ng.NGP_VA:
            continue
        for op in i.operands:
            if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_EBP:
                seen.add(op.mem.disp)
    assert {d for d in seen if d < 0} == {-0x1060, -0x1064, -0x1068, -0x106C, -0x1070, -0x1074, -4}
    assert {d for d in seen if d > 0} <= {8, 0xC}
    assert -0x1060 + ng.GRID_NATIVE == -4  # the grid ends at the cookie
    assert ng.FRAME_NATIVE == 0x1074 and ng.FRAME_NATIVE > 0x1000


def test_native_edits_leave_no_stock_time_constant_in_the_grid_functions():
    """Init / Prune: no 0A90h / 270h operand is left once the edits are applied, except the pointer block's clear."""
    text, base = game_text()
    edited = {va for va, *_ in ng.all_edits(text, base)}
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    left = []
    for a, b in ((0x7AA0B0, 0x7AA137), (0x7AA1B0, 0x7AA25D)):
        for i in md.disasm(text[a - base : b - base], a):
            for op in i.operands:
                v = (
                    op.imm
                    if op.type == capstone.x86.X86_OP_IMM
                    else op.mem.disp
                    if op.type == capstone.x86.X86_OP_MEM
                    else None
                )
                if v in (0xA90, 0x270) and not any(i.address <= e < i.address + i.size for e in edited):
                    left.append(i.address)
    assert left == [0x7AA0D5]  # `push 0A90h`: the pointer block (unchanged)


# --- Init ---------------------------------------------------------------------------------------------------


@pytest.mark.parametrize("strings,frets", [(6, 24), (6, 25), (6, 1), (4, 21)])
def test_init_matches_stock_and_fills_all_eight_rows(machines, strings, frets):
    stock, native = machines["stock"], machines["native"]
    stock.init(strings, frets)
    native.init(strings, frets)
    assert stock.read(GRID, 0xA90) == native.read(GRID, 0xA90)
    assert stock.read(GRID + 0xD00, 0x1C) == native.read(GRID + 0xD00, 0x1C)  # counts + tree header
    cells_equal(stock, native, rows=range(strings), cols=frets)
    for s in range(8):
        row = u32s(native.read(GRID + ng.T_NATIVE + s * ROW, ROW))
        assert row[:frets] == (f2u(2000.0),) * frets, f"row {s}"
        assert row[frets:] == (0,) * (CELLS - frets), f"row {s}"
    # nothing outside the grid is touched: the 0xAA fill survives behind the 105Ch bytes and in front of the grid
    assert native.read(GRID + ng.GRID_NATIVE, 0x40) == b"\xaa" * 0x40
    assert native.read(GRID - 0x40, 0x40) == b"\xaa" * 0x40


# --- Set / Prune --------------------------------------------------------------------------------------------


def scenario(seed: int, frets: int, strings: int = 6):
    """Random next notes: (s, f, note, time) with times from a small pool (so ties and later-than happen)."""
    rnd = random.Random(seed)
    pool = [round(rnd.uniform(0.5, 9.0), 3) for _ in range(6)]
    return pool, [(rnd.randrange(strings), rnd.randrange(frets), note(k), rnd.choice(pool)) for k in range(40)]


def drive(m: Machine, frets, pool, sets, strings=6):
    m.init(strings, frets)
    m.set_tree(sorted(set(pool)))
    for s, f, nt, t in sets:
        m.set_cell(s, f, nt, t)
    m.prune()


@pytest.mark.parametrize("seed", range(6))
@pytest.mark.parametrize("frets", [5, 24, 25])
def test_init_set_prune_match_stock_at_six_strings(machines, seed, frets):
    pool, sets = scenario(seed, frets)
    for m in machines.values():
        drive(m, frets, pool, sets)
    stock = machines["stock"]
    cells_equal(stock, machines["native"], cols=frets)
    assert stock.read(GRID + 0xD00, 0x1C) == machines["native"].read(GRID + 0xD00, 0x1C)


def ptr(m, s, f):
    return struct.unpack("<I", m.read(GRID + (s * CELLS + f) * 4, 4))[0]


def test_prune_clears_cells_later_than_the_fourth_time_for_the_active_rows_only(machines):
    """With 5 distinct times begin + 3 (4.0) is the limit; rows 6/7 are outside the active count (6)."""
    native = machines["native"]
    native.init(6, 6)
    native.set_tree([1.0, 2.0, 3.0, 4.0, 5.0])
    for k, (s, f, t) in enumerate([(0, 0, 1.0), (1, 1, 4.0), (5, 2, 5.0), (6, 1, 5.0), (7, 3, 2.0), (7, 4, 4.5)]):
        native.set_cell(s, f, note(k), t)
    native.prune()
    assert ptr(native, 0, 0) and ptr(native, 1, 1)  # 1.0 and 4.0 <= limit: kept
    assert ptr(native, 5, 2) == 0  # 5.0: pruned
    assert ptr(native, 6, 1) and ptr(native, 7, 3) and ptr(native, 7, 4)


@pytest.mark.parametrize("s", [6, 7])
@pytest.mark.parametrize("state", ["empty", "earlier", "later"])
@pytest.mark.parametrize("f", [0, 12, 24])
def test_native_set_stores_strings_6_and_7_like_the_stock_rows(machines, s, f, state):
    native = machines["native"]
    native.init(6, 25)
    old = {"earlier": 1.0, "later": 3.0}.get(state)
    if old is not None:
        native.set_cell(s, f, note(99), old)
    native.set_cell(s, f, note(1), 1.5)
    t = struct.unpack("<f", native.read(GRID + ng.T_NATIVE + s * ROW + f * 4, 4))[0]
    want = {"empty": (note(1), 1.5), "later": (note(1), 1.5), "earlier": (note(99), 1.0)}[state]
    assert (ptr(native, s, f), t) == want


def test_native_set_column_past_the_fret_count_never_stores(machines):
    """Init fills fretCount columns with 2000.0; the next column stays 0.0 and no time beats it (same as stock)."""
    native = machines["native"]
    native.init(6, 25)
    native.set_cell(7, 25, note(1), 1.5)
    assert ptr(native, 7, 25) == 0


@pytest.mark.parametrize("s,f", [(8, 0), (255, 1), (0, 26), (7, 26), (7, 255)])
def test_native_set_rejects_out_of_range_cells_without_a_write(machines, s, f):
    native = machines["native"]
    native.init(6, 26)
    before = native.read(GRID - 0x100, 0x1400)
    native.set_cell(s, f, note(1), 1.5)
    assert native.read(GRID - 0x100, 0x1400) == before


def test_native_set_keeps_the_earliest_string_6_pointer(machines):
    native = machines["native"]
    native.init(6, 25)
    for k, (f, t) in enumerate([(3, 2.0), (3, 1.0), (7, 4.0), (24, 0.5)]):
        native.set_cell(6, f, note(k), t)
    row = u32s(native.read(GRID + 6 * ROW, ROW))
    assert (row[3], row[7], row[24]) == (note(1), note(2), note(3))  # fret 3: the earlier (1.0) note wins
    assert all(row[f] == 0 for f in range(25) if f not in (3, 7, 24))
