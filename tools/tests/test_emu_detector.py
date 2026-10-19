"""Pitch and detector stubs: expected MIDI (S2), SetTuning exit (S3), wrong-instrument threshold (W) and the inverse pitch (I1)."""

from __future__ import annotations

import struct
from contextlib import contextmanager

import pytest
from unicorn.x86_const import (
    UC_X86_REG_EAX,
    UC_X86_REG_EBP,
    UC_X86_REG_EBX,
    UC_X86_REG_ECX,
    UC_X86_REG_EDI,
    UC_X86_REG_EDX,
    UC_X86_REG_ESI,
    UC_X86_REG_ESP,
    UC_X86_REG_FPSW,
)

from emu_harness import ALL_REGS, CODE, DATA, Emu, RET_MAGIC, STACK, needs_emu, site_target, site_va

pytestmark = needs_emu


# ---- S2: plain single-note expected MIDI (0x873E90) ----------------------------------------


def call_twin(emu: Emu, string: int, fret: int, tuning: list[int]) -> int:
    vec_data = DATA + 0x100
    emu.uc.mem_write(vec_data, struct.pack(f"<{len(tuning)}h", *tuning))
    emu.uc.mem_write(DATA, struct.pack("<II", vec_data, vec_data + 2 * len(tuning)))
    esp = STACK + 0x80000
    emu.uc.mem_write(esp, struct.pack("<III", RET_MAGIC, fret, DATA))
    emu.run(
        0x873E90,
        RET_MAGIC,
        {UC_X86_REG_ESP: esp, UC_X86_REG_EAX: 1, UC_X86_REG_EDX: string, UC_X86_REG_EBP: STACK + 0x90000},
    )
    return emu.uc.reg_read(UC_X86_REG_EAX)


@pytest.mark.parametrize("string", range(6))
@pytest.mark.parametrize("tuning", [[0] * 6, [0] * 7, [-2, 0, 0, 0, 0, 0, -1]])
def test_s2_strings_0_to_5_are_stock(emus, string, tuning):
    stock, patched = emus
    assert call_twin(patched, string, 3, tuning) == call_twin(stock, string, 3, tuning)


@pytest.mark.parametrize("fret,expected", [(0, 35), (3, 38), (5, 40), (7, 42)])
def test_s2_string_6_in_a_7_entry_song_is_low_b(emus, fret, expected):
    _, patched = emus
    assert call_twin(patched, 6, fret, [0] * 7) == expected


def test_s2_string_6_uses_its_tuning_offset(emus):
    _, patched = emus
    assert call_twin(patched, 6, 3, [0, 0, 0, 0, 0, 0, -1]) == 37


# ---- S3: exit of Play_Detector_SetTuning (0x4E2590), whole function -------------------------
# Rs7_OnDetectorTuned is pure C without calls, so it runs in the emulator as built.

SET_TUNING = 0x4E2590
DET = DATA + 0x7000
DET_SIZE = 0x1360
DET_INSTRUMENT, DET_LOWEST = 0x11EC, 0x11F8


def run_set_tuning(emu: Emu, tuning: list[int], instrument: int):
    vec_data = DATA + 0x100
    emu.uc.mem_write(vec_data, struct.pack(f"<{len(tuning)}h", *tuning))
    emu.uc.mem_write(DATA, struct.pack("<II", vec_data, vec_data + 2 * len(tuning)))
    det = bytearray((i * 5 + 1) & 0xFF for i in range(DET_SIZE))
    det[DET_INSTRUMENT : DET_INSTRUMENT + 4] = struct.pack("<I", instrument)
    emu.uc.mem_write(DET, bytes(det))
    esp = STACK + 0x80000
    emu.uc.mem_write(esp, struct.pack("<I", RET_MAGIC))
    emu.run(
        SET_TUNING,
        RET_MAGIC,
        {
            UC_X86_REG_ESP: esp,
            UC_X86_REG_EAX: DET,
            UC_X86_REG_ESI: DATA,
            UC_X86_REG_EBX: 0xB1B1,
            UC_X86_REG_EDI: 0xD1D1,
            UC_X86_REG_EBP: 0x1111,
        },
    )
    return emu.read(DET, DET_SIZE), emu.regs(*ALL_REGS), emu.fpu_top()


