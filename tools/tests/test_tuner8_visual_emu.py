"""The native tuner session copies of the visual methods (letters, picks, peaks, fades, labels, the 3D
peg widget) executed against the stock functions: same calls / state for six strings (and bass), every
string of seven / eight with the tail arrays.
"""

import struct

import pytest

import tuner8_harness as h

pytestmark = pytest.mark.skipif(
    not (h.EXE.exists() and h.DLL.exists() and h.HEADER.exists()),
    reason="needs the unpacked exe, the built DLL and the generated copies",
)

OPEN = [40, 45, 50, 55, 59, 64, 35, 30]
OFFS = [0] * 8


def machine(native, n, fields=None, gs=None, pegs=None, arrays=None, stock_count=None):
    m = h.Machine(native)
    m.session(
        n,
        fields or {},
        arrays or h.arrays_for(OPEN, OFFS),
        stock_count=stock_count if stock_count is not None else min(n, 6),
    )
    h.with_world(m, gs if gs is not None else n, pegs if pegs is not None else n)
    m.effects[0x498AC0] = h.spawn_effect
    return m


def pair(fid, n, regs, stack=(), fields=None, ret=None, gs=None, pegs=None, arrays=None):
    out = []
    for native in (False, True):
        m = machine(native, n, fields, gs, pegs, arrays)
        calls = m.run(
            fid,
            {k: (h.SESSION if v == "S" else v) for k, v in regs.items()},
            [h.SESSION if a == "S" else a for a in stack],
            ret,
        )
        out.append((h.normalize(calls, native), m.arrays(), m.fields(), m))
    h.same_calls(out[0][0], out[1][0])
    assert out[0][2] == out[1][2], "session fields differ"
    for a, b in zip(out[0][1], out[1][1]):
        assert a == b[: len(a)], "arrays differ"
    return out


def native_run(fid, n, regs, stack=(), fields=None, ret=None, gs=None, pegs=None, arrays=None):
    m = machine(True, n, fields, gs, pegs, arrays, stock_count=6)
    calls = m.run(
        fid,
        {k: (h.SESSION if v == "S" else v) for k, v in regs.items()},
        [h.SESSION if a == "S" else a for a in stack],
        ret,
    )
    return m, calls


def tail(m, base, s, size=4):
    return struct.unpack("<I", m.uc.mem_read(h.SESSION + base + size * s, 4))[0]


FADE_IN, FADE_OUT, SPAWN, DESPAWN, MSG = 0x824E70, 0x825000, 0x498AC0, 0x498EF0, 0x942AC0


# --- fades -------------------------------------------------------------------------------------------
@pytest.mark.parametrize("fid,reg,callee", [("TS8FIN", "esi", FADE_IN), ("TS8FOU", "edi", FADE_OUT)])
@pytest.mark.parametrize("n", [4, 6])
def test_fades_match_stock(fid, reg, callee, n):
    pair(fid, n, {reg: "S"})


@pytest.mark.parametrize("fid,reg,callee", [("TS8FIN", "esi", FADE_IN), ("TS8FOU", "edi", FADE_OUT)])
@pytest.mark.parametrize("n", [7, 8])
def test_fades_reach_every_string(fid, reg, callee, n):
    stock = machine(False, 6)
    base = len([c for c in stock.run(fid, {reg: h.SESSION}) if c[0] == callee])
    m, calls = native_run(fid, n, {reg: h.SESSION})
    assert len([c for c in calls if c[0] == callee]) == base // 6 * n  # letter, sharp, pick per string


# --- pluck loops / peaks (GuitarStrings main[8], native active count) --------------------------------
@pytest.mark.parametrize("n", [4, 6])
def test_pluck_stop_matches_stock(n):
    pair("TS8PLS", n, {"edi": "S"})


@pytest.mark.parametrize("n", [7, 8])
def test_pluck_stop_reaches_the_native_strings(n):
    m, calls = native_run("TS8PLS", n, {"edi": h.SESSION})
    strings = [c[1][4] for c in calls if c[0] == 0x5992E0]  # ESI = the string object
    assert strings == [0x20300000 + 0x200 * s for s in range(n)]


@pytest.mark.parametrize("mode", [0, 1, 2])
@pytest.mark.parametrize("n", [4, 6])
def test_peaks_match_stock(mode, n):
    for detected in (0, 1):
        pair("TS8PEK", n, {"edi": "S"}, fields={0x18: mode, 0x18C: detected}, ret={0x4E8E50: 0})


@pytest.mark.parametrize("n", [7, 8])
def test_peaks_cover_the_native_strings(n):
    m, calls = native_run("TS8PEK", n, {"edi": h.SESSION}, fields={0x18: 2})
    targets = [c[2][0] for c in calls if c[0] == 0x4E8E50]  # the detector query per target
    assert targets == OPEN[:n]


