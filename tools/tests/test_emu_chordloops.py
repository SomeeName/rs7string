"""Actual DLL hooks on the September 2022 snapshot; stock six-string equivalence."""

import struct

import pytest
from unicorn.x86_const import *
from test_emu import Emu, CODE, DATA, STACK, RET_MAGIC, site_va, FLAGS_MASK, DLL, SNAPSHOT

pytestmark = pytest.mark.skipif(
    not DLL.exists() or not (SNAPSHOT / "memory.bin").exists(), reason="needs built DLL and snapshot"
)
GS, INFO, GUITAR, CN, TPL = (DATA + x for x in (0x10000, 0x12000, 0x14000, 0x20000, 0x30000))


class ChordEmu(Emu):
    native_chordloops = True


@pytest.fixture(scope="module")
def pair():
    return Emu(False), ChordEmu(True)


def put(e, p, v):
    e.uc.mem_write(p, struct.pack("<I", v))


def register_info(e):
    """GIA1 registers each native InstrumentInfo with Rs7Gs_InfoAllocated; Rs7Gs_InfoNative (used by
    Rs7Gs_Active and the chord-bend check) requires that registration. Run once per emulator."""
    if getattr(e, "info_registered", False):
        return
    sp = STACK + 0x80000
    e.uc.mem_write(sp, struct.pack("<II", RET_MAGIC, INFO))
    e.run(e.exports["Rs7Gs_InfoAllocated"], RET_MAGIC, {UC_X86_REG_ESP: sp})
    e.info_registered = True


def setup(e, n=6):
    e.uc.mem_write(GS, bytes(0xB0))
    e.uc.mem_write(INFO, bytes(0x48))
    put(e, GS, GUITAR)
    put(e, GUITAR + 0x24, INFO)
    e.uc.mem_write(INFO + 0xB, bytes([6]))
    e.uc.mem_write(GS + 0x3A, b"R7")
    put(e, GS + 0xAC, 0x47375352)
    put(e, GS + 0xA8, INFO)
    e.uc.mem_write(INFO + 0x40, bytes([n, n]))
    put(e, INFO + 0x44, 0x49375352)
    put(e, e.exports["g_rs7_native_strings"], 1)
    for ghost in (False, True):
        for s in range(8):
            obj = DATA + 0x40000 + (int(ghost) * 8 + s) * 0x200
            e.uc.mem_write(obj, bytes(0x200))
            put(e, obj + 4, DATA + 0x90000)
            e.uc.mem_write(obj + 0x50, b"\x01")
            put(e, GS + (0x60 if ghost else 0x40) + s * 4, obj)
            if s < 6:
                put(e, GS + (0x1C if ghost else 4) + s * 4, obj)
    register_info(e)


@pytest.mark.parametrize("s", range(6))
@pytest.mark.parametrize("sid,start,end", [("CLGU", 0x7E3ED5, 0x7E3EEA), ("CLGE", 0x7E4347, 0x7E435A)])
def test_ghost_bend_access_six_strings_equals_stock(pair, s, sid, start, end):
    regs = {
        UC_X86_REG_ESP: STACK + 0x80000,
        UC_X86_REG_ESI: INFO,
        UC_X86_REG_EAX: 0xAB00 | s,
        UC_X86_REG_EDX: 5,
        UC_X86_REG_ECX: GS,
        UC_X86_REG_EFLAGS: 0x202,
    }
    if sid == "CLGE":
        regs.update({UC_X86_REG_EDX: GS, UC_X86_REG_ECX: s})
    outputs = []
    for e in pair:
        setup(e)
        e.run(start, end, regs)
        outputs.append((e.regs(*regs.keys()), e.uc.reg_read(UC_X86_REG_EFLAGS) & FLAGS_MASK))
    assert outputs[0] == outputs[1]