def lowest(det: bytes) -> int:
    return struct.unpack_from("<i", det, DET_LOWEST)[0]


S3_TUNINGS = [
    [0] * 6,
    [-2, 0, 0, 0, 0, 0],
    [0] * 7,
    [0, 0, 0, 0, 0, 0, -2],
    [0, 0, 0, 0, 0, 0, 10],
    [-2] * 7,
    [0] * 8,
    [-4] * 7,
    [0, 0, 0, 0, 0, 0, -4],
    [0, 0, 0, 0, 0, 0, -5],
]


@pytest.mark.parametrize("instrument", [0, 1, 2])
@pytest.mark.parametrize("tuning", S3_TUNINGS, ids=lambda t: f"{len(t)}x{t[-1]}" if t else "empty")
def test_s3_set_tuning(emus, tuning, instrument):
    stock, patched = emus
    assert SET_TUNING < site_va("S3") < SET_TUNING + 0xC0
    want, got = run_set_tuning(stock, tuning, instrument), run_set_tuning(patched, tuning, instrument)
    assert got[1:] == want[1:]  # registers, FPU as stock
    if len(tuning) == 7 and instrument != 2:
        assert lowest(got[0]) == min(lowest(want[0]), 35 + tuning[6])
        assert got[0][:DET_LOWEST] + got[0][DET_LOWEST + 4 :] == want[0][:DET_LOWEST] + want[0][DET_LOWEST + 4 :]
    else:
        assert got[0] == want[0]


@pytest.mark.parametrize("t6,low", [(0, 35), (-2, 33), (-4, 31), (-5, 30)])
def test_s3_lowest_open_is_the_7th_string_for_extended_low_tunings(emus, t6, low):
    """Item 2: the song-play lowest-open policy (+11F8h) reaches Drop A 33, G 31, F#1 30 (guitar only)."""
    stock, patched = emus
    tuning = [0, 0, 0, 0, 0, 0, t6]
    assert lowest(run_set_tuning(patched, tuning, 1)[0]) == low
    assert lowest(run_set_tuning(patched, tuning, 0)[0]) == low
    assert lowest(run_set_tuning(patched, tuning, 2)[0]) == lowest(run_set_tuning(stock, tuning, 2)[0])  # bass stock
    assert lowest(run_set_tuning(patched, [0] * 6, 1)[0]) == lowest(run_set_tuning(stock, [0] * 6, 1)[0])


def test_s3_six_entries_after_a_seven_entry_song_are_stock(emus):
    """Final review F1: the tuner/Guitarcade re-tune with a 6-entry vector after a 7-string song."""
    stock, patched = emus
    seven = run_set_tuning(patched, [0] * 7, 1)
    assert lowest(seven[0]) == 35
    assert run_set_tuning(patched, [0] * 6, 1) == run_set_tuning(stock, [0] * 6, 1)


# ---- W: UI_PlayerTuner_CheckWrongInstrument 0x5B2DDE, `fcomp float [34.0]` (low range) -----------
# ST0 = the mean of ten detected notes, ESI = tuner session. Stock warns when mean <= 34 (C0 or C3
# set by the compare, tested at 0x5B2DE6). 7-string sessions (the exported table the C++ keeps) get
# min(34, lowest open - 1); everything else, and [tuner] lowrange=0, stays stock.

SESSION_7, SESSION_6 = 0x5D400000, 0x5D400400
SEVEN_TUNINGS = {  # name -> (lowest open MIDI of the arrangement, expected threshold)
    "B": (35, 34),
    "Drop A": (33, 32),
    "A": (33, 32),
    "G": (31, 30),
    "8str F#1": (30, 29),
    "tuned up": (40, 34),
}