# --- letters -----------------------------------------------------------------------------------------
@pytest.mark.parametrize("n", [4, 6])
def test_letters_match_stock(n):
    pair("TS8LET", n, {"esi": "S"})


@pytest.mark.parametrize("n", [7, 8])
def test_letters_for_every_string(n):
    m, calls = native_run("TS8LET", n, {"esi": h.SESSION})
    ltab = struct.unpack("<12I", m.read(0x12ED108, 48))
    hashes = [c[2][0] for c in calls if c[0] == SPAWN]
    want = []
    for s in range(n):
        # The top string's letter is the stock lowercase variant when off[0] == off[5] (0x5B1858).
        want.append(None if s == 5 else ltab[OPEN[s] % 12])
        if OPEN[s] % 12 in (1, 3, 6, 8, 10):
            want.append(0xBC6B5D41)  # the sharp sign
    assert len(hashes) == len(want)
    assert all(w is None or w == g for w, g in zip(want, hashes))
    assert hashes[6] == ltab[11]  # string 6: B
    for s in range(n):
        assert tail(m, 0x2E8, s, 8) != 0x30000000 + 0x100 * s + 1  # a new letter entity in the tail


@pytest.mark.parametrize("n", [4, 6])
def test_letter_placement_matches_stock(n):
    pair("TS8LPL", n, {}, stack=["S"], fields={0x178: 1})


@pytest.mark.parametrize("n,pegs", [(7, 7), (8, 7), (8, 8), (7, 6)])
def test_letter_placement_skips_strings_without_a_peg(n, pegs):
    m, calls = native_run("TS8LPL", n, {}, stack=[h.SESSION], fields={0x178: 1}, pegs=pegs)
    # Each string with a peg places its letter and sharp (0x7ADF70 per entity), none without.
    placed = [c for c in calls if c[0] == 0x7ADF70]
    assert len(placed) == 2 * min(n, pegs)


@pytest.mark.parametrize("n", [4, 6])
def test_letter_release_matches_stock(n):
    pair("TS8LRL", n, {"edi": "S"}, ret={DESPAWN: 1})


@pytest.mark.parametrize("n", [7, 8])
def test_letter_release_empties_the_tail(n):
    m, calls = native_run("TS8LRL", n, {"edi": h.SESSION}, ret={DESPAWN: 1})
    for s in range(8):
        want = 0 if s < n else 0x30000000 + 0x100 * s + 1
        assert tail(m, 0x2E8, s, 8) == want


# --- picks -------------------------------------------------------------------------------------------
@pytest.mark.parametrize("n", [4, 6])
def test_pick_setup_matches_stock(n):
    pair("TS8PKS", n, {}, stack=["S"], fields={0x15C: 0xFE})


@pytest.mark.parametrize("n", [7, 8])
def test_pick_setup_for_every_string(n):
    m, calls = native_run("TS8PKS", n, {}, stack=[h.SESSION], fields={0x15C: 0xFE})
    assert [c[2][0] for c in calls if c[0] == SPAWN] == [0x81EC99CC] * n
    assert all(tail(m, 0x328, s) >= h.ENTITY for s in range(n))


def test_stock_pick_setup_on_a_seven_built_guitar_overruns_the_six_slots():
    # fix/menutuner, the owner's crash (tests/owner-crash1, AV at 0x5B1EF5): the stock UI_PlayerTuner_SetupPicks loops
    # to InstrumentInfo +0Bh = 7 and stores pick 6 at +144h + 18h = +15Ch, the fret byte it reads for the cell.
    m = machine(False, 6, {0x15C: 0xFE}, stock_count=7)
    calls = m.run("TS8PKS", {}, [h.SESSION])
    assert [c[2][0] for c in calls if c[0] == SPAWN] == [0x81EC99CC] * 7
    assert m.get(h.SESSION + 0x15C) >= h.ENTITY  # slot 6 written over the fret field


def test_native_pick_setup_six_target_on_a_seven_built_guitar_stays_in_the_tail():
    # The same guitar with the session made native for a 6-string target (Rs7Tuner8_EnsureNative / TS): six picks in
    # the 8-slot tail, the fret field untouched.
    m = machine(True, 6, {0x15C: 0xFE}, stock_count=7)
    before = [tail(m, 0x328, s) for s in (6, 7)]
    calls = m.run("TS8PKS", {}, [h.SESSION])
    assert [c[2][0] for c in calls if c[0] == SPAWN] == [0x81EC99CC] * 6
    assert m.get(h.SESSION + 0x15C) == 0xFE
    assert all(tail(m, 0x328, s) >= h.ENTITY for s in range(6))
    assert [tail(m, 0x328, s) for s in (6, 7)] == before  # strings 6 / 7 untouched