@pytest.mark.parametrize("s", [6, 7])
@pytest.mark.parametrize("sid,start,end", [("CLGU", 0x7E3ED5, 0x7E3EE6), ("CLGE", 0x7E4347, 0x7E4353)])
def test_ghost_bends_use_the_native_tail(pair, s, sid, start, end):
    e = pair[1]
    setup(e, 8)
    regs = {
        UC_X86_REG_ESP: STACK + 0x80000,
        UC_X86_REG_ESI: INFO,
        UC_X86_REG_EAX: s,
        UC_X86_REG_EDX: 5,
        UC_X86_REG_ECX: GS,
    }
    if sid == "CLGE":
        regs.update({UC_X86_REG_EDX: GS, UC_X86_REG_ECX: s})
    e.run(start, end, regs)
    assert e.uc.reg_read(UC_X86_REG_ECX) == DATA + 0x40000 + (8 + s) * 0x200


@pytest.mark.parametrize("s", range(8))
def test_bend_record_address_uses_c8_tail(pair, s):
    e = pair[1]
    put(e, STACK + 0x90000 - 0x78, CN)
    e.run(
        site_va("CLBA"),
        0x7E3C6C,
        {UC_X86_REG_ESP: STACK + 0x80000, UC_X86_REG_EBP: STACK + 0x90000, UC_X86_REG_ECX: CN, UC_X86_REG_EAX: s},
    )
    assert e.uc.reg_read(UC_X86_REG_EAX) == CN + 0x988 + s * 0x184


@pytest.mark.parametrize("s", range(8))
@pytest.mark.parametrize("mask", [0, 0x1000, 0x1001, 0x2000])
def test_bend_list_reads_c8_masks_with_stock_test_flags(pair, s, mask):
    a, b = pair
    put(a, CN + s * 4, mask)
    put(b, CN + 0x948 + s * 4, mask)
    flags = []
    for e in pair:
        e.run(
            0x7A62D4,
            0x7A62DB,
            {UC_X86_REG_ESP: STACK + 0x80000, UC_X86_REG_EDX: CN, UC_X86_REG_EAX: s, UC_X86_REG_EFLAGS: 0x202},
        )
        flags.append(e.uc.reg_read(UC_X86_REG_EFLAGS) & FLAGS_MASK)
    assert flags[0] == flags[1]


@pytest.mark.parametrize("s", range(6))
def test_stop_pluck_six_strings_equals_stock_at_the_game_call(pair, s):
    got = []
    for e in pair:
        setup(e)
        regs = {
            UC_X86_REG_ESP: STACK + 0x80000,
            UC_X86_REG_EAX: GS,
            UC_X86_REG_ECX: s,
            UC_X86_REG_EDX: INFO,
            UC_X86_REG_ESI: 0x5151,
            UC_X86_REG_EFLAGS: 0x202,
        }
        e.run(0x7E4178, 0x5992E0, regs)
        got.append((e.regs(*regs), e.uc.reg_read(UC_X86_REG_EFLAGS) & FLAGS_MASK, e.read(STACK + 0x80000 - 4, 4)))
    assert got[0] == got[1]


@pytest.mark.parametrize("s", [6, 7])
def test_stop_pluck_targets_native_main_not_ghost_prefix(pair, s):
    e = pair[1]
    setup(e, 8)
    e.run(
        0x7E4178,
        0x5992E0,
        {UC_X86_REG_ESP: STACK + 0x80000, UC_X86_REG_EAX: GS, UC_X86_REG_ECX: s, UC_X86_REG_EDX: INFO},
    )
    assert e.uc.reg_read(UC_X86_REG_ESI) == DATA + 0x40000 + s * 0x200