def run_wrong(emu: Emu, mean: float, session: int, table: list[tuple[int, int]], lowrange: int = 1, any_: int = 1):
    w = lambda name, fmt, *v: emu.uc.mem_write(emu.exports[name], struct.pack(fmt, *v))
    w("g_rs7_lowrange", "<I", lowrange)
    w("g_rs7_tuner_any", "<I", any_)
    w("g_rs7_wrong_tab", "<8I", *([x for e in table for x in e] + [0] * (8 - 2 * len(table))))
    va = site_va("W")
    emu.uc.mem_write(DATA + 0x40, struct.pack("<f", mean))
    # helper code: fld dword [DATA+0x40]; jmp site
    j = CODE + 0x20 + 6
    emu.uc.mem_write(
        CODE + 0x20, bytes.fromhex("D9 05") + struct.pack("<I", DATA + 0x40) + b"\xe9" + struct.pack("<i", va - (j + 5))
    )
    esp = STACK + 0x80000
    emu.uc.mem_write(esp, struct.pack("<I", RET_MAGIC))
    emu.run(
        CODE + 0x20,
        va + 6,
        {
            UC_X86_REG_ESI: session,
            UC_X86_REG_ESP: esp,
            UC_X86_REG_EAX: 0xA1A1,
            UC_X86_REG_EBX: 0xB1B1,
            UC_X86_REG_ECX: 0xC1C1,
            UC_X86_REG_EDX: 0xD1D1,
            UC_X86_REG_EDI: 0xE1E1,
            UC_X86_REG_EBP: 0x1111,
            UC_X86_REG_FPSW: 0,
        },
    )
    sw = emu.uc.reg_read(UC_X86_REG_FPSW)
    w("g_rs7_tuner_any", "<I", 0)  # the emulators are shared: no tuner state afterwards
    w("g_rs7_lowrange", "<I", 1)
    warn = bool(sw & ((1 << 8) | (1 << 14)))  # C0 (mean < thr) or C3 (equal): `test ah,41h` != 0
    return warn, emu.fpu_top(), emu.regs(*ALL_REGS)


MEANS = [29.0, 30.0, 31.0, 32.0, 33.0, 33.5, 34.0, 34.5, 35.0, 36.0, 41.0]


@pytest.mark.parametrize("mean", MEANS)
@pytest.mark.parametrize("case", ["6-string/bass (no state)", "other session", "lowrange=0", "no tuner state"])
def test_w_everything_else_is_stock(emus, mean, case):
    stock, patched = emus
    table = [(SESSION_7, 30)] if case != "6-string/bass (no state)" else []
    session = SESSION_6 if case in ("other session", "6-string/bass (no state)") else SESSION_7
    kw = {"lowrange": 0} if case == "lowrange=0" else {"any_": 0} if case == "no tuner state" else {}
    want = run_wrong(stock, mean, session, table)
    got = run_wrong(patched, mean, session, table, **kw)
    assert want[0] == (mean <= 34.0)
    assert got == want  # warns like stock; x87 depth and every register as stock


@pytest.mark.parametrize("mean", MEANS)
@pytest.mark.parametrize("name", SEVEN_TUNINGS)
def test_w_seven_string_threshold_follows_the_lowest_string(emus, name, mean):
    stock, patched = emus
    _, thr = SEVEN_TUNINGS[name]
    table = [(SESSION_6, 12), (SESSION_7, thr)]
    got = run_wrong(patched, mean, SESSION_7, table)
    want = run_wrong(stock, mean, SESSION_7, table)
    assert got[0] == (mean <= thr)
    assert got[1:] == want[1:]  # x87 depth and registers as stock


def test_w_detects_the_correct_open_note_without_warning(emus):
    """The point: the open lowest string (A1/G1/F#1) no longer warns, one semitone below it still does."""
    _, patched = emus
    for name, (low, thr) in SEVEN_TUNINGS.items():
        table = [(SESSION_7, thr)]
        assert not run_wrong(patched, float(low), SESSION_7, table)[0], name
        if thr < 34:
            assert run_wrong(patched, float(low - 2), SESSION_7, table)[0], name


