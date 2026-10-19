"""Native next-note grid, whole Rnd_NeckTargets_Update 0x7E4870 with a fake Register and Spawn: spawned targets equal stock."""

from __future__ import annotations

import random
import struct

import pytest
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_READ
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESP

import gen_nextgrid as ng
from emu_common import needs_exe_and_dll
from nextgrid_harness import (
    FAKE,
    FAKE_PTR,
    GRID,
    NODES,
    NOTE_BASE,
    RET,
    SESS,
    SPAWN,
    STACK,
    TOP,
    UPDATE,
    Machine,
    cells_equal,
    f2u,
    note,
    run_update,
)

pytestmark = needs_exe_and_dll


# --- the whole Rnd_NeckTargets_Update ------------------------------------------------------------------------


UPDATE_TIMES = [1.0, 2.0, 3.0, 4.0, 5.0]


def update_sets(rnd: random.Random, strings: int, frets: int, count=30):
    return [(rnd.randrange(strings), rnd.randrange(frets), note(k), rnd.choice(UPDATE_TIMES)) for k in range(count)]


@pytest.mark.parametrize("seed", range(5))
def test_update_spawns_the_same_targets_at_six_strings(machines, seed):
    sets = update_sets(random.Random(seed), 6, 24)
    res = {name: run_update(m, sets, UPDATE_TIMES) for name, m in machines.items()}
    stock = res["stock"]
    assert stock[0], "the scenario must spawn something"
    native = res["native"]
    assert native[0] == stock[0]
    assert native[1] == stock[1]  # esp / ebp / ebx / esi / edi restored
    assert native[2] == stock[2]  # the caller's stack above untouched
    assert native[4] is True and stock[4] is True  # the /GS cookie slot is intact
    assert res["native"][1][0] == res["native"][3] + 12  # ret 8
    # the tree release (size != 0): this = the set header, arg = its root; both moved with the grid
    ebp = res["stock"][3] - 4
    root = NODES
    assert res["stock"][5] == [(ebp - 0x1C, root)]
    assert res["native"][5] == [(ebp - 0x35C, root)]


def test_update_native_frame_is_1074h_and_probed():
    native = Machine("native")  # a fresh machine: Unicorn applies a memory hook only to blocks translated after it
    reads = []
    native.uc.hook_add(UC_HOOK_MEM_READ, lambda uc, a, addr, s, v, u: reads.append(addr))
    run_update(native, update_sets(random.Random(1), 6, 24), UPDATE_TIMES)
    ebp = TOP - 4
    assert ebp - 0x1000 in reads  # NGP's probe of the page the frame crosses
    # the grid (ebp-1060h .. ebp-4) was built by Init in place: counts and the set header are where the layout says
    assert struct.unpack("<HH", native.read(ebp - 0x1060 + 0xD00, 4)) == (6, 24)


@pytest.mark.parametrize("seed", range(3))
def test_update_string_6_targets_match_the_stock_loops_at_seven(machines, seed):
    """NG9 at an active count of 6 spawns row 6 exactly as the stock loops do it at 7 (the oracle that replaced the
    removed R7b / N9 comparison)."""
    sets = update_sets(random.Random(100 + seed), 7, 24)
    assert any(s == 6 for s, *_ in sets)
    six = run_update(machines["native"], sets, UPDATE_TIMES, strings=6)
    seven = run_update(machines["native"], sets, UPDATE_TIMES, strings=7)
    assert any(c[4] == 6 for c in six[0]), "string 6 must spawn something"
    assert sorted(c for c in six[0] if c[4] == 6) == sorted(c for c in seven[0] if c[4] == 6)
    assert six[1] == seven[1]


def test_update_string_7_cells_are_stored_but_not_spawned_while_the_count_is_six(machines):
    base = update_sets(random.Random(7), 7, 24)
    with7 = base + [(7, f, note(60 + f), 1.0 + f % 3) for f in range(1, 12)]
    a = run_update(machines["native"], base, UPDATE_TIMES)
    b = run_update(machines["native"], with7, UPDATE_TIMES)
    assert [c for c in b[0] if c[4] == 7] == []
    assert b[0] == a[0]
    assert b[1] == a[1] and b[4] == a[4]


def test_update_from_seven_strings_row_6_is_left_to_the_stock_loops(machines):
    """Active count 7: the stock loops enumerate row 6, NG9 must not spawn it a second time."""
    sets = [(6, f, note(k), 1.0) for f, k in enumerate((71, 72, 73, 75, 76), 1)]  # note k % 5 == 4: no spawn flags
    six = run_update(machines["native"], sets, UPDATE_TIMES, strings=6)
    seven = run_update(machines["native"], sets, UPDATE_TIMES, strings=7)
    assert len(six[0]) == len(seven[0]) == 5
    assert sorted(six[0]) == sorted(seven[0])  # same targets, once each: by NG9 at 6, by the stock loop at 7
