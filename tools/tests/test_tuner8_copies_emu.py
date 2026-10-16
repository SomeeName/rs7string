"""The native tuner session copies (tools/gen_tuner8s.py) executed against the stock functions.

Six strings (and bass): the copy with its tail must make the same calls and leave the same session as
the stock function with its arrays. Seven / eight strings: the copy walks the low-B-first order and
reads strings 6 / 7 from the tail.
"""

import pytest

import tuner8_harness as h

pytestmark = pytest.mark.skipif(
    not (h.EXE.exists() and h.DLL.exists() and h.HEADER.exists()),
    reason="needs the unpacked exe, the built DLL and the generated copies",
)

GUITAR_OPEN = [40, 45, 50, 55, 59, 64, 35, 30]
OFFS = [0, 0, 0, 0, 0, 0, 0, 0]


def pair(fid, n, fields, regs, stack=(), ret=None, lefty=0, stock_count=None, arrays=None):
    """Run stock (count from InstrumentInfo) and copy (count n in the tail); both results."""
    arrays = arrays or h.arrays_for(GUITAR_OPEN, OFFS)
    out = []
    for native in (False, True):
        m = h.Machine(native)
        m.session(n, fields, arrays, lefty=lefty, stock_count=stock_count if stock_count is not None else n)
        calls = m.run(
            fid,
            {k: (h.SESSION if v == "S" else v) for k, v in regs.items()},
            [h.SESSION if a == "S" else a for a in stack],
            ret,
        )
        out.append((h.normalize(calls, native), m.arrays(), m.fields(), m))
    return out


def same(stock, native, n=6):
    h.same_calls(stock[0], native[0])
    assert stock[2] == native[2], "session fields differ"
    for a, b in zip(stock[1], native[1]):
        assert a == b[: len(a)], "arrays differ"


def cur(m):
    return m.get(h.SESSION + 0x74)


# --- arrow keys (UI_PlayerTuner_StepStringA / B, EAX session) --------------------------------------
@pytest.mark.parametrize("fid", ["TS8STA", "TS8STB"])
@pytest.mark.parametrize("n", [4, 6])
@pytest.mark.parametrize("lefty", [0, 1])
def test_steps_match_stock(fid, n, lefty):
    for c in range(n):
        s, v = pair(fid, n, {0x74: c}, {"eax": "S"}, lefty=lefty)
        same(s, v)


@pytest.mark.parametrize("n", [7, 8])
def test_steps_walk_the_low_first_order(n):
    order = list(h.order(n)[:n])
    for fid, lefty, d in (("TS8STA", 0, -1), ("TS8STA", 1, 1), ("TS8STB", 0, 1), ("TS8STB", 1, -1)):
        for r, c in enumerate(order):
            m = h.Machine(True)
            m.session(n, {0x74: c}, h.arrays_for(GUITAR_OPEN, OFFS), lefty=lefty, stock_count=6)
            calls = m.run(fid, {"eax": h.SESSION})
            want = order[r + d] if 0 <= r + d < n else None
            if want is None:
                assert cur(m) == c and not calls
            else:
                assert cur(m) == want
                assert [c_[0] for c_ in calls] == [0x5B0490] and calls[0][1][5] == want  # EDI = string


# --- string selection (UI_PlayerTuner_SelectString, ESI session, EDI string) -----------------------
@pytest.mark.parametrize("mode", [0, 2, 3])
def test_select_matches_stock(mode):
    for s in range(6):
        a, b = pair("TS8SEL", 6, {0x18: mode, 0xF4: h.WORLD, 0x68: 2}, {"esi": "S", "edi": s})
        same(a, b)


@pytest.mark.parametrize("n,s", [(7, 6), (8, 6), (8, 7)])
def test_select_reads_the_extra_targets(n, s):
    m = h.Machine(True)
    m.session(n, {0x18: 2, 0xF4: h.WORLD}, h.arrays_for(GUITAR_OPEN, OFFS), stock_count=6)
    calls = m.run("TS8SEL", {"esi": h.SESSION, "edi": s})
    tol = [c for c in calls if c[0] == 0x5B2A80][0]
    assert tol[2][:3] == (h.SESSION, GUITAR_OPEN[s], s)  # GetTolerance(session, open[s], s)
    note = [c for c in calls if c[0] == 0x85DB30][0]
    assert note[2][:2] == (s, GUITAR_OPEN[s])  # NoteTuner_Initialize(s, open[s], ...)


# --- mode 2 entry (0x5B00C0: [esp+4] session, [esp+8] string, EAX sub mode) -------------------------
def test_mode2_entry_matches_stock():
    for s in range(7):
        for sub in (0, 1, 5):
            for reverse in (0, 1):
                a, b = pair("TS8M2E", 6, {0x18: 0, 0x22C: reverse << 8}, {"eax": sub}, stack=["S", s])
                same(a, b)


@pytest.mark.parametrize("n", [7, 8])
def test_mode2_entry_accepts_the_extra_strings(n):
    m = h.Machine(True)
    m.session(n, {0x18: 0}, h.arrays_for(GUITAR_OPEN, OFFS), stock_count=6)
    calls = m.run("TS8M2E", {"eax": 1}, stack=[h.SESSION, n - 1])
    assert cur(m) == n - 1 and any(c[0] == 0x5B0490 for c in calls)