# ---- technique fixes for string 6 (docs/ARCHITECTURE.md): I1 inverse pitch ----
# Rs7_Base6 reads the side store (covered by the C++ unit tests); here
# they are replaced by `mov eax,[cell]; ret` so the stubs' own logic runs against the stock code.


@contextmanager
def fake_c(emu: Emu, name: str, cell: int, value: int):
    addr = emu.exports[name]  # the C helper of the hook
    saved = emu.read(addr, 6)
    emu.uc.mem_write(cell, struct.pack("<I", value & 0xFFFFFFFF))
    emu.uc.mem_write(addr, bytes([0xA1]) + struct.pack("<I", cell) + bytes([0xC3]))
    try:
        yield
    finally:
        emu.uc.mem_write(addr, saved)


def call_midi_to_fret(emu: Emu, midi: int, string: int, tuning: list[int] | None, instrument: int):
    esi = 0
    if tuning is not None:
        esi = DATA + 0x200
        emu.uc.mem_write(esi, struct.pack(f"<{len(tuning)}h", *tuning))
    esp = STACK + 0x80000
    emu.uc.mem_write(esp, struct.pack("<II", RET_MAGIC, instrument))
    emu.run(
        0x874050,
        RET_MAGIC,
        {
            UC_X86_REG_ESP: esp,
            UC_X86_REG_ECX: midi,
            UC_X86_REG_EDX: string,
            UC_X86_REG_ESI: esi,
            UC_X86_REG_EBP: STACK + 0x90000,
        },
    )
    return emu.uc.reg_read(UC_X86_REG_EAX) & 0xFFFFFFFF, emu.uc.reg_read(UC_X86_REG_ESP)


def test_i1_site_is_in_play_midi_to_fret():
    assert site_va("I1") == 0x874056 and site_target("I1") == 0x874079


@pytest.mark.parametrize("instrument", [0, 1, 2])
@pytest.mark.parametrize("string", range(6))
@pytest.mark.parametrize("tuning", [None, [0] * 6, [-2, 0, 1, 0, 0, 0]])
def test_i1_strings_0_to_5_and_bass_are_stock(emus, instrument, string, tuning):
    stock, patched = emus
    with fake_c(patched, "Rs7_Base6", DATA + 0x300, 35):
        assert call_midi_to_fret(patched, 52, string, tuning, instrument) == call_midi_to_fret(
            stock, 52, string, tuning, instrument
        )


@pytest.mark.parametrize("tuning", [None, [0] * 7])
def test_i1_string_6_of_a_bass_is_stock(emus, tuning):
    stock, patched = emus
    with fake_c(patched, "Rs7_Base6", DATA + 0x300, 35):
        assert call_midi_to_fret(patched, 40, 6, tuning, 2) == call_midi_to_fret(stock, 40, 6, tuning, 2)


@pytest.mark.parametrize("instrument", [0, 1])
def test_i1_string_6_not_in_seven_string_mode_is_stock(emus, instrument):
    stock, patched = emus
    with fake_c(patched, "Rs7_Base6", DATA + 0x300, 0x80000000):
        for tuning in (None, [0] * 7):
            assert call_midi_to_fret(patched, 40, 6, tuning, instrument) == call_midi_to_fret(
                stock, 40, 6, tuning, instrument
            )


@pytest.mark.parametrize("t6", [0, -1, -3, 2])
@pytest.mark.parametrize("fret", [0, 3, 7, 12])
def test_i1_string_6_is_the_inverse_of_the_forward_pitch(emus, t6, fret):
    _, patched = emus
    with fake_c(patched, "Rs7_Base6", DATA + 0x300, 35 + t6):
        got, esp = call_midi_to_fret(patched, 35 + t6 + fret, 6, [0] * 6 + [t6], 0)
    assert got == fret
    assert esp == STACK + 0x80000 + 4  # plain ret, the instrument argument stays for the caller


def test_i1_string_6_without_tuning_pointer_is_stock(emus):
    stock, patched = emus
    with fake_c(patched, "Rs7_Base6", DATA + 0x300, 35):
        assert call_midi_to_fret(patched, 40, 6, None, 0) == call_midi_to_fret(stock, 40, 6, None, 0)
