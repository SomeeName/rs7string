"""Song-setup detector width without a running game (uses the analysis snapshot and built DLL).

[neck] clite: the guitar's string count is its build count (InstrumentInfo +0Bh); S4 only widens the
song's detector (Rs7Song_WidenDetector). Game _msize/show/detach calls are tiny deterministic stubs.
Every fake owner uses the confirmed September 2022 offsets; this checks the compiled helper rather
than a Python model.
"""

import struct
import pytest
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESP
from emu_harness import Emu, CODE, DATA, STACK, RET_MAGIC

SESSION, GUITAR, GS, INFO = (DATA + n for n in (0x10000, 0x12000, 0x14000, 0x16000))
DET, INPUT, MAT, VEC = (DATA + n for n in (0x20000, 0x24000, 0x26000, 0x28000))


def put(e, p, fmt, *values):
    e.uc.mem_write(p, struct.pack("<" + fmt, *values))


def u32(e, p):
    return struct.unpack("<I", e.read(p, 4))[0]


def call(e, name, *args):
    sp = STACK + 0x80000
    put(e, sp, "I" * (len(args) + 1), RET_MAGIC, *args)
    e.run(e.exports[name], RET_MAGIC, {UC_X86_REG_ESP: sp})
    return e.regs(UC_X86_REG_EAX)[0]


def setup(tuning, built=None, bass=False, enabled=True):
    e = Emu(patched=False)
    required = ("Rs7Song_WidenDetector", "Rs7Pitch_SetSongCount", "Rs7Pitch_SongCount")
    assert all(n in e.exports for n in required), "Rebuild DLL with publish/hooks_song.cpp before the song-setup tests"
    for name, value in [
        ("g_rs7_native_publish", enabled),
        ("g_rs7_publish_ports", 1),
        ("g_rs7_native_strings", 1),
        ("g_rs7_native_pitch", 1),
        ("g_rs7_endlog", 0),
    ]:
        put(e, e.exports[name], "I", int(value))
    # _msize reports capacity (the detector tail needs it).
    e.uc.mem_write(CODE + 0x1000, b"\xb8\xe0\x13\x00\x00\xc3")
    put(e, e.exports["g_rs7_fn_msize"], "I", CODE + 0x1000)
    call(e, "Rs7Gs_InfoAllocated", INFO)
    put(e, SESSION + 0x37C, "I", GUITAR)
    put(e, GUITAR + 0x24, "I", INFO)
    if built is None:
        built = len(tuning) if len(tuning) in (7, 8) else 6
    put(e, INFO + 0x0B, "B", 4 if bass else built)
    put(e, DET + 0x11EC, "I", 2 if bass else 1)
    put(e, DET + 0x11F8, "i", 28 if bass else 40)
    put(e, DET + 0x1238, "I", INPUT)
    put(e, INPUT + 8, "I", MAT)
    put(e, MAT + 4, "I", DET)
    put(e, MAT + 0x68, "I", 0x38503752)
    put(e, DET + 0x13D8, "II", 4 if bass else 6, 0x38503752)
    put(e, VEC, "II", VEC + 0x100, VEC + 0x100 + 2 * len(tuning))
    put(e, VEC + 0x100, "h" * len(tuning), *tuning)
    # The DLL's tail writers (PM0, PR0) record MAT / DET in the registry of tailed allocations Rs7Pitch_Tailed reads;
    # they leave the magic and the stock count written above.
    call(e, "Rs7Pitch_MatNew", MAT)
    call(e, "Rs7Pitch_DetReset", DET)
    return e


def widen(e):
    call(e, "Rs7Song_WidenDetector", SESSION, DET, VEC)


def det(e):
    return u32(e, DET + 0x13D8), struct.unpack("<i", e.read(DET + 0x11F8, 4))[0]


@pytest.mark.parametrize("n", [6, 7, 8])
def test_song_widens_the_detector_to_the_guitars_build_count(n):
    e = setup([0] * n)
    widen(e)
    assert det(e) == (n, {6: 40, 7: 35, 8: 30}[n])
    # The guitar's own counts are the build count and are never written here.
    assert (e.read(INFO + 0x0B, 1)[0]) == n