@pytest.mark.parametrize(
    "sid,start,reject,reg",
    [
        ("CLGU", 0x7E3ED5, 0x7E3F14, UC_X86_REG_EAX),
        ("CLGE", 0x7E4347, 0x7E4373, UC_X86_REG_ECX),
        ("CLVB", 0x7E4178, 0x7E4189, UC_X86_REG_ECX),
    ],
)
@pytest.mark.parametrize(
    "case",
    [
        "inactive",
        "unloaded",
        "null_object",
        "no_model",
        "no_shader",
        "bad_marker",
        "bad_info",
        "index8",
        "index255",
        "fret255",
    ],
)
def test_added_string_access_rejects_unusable_slots(pair, sid, start, reject, reg, case):
    if case == "fret255" and sid != "CLGU":
        return  # release/stop paths do not receive a fret
    e = pair[1]
    setup(e, 8)
    s = 8 if case == "index8" else 255 if case == "index255" else 7
    if case == "inactive":
        e.uc.mem_write(INFO + 0x40, b"\x07")
    if case == "unloaded":
        e.uc.mem_write(INFO + 0x40, b"\x07\x07")
    if case == "bad_marker":
        e.uc.mem_write(GS + 0x3A, b"xx")
    if case == "bad_info":
        put(e, INFO + 0x44, 0)
    ghost = sid != "CLVB"
    obj = DATA + 0x40000 + (8 * ghost + 7) * 0x200
    if case == "null_object":
        put(e, GS + (0x60 if ghost else 0x40) + 7 * 4, 0)
    if case == "no_model":
        put(e, obj + 4, 0)
    if case == "no_shader":
        e.uc.mem_write(obj + 0x50, b"\0")
    regs = {
        UC_X86_REG_ESP: STACK + 0x80000,
        UC_X86_REG_ESI: INFO,
        UC_X86_REG_EAX: s,
        UC_X86_REG_EDX: 255 if case == "fret255" else 5,
        UC_X86_REG_ECX: GS,
    }
    if sid == "CLGE":
        regs.update({UC_X86_REG_ECX: s, UC_X86_REG_EDX: GS})
    if sid == "CLVB":
        regs.update({UC_X86_REG_EAX: GS, UC_X86_REG_ECX: s, UC_X86_REG_EDX: INFO})
    e.run(start, reject, regs)
    assert e.uc.reg_read(UC_X86_REG_ESP) == STACK + 0x80000


@pytest.mark.parametrize("frets", [[-1] * 6, [0, -1, 5, 7, -1, 12], [1] * 6])
def test_chord_end_six_string_state_equals_stock(pair, frets):
    note = DATA + 0x60000
    states = []
    for e in pair:
        e.uc.mem_write(note, bytes(0x200))
        e.uc.mem_write(TPL, bytes(0x80))
        encoded = bytes(f & 255 for f in frets)
        e.uc.mem_write(TPL + 4, encoded)
        e.uc.mem_write(TPL + 0x48, encoded + b"\xff\xff")
        put(e, note + 0x30, TPL)
        put(e, note + 0xCC, 0xFF)
        e.run(0x7E4299, 0x7E42D1, {UC_X86_REG_ESP: STACK + 0x80000, UC_X86_REG_EDI: note})
        states.append(e.read(note, 0x200))
    assert states[0] == states[1]


def test_chord_end_updates_all_eight_string_bits(pair):
    e = pair[1]
    note = DATA + 0x60000
    e.uc.mem_write(note, bytes(0x200))
    e.uc.mem_write(TPL + 0x48, b"\x03" * 8)
    put(e, note + 0x30, TPL)
    put(e, note + 0xCC, 0xFF)
    e.run(0x7E4299, 0x7E42D1, {UC_X86_REG_ESP: STACK + 0x80000, UC_X86_REG_EDI: note})
    assert struct.unpack("<I", e.read(note + 0xCC, 4))[0] == 0
    assert struct.unpack("<I", e.read(note + 0xD4, 4))[0] == 0xFF