# --- modes: the advance after a tuned string ------------------------------------------------------
TUNED = {0x85E2E0: 1, 0x5B4360: 1, 0x5B0C20: 1}


@pytest.mark.parametrize("fid", ["TS8MD2", "TS8MD3"])
@pytest.mark.parametrize("reverse", [0, 1])
def test_precision_modes_match_stock(fid, reverse):
    for c in range(6):
        out = []
        for native in (False, True):
            m = h.Machine(native)
            m.session(
                6, {0x74: c, 0x110: 1, 0x1C: 0, 0x18: 2 if fid == "TS8MD2" else 3}, h.arrays_for(GUITAR_OPEN, OFFS)
            )
            m.uc.mem_write(h.SESSION + 0x22D, bytes([reverse]))
            calls = m.run(fid, {}, [h.SESSION], TUNED)
            out.append((h.normalize(calls, native), cur(m)))
        h.same_calls(out[0][0], out[1][0])
        assert out[0][1] == out[1][1]


@pytest.mark.parametrize("fid", ["TS8MD2", "TS8MD3"])
@pytest.mark.parametrize("n", [7, 8])
@pytest.mark.parametrize("reverse", [0, 1])
def test_precision_modes_walk_the_order(fid, n, reverse):
    order = list(h.order(n)[:n])
    if reverse:
        order.reverse()
    for r, c in enumerate(order):
        m = h.Machine(True)
        m.session(n, {0x74: c, 0x110: 1, 0x1C: 0, 0x18: 2}, h.arrays_for(GUITAR_OPEN, OFFS), stock_count=6)
        m.uc.mem_write(h.SESSION + 0x22D, bytes([reverse]))
        calls = m.run(fid, {}, [h.SESSION], TUNED)
        if r + 1 < n:
            assert cur(m) == order[r + 1]
            assert 0x5B0490 in [x[0] for x in calls]
        else:
            assert cur(m) == c
            assert (0x5AFAE0 if fid == "TS8MD2" else 0x5AFCF0) in [x[0] for x in calls]  # end of the pass


@pytest.mark.parametrize("fid", ["TS8MD0", "TS8MD1"])
def test_check_modes_match_stock(fid):
    for c in range(6):
        for capo in (0, 2):
            a, b = pair(fid, 6, {0x74: c, 0x68: capo, 0x18: 0 if fid == "TS8MD0" else 1}, {"eax": "S"}, ret=TUNED)
            same(a, b)


@pytest.mark.parametrize("fid", ["TS8MD0", "TS8MD1"])
@pytest.mark.parametrize("n", [7, 8])
def test_check_modes_walk_the_order(fid, n):
    order = list(h.order(n)[:n])
    for r, c in enumerate(order):
        m = h.Machine(True)
        m.session(n, {0x74: c, 0x18: 0}, h.arrays_for(GUITAR_OPEN, OFFS), stock_count=6)
        calls = m.run(fid, {"eax": h.SESSION}, ret=TUNED)
        targets = [x[0] for x in calls]
        if r + 1 < n:
            assert cur(m) == order[r + 1] and 0x5B0490 in targets
        else:
            assert cur(m) == c and 0x5B0490 not in targets


def test_generated_copies_match_the_unpacked_image():
    """The tracked headers are what tools/gen_tuner8s.py emits (needs keystone: uv run --with keystone-engine)."""
    pytest.importorskip("keystone")
    import gen_fncopy
    import gen_tuner8s

    text, base = gen_fncopy.load_text()
    assert gen_tuner8s.render(text, base) == gen_tuner8s.OUT.read_text(encoding="utf-8")
    assert gen_tuner8s.render_ids() == gen_tuner8s.OUT_IDS.read_text(encoding="utf-8")


def test_every_copy_entry_has_its_site_row():
    import json

    rows = {s["id"]: s for s in json.loads((h.REPO / "sites.json").read_text(encoding="utf-8"))["sites"]}
    ids = (h.REPO / "src" / "core" / "tuner8s_ids.generated.h").read_text(encoding="utf-8")
    import re

    for fid in re.findall(r"X\((TS8\w+), \d+\)", ids):
        assert rows[fid]["tuner"] == "native" and rows[fid]["kind"] == "jmp"
        va, n, code, rels = h.specs()[fid]
        assert int(rows[fid]["note"].split()[1].rstrip(":"), 16) == va


@pytest.mark.parametrize("n", [7, 8])
@pytest.mark.parametrize("old_mode", [0, 5])
def test_mode2_menu_start_is_the_first_string_of_the_order(n, old_mode):
    for sub, string, reverse, want in ((0, 0, 0, n - 1), (1, 0, 0, 0), (0, 5, 1, 5), (1, 6, 0, 6)):
        m = h.Machine(True)
        m.session(n, {0x18: old_mode}, h.arrays_for(GUITAR_OPEN, OFFS), stock_count=6)
        m.uc.mem_write(h.SESSION + 0x22D, bytes([reverse]))
        calls = m.run("TS8M2E", {"eax": sub}, stack=[h.SESSION, string])
        assert cur(m) == want, (sub, string, reverse)
        sel = [c for c in calls if c[0] == 0x5B0490]
        assert sel and all(c[1][5] == want for c in sel)  # Every SelectString's EDI, including mode-5 reselect.