def test_lowest_open_follows_a_lowered_string():
    e = setup([0, 0, 0, 0, 0, 0, -2])
    widen(e)
    assert det(e) == (7, 33)


@pytest.mark.parametrize("built", [6, 8])
def test_a_guitar_built_with_another_count_is_not_widened(built):
    e = setup([0] * 7, built=built)
    widen(e)
    assert det(e) == (6, 40)
    assert call(e, "Rs7Pitch_SongCount", DET) == 0


def test_a_failed_neck_is_not_widened():
    # Review F3: GridBuild gave up on this guitar (5 s timeout without cells 6/7) -> its wait entry is marked failed
    # (WaitEntry: grid +0, owner +32, failed +36, 40 bytes; static_assert in features/publish/hooks_grid.cpp).
    e = setup([0] * 7)
    entry = e.exports["g_rs7_publish_wait"]
    put(e, entry, "I", DATA + 0x30000)
    put(e, entry + 32, "I", GUITAR)
    put(e, entry + 36, "B", 1)
    widen(e)
    assert det(e) == (6, 40)
    assert call(e, "Rs7Pitch_SongCount", DET) == 0
    put(e, entry + 36, "B", 0)  # the wait ended without a failure: the song widens as before
    widen(e)
    assert det(e) == (7, 35)


def test_bass_never_widens():
    e = setup([0] * 8, bass=True)
    widen(e)
    assert det(e) == (4, 28)


def test_switch_off_is_byte_inert():
    e = setup([0] * 8, enabled=False)
    before = [e.read(p, size) for p, size in [(INFO, 0x48), (DET, 0x13E0)]]
    widen(e)
    assert before == [e.read(p, size) for p, size in [(INFO, 0x48), (DET, 0x13E0)]]


@pytest.mark.parametrize("bad", ["det_tail", "info"])
def test_missing_owner_never_widens(bad):
    e = setup([0] * 8)
    if bad == "det_tail":
        put(e, DET + 0x13DC, "I", 0)
    elif bad == "info":
        call(e, "Rs7Gs_InfoFreeSize", INFO)
    widen(e)
    assert det(e) == (6, 40)


@pytest.mark.parametrize("n", [7, 8])
def test_settuning_after_s4_keeps_the_width(n):
    # S4 widens BEFORE it jumps into Play_Detector_SetTuning, whose exit hooks (PT1/S3) re-write the detector
    # tail: the width must survive that, and the next S4 of a six-string song puts the stock width back.
    e = setup([0] * n)
    widen(e)
    call(e, "Rs7Pitch_DetTuned", DET, VEC)
    assert det(e)[0] == n
    six = [0] * 6
    put(e, VEC, "II", VEC + 0x100, VEC + 0x100 + 12)
    widen(e)  # a six-string song on the same detector
    call(e, "Rs7Pitch_DetTuned", DET, VEC)
    assert det(e)[0] == 6


def test_detector_reset_ends_the_song_width():
    e = setup([0] * 8)
    widen(e)
    assert call(e, "Rs7Pitch_SongCount", DET) == 8
    call(e, "Rs7Pitch_DetReset", DET)
    assert call(e, "Rs7Pitch_SongCount", DET) == 0
    assert det(e)[0] == 6


def test_settuning_on_an_unwidened_detector_stays_stock():
    e = setup([0] * 8, enabled=False)
    call(e, "Rs7Pitch_DetTuned", DET, VEC)
    assert u32(e, DET + 0x13D8) == 6


@pytest.mark.parametrize(
    "begin,end",
    [
        (0, 0),
        (VEC + 0x100, VEC + 0xFF),
        (VEC + 0x100, VEC + 0x101),
        (VEC + 0x100, VEC + 0x112),
        (VEC + 0x100, VEC + 0x100),
        (VEC + 0x100, VEC + 0x10A),
    ],
)
def test_malformed_vector_changes_nothing(begin, end):
    e = setup([0] * 8)
    before = [e.read(p, size) for p, size in [(INFO, 0x48), (DET, 0x13E0)]]
    put(e, VEC, "II", begin, end)
    widen(e)
    assert before == [e.read(p, size) for p, size in [(INFO, 0x48), (DET, 0x13E0)]]