@pytest.mark.parametrize("s", [6, 7])
def test_native_target_lookup_preserves_live_x87_release_values(pair, s):
    e = pair[1]
    setup(e, 8)
    e.uc.mem_write(CODE + 0x3000, bytes.fromhex("DB E3 D9 E8 D9 EE"))  # fninit; fld1; fldz
    e.run(CODE + 0x3000, CODE + 0x3006, {})
    e.run(
        0x7E4347,
        0x7E4353,
        {UC_X86_REG_ESP: STACK + 0x80000, UC_X86_REG_ESI: INFO, UC_X86_REG_ECX: s, UC_X86_REG_EDX: GS},
    )
    assert e.st0() == 0.0
    assert e.st0() == 1.0


@pytest.mark.parametrize("mask", [0x00, 0x21, 0x3F, 0x40, 0x80, 0xC0, 0xFF])
def test_bend_list_selects_every_authored_bending_string(pair, mask):
    note, output = DATA + 0x60000, DATA + 0x70000
    # Substitute only the game's allocating tree insertion boundary: return the byte addressed
    # by its string key (EDI), so the real loop writes the authored fret there. No Windows heap
    # is needed; every loop test / mask reader / T8 fret access is actual patched game code.
    e = pair[1]
    e.uc.mem_write(0x7A7F00, b"\x0f\xb6\x07\x05" + struct.pack("<I", output) + b"\xc3")
    e.uc.mem_write(output, b"\xff" * 8)
    e.uc.mem_write(note, bytes(0x200))
    e.uc.mem_write(note + 0x52, b"\x01")
    put(e, note + 0x30, TPL)
    put(e, note + 0x34, CN)
    e.uc.mem_write(TPL + 0x48, bytes(range(8)))
    for s in range(8):
        put(e, CN + 0x948 + s * 4, 0x1000 if mask & (1 << s) else 0)
    esp = STACK + 0x80000
    e.uc.mem_write(esp, struct.pack("<II", RET_MAGIC, output))
    e.run(0x7A6280, RET_MAGIC, {UC_X86_REG_ESP: esp, UC_X86_REG_ESI: note})
    assert e.read(output, 8) == bytes(s if mask & (1 << s) else 255 for s in range(8))
    assert e.uc.reg_read(UC_X86_REG_ESP) == esp + 8


@pytest.mark.parametrize("published", [7, 8])
@pytest.mark.parametrize("s", range(8))
@pytest.mark.parametrize(
    "sid,start,end,ghost",
    [("CLGU", 0x7E3ED5, 0x7E3EE6, True), ("CLGE", 0x7E4347, 0x7E4353, True), ("CLVB", 0x7E4178, 0x5992E0, False)],
)
def test_published_count_keeps_added_strings_off_the_six_slot_prefix(pair, published, s, sid, start, end, ghost):
    """feat/publish raises InstrumentInfo +0Bh to 7/8: strings 0-5 stay on the stock prefix, 6/7 still
    go to the native objects (never prefix + 4*s past slot 5); a string above the count is rejected."""
    e = pair[1]
    setup(e, published)
    e.uc.mem_write(INFO + 0xB, bytes([published]))
    regs = {
        UC_X86_REG_ESP: STACK + 0x80000,
        UC_X86_REG_ESI: INFO,
        UC_X86_REG_EAX: s,
        UC_X86_REG_EDX: 5,
        UC_X86_REG_ECX: GS,
    }
    if sid == "CLGE":
        regs.update({UC_X86_REG_EDX: GS, UC_X86_REG_ECX: s})
    if sid == "CLVB":
        regs.update({UC_X86_REG_EAX: GS, UC_X86_REG_ECX: s, UC_X86_REG_EDX: INFO})
    reject = {"CLGU": 0x7E3F14, "CLGE": 0x7E4373, "CLVB": 0x7E4189}[sid]
    if s >= published:
        e.run(start, reject, regs)
        assert e.uc.reg_read(UC_X86_REG_ESP) == STACK + 0x80000
        return
    e.run(start, end, regs)
    reg = UC_X86_REG_ESI if sid == "CLVB" else UC_X86_REG_ECX
    assert e.uc.reg_read(reg) == DATA + 0x40000 + (8 * ghost + s) * 0x200