@pytest.mark.parametrize("n", [4, 6])
def test_pick_release_and_place_match_stock(n):
    pair("TS8PKR", n, {"edi": "S"})
    pair("TS8PKP", n, {"esi": "S"}, fields={0x179: 1, 0x15C: 0xFE})


@pytest.mark.parametrize("n", [7, 8])
def test_picks_placed_and_released_on_every_string(n):
    m, calls = native_run("TS8PKP", n, {"esi": h.SESSION}, fields={0x179: 1, 0x15C: 0xFE})
    assert len([c for c in calls if c[0] == MSG]) == n
    m, calls = native_run("TS8PKR", n, {"edi": h.SESSION})
    assert all(tail(m, 0x328, s) == 0 for s in range(n))


@pytest.mark.parametrize("n", [4, 6])
def test_tuned_logo_matches_stock(n):
    for s in range(n):
        for on in (0, 1):
            pair("TS8PKL", n, {"ecx": "S", "edx": s}, stack=[on, 0, 0, 0, 0, 0, 0], ret={0x7AE740: 0x77})


@pytest.mark.parametrize("n", [7, 8])
def test_tuned_logo_on_the_extra_picks(n):
    for s in range(6, n):
        m, calls = native_run(
            "TS8PKL", n, {"ecx": h.SESSION, "edx": s}, stack=[1, 0, 0, 0, 0, 0, 0], ret={0x7AE740: 0x77}
        )
        assert [c[0] for c in calls][:1] == [0x5B20E0]  # the colour (copy, recorded as stock)
        assert tail(m, 0x348, s) == 0x77


@pytest.mark.parametrize("n", [4, 6, 7, 8])
def test_logo_off_every_string(n):
    if n <= 6:
        pair("TS8LOF", n, {"esi": "S"})
    m, calls = native_run("TS8LOF", n, {"esi": h.SESSION})
    assert [c[1][2] for c in calls if c[0] == 0x5B2560] == list(range(n))  # EDX = string


@pytest.mark.parametrize("n", [4, 6])
def test_pick_colours_match_stock(n):
    for s in range(n):
        pair("TS8PKC", n, {}, stack=["S", s])
        pair("TS8PKA", n, {"ecx": "S", "eax": s})


# --- hint, widget, labels ----------------------------------------------------------------------------
def test_hint_matches_stock():
    for s in range(6):
        pair("TS8HNT", 6, {"ecx": "S"}, fields={0x74: s, 0x18: 2}, ret={0x4E92E0: 0})


def component(m):
    comp = 0x20500000
    m.put(comp + 0x1C, h.GUITAR)
    return comp


@pytest.mark.parametrize("s", range(6))
def test_peg_widget_matches_stock(s):
    out = []
    for native in (False, True):
        m = machine(native, 6)
        calls = m.run("TS8FLA", {"ecx": component(m)}, [s])
        out.append(h.normalize(calls, native))
    h.same_calls(*out)


@pytest.mark.parametrize("pegs,s,peg", [(7, 6, 6), (7, 7, 6), (8, 7, 7), (6, 6, 5)])
def test_peg_widget_reaches_the_extra_pegs(pegs, s, peg):
    m = machine(True, 8, pegs=pegs)
    calls = m.run("TS8FLA", {"ecx": component(m)}, [s])
    ent = 0x20420000 + 0x400 * peg
    assert any(c[1][1] == ent or ent in c[2] for c in calls), "the widget is not at the expected peg"


def labels(m):
    stock = struct.unpack("<6I", m.read(0x12E8644, 24))
    base = m.exports["g_rs7_tu8_labels"]
    for k, v in enumerate(stock + (0x20600000, 0x20600010)):
        m.put(base + 4 * k, v)


@pytest.mark.parametrize("s", range(6))
def test_note_tuner_init_matches_stock(s):
    out = []
    for native in (False, True):
        m = machine(native, 6)
        labels(m)
        m.put(h.SESSION + 0xD0 + 0x14, h.GUITAR)
        calls = m.run("TS8NTI", {"edx": h.SESSION + 0xD0}, [s, OPEN[s], 0, 0])
        out.append(h.normalize(calls, native))
    h.same_calls(*out)


@pytest.mark.parametrize("n,s", [(7, 6), (8, 6), (8, 7)])
def test_note_tuner_labels_the_extra_strings(n, s):
    m = machine(True, n, stock_count=6)
    labels(m)
    m.put(h.SESSION + 0xD0 + 0x14, h.GUITAR)
    calls = m.run("TS8NTI", {"edx": h.SESSION + 0xD0}, [s, OPEN[s], 0, 0])
    label = 0x20600000 + 0x10 * (s - 6)
    assert any(label in c[2] or label in c[1] for c in calls), "the extra string's label is not used"
