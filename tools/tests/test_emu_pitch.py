"""[neck] native_pitch (issue #18 stage 5 area 3, core/pitch8.h) in Unicorn on the game snapshot.

Three emulators: stock (no site written), `cur` (today's default build: every site but the P* ones,
string 6 through S1/S2/S3/I1) and `nat` (also the P* sites, g_rs7_native_pitch = 1, _msize replaced by
a stub that reports SIZE). Six-string inputs must give stock results in `nat` (ports and tails are
equivalence-checked against the stock code); seven-string inputs must give what `cur` returns, eight
strings what the native formula gives.
"""

from __future__ import annotations

import struct

import pytest
from unicorn import UC_HOOK_MEM_READ
from unicorn.x86_const import (
    UC_X86_REG_EAX,
    UC_X86_REG_EBP,
    UC_X86_REG_EBX,
    UC_X86_REG_ECX,
    UC_X86_REG_EDI,
    UC_X86_REG_EDX,
    UC_X86_REG_EIP,
    UC_X86_REG_ESI,
    UC_X86_REG_ESP,
)

from test_emu import ALL_REGS, CODE, DATA, RET_MAGIC, STACK, Emu, site_va

MSIZE_CODE = CODE + 0x1000
MSIZE_VAL = DATA + 0xE0000
DET = DATA + 0x10000  # detector (13E0h)
MAT = DATA + 0x20000  # matcher (6Ch)
VEC = DATA + 0x30000  # std::vector<int16>: begin, end; data at VEC + 0x100
DET_INSTR, DET_LOWEST, DET_TUNING, DET_OPEN = 0x11EC, 0x11F8, 0x1340, 0x134C
DET_T8, DET_O8, DET_COUNT, DET_MAGIC, DET_SIZE = 0x13B8, 0x13C8, 0x13D8, 0x13DC, 0x13E0
MAT_T8, MAT_MAGIC, MAT_SIZE = 0x58, 0x68, 0x6C
MAGIC = 0x38503752
EXPECT_VEC, EXPECT_RAW = 0x873E90, 0x873F80
FAIL_433650 = 0x433650


class NatEmu(Emu):
    native_pitch = True


@pytest.fixture(scope="module")
def emus3():
    stock, cur, nat = Emu(patched=False), Emu(patched=True), NatEmu(patched=True)
    nat.uc.mem_write(nat.exports["g_rs7_native_pitch"], struct.pack("<I", 1))
    nat.uc.mem_write(MSIZE_CODE, bytes([0xA1]) + struct.pack("<I", MSIZE_VAL) + b"\xc3")  # mov eax,[val]; ret
    nat.uc.mem_write(nat.exports["g_rs7_fn_msize"], struct.pack("<I", MSIZE_CODE))
    set_msize(nat, 0x10000)
    return stock, cur, nat


def set_msize(emu: Emu, size: int):
    emu.uc.mem_write(MSIZE_VAL, struct.pack("<I", size))


def put_vec(emu: Emu, tuning: list[int]):
    data = VEC + 0x100
    emu.uc.mem_write(data, struct.pack(f"<{len(tuning)}h", *tuning))
    emu.uc.mem_write(VEC, struct.pack("<II", data, data + 2 * len(tuning)))


def i32(v: int) -> int:
    return struct.unpack("<i", struct.pack("<I", v & 0xFFFFFFFF))[0]


# ---- PE0: 0x873E90 port (vector) ----------------------------------------------------------------


def call_vec(emu: Emu, instrument: int, string: int, fret: int, tuning: list[int], until=RET_MAGIC):
    put_vec(emu, tuning)
    esp = STACK + 0x80000
    emu.uc.mem_write(esp, struct.pack("<III", RET_MAGIC, fret, VEC))
    emu.run(
        EXPECT_VEC,
        until,
        {
            UC_X86_REG_ESP: esp,
            UC_X86_REG_EAX: instrument,
            UC_X86_REG_EDX: string,
            UC_X86_REG_ECX: 0xC1C1,
            UC_X86_REG_ESI: 0x5151,
            UC_X86_REG_EBP: STACK + 0x90000,
            UC_X86_REG_EBX: 0xB1B1,
            UC_X86_REG_EDI: 0xD1D1,
        },
    )
    return emu.regs(*ALL_REGS)


TUNINGS6 = [[0] * 6, [-2, 0, 0, 0, 0, 0], [-2] * 6, [3, -1, 0, 2, -5, 1]]


@pytest.mark.parametrize("instrument", [0, 1, 2])
@pytest.mark.parametrize("string", range(6))
@pytest.mark.parametrize("tuning", TUNINGS6 + [[0] * 7, [-1] * 8], ids=str)
def test_pe0_strings_0_to_5_equal_stock(emus3, instrument, string, tuning):
    stock, _, nat = emus3
    for fret in (0, 3, 12):
        assert call_vec(nat, instrument, string, fret, tuning) == call_vec(stock, instrument, string, fret, tuning)


@pytest.mark.parametrize("instrument", [0, 1])
@pytest.mark.parametrize("t6", [0, -1, -2, -5, 3])
@pytest.mark.parametrize("fret", [0, 3, 7, 24])
def test_pe0_string_6_of_a_7_entry_vector_equals_the_current_s2(emus3, instrument, t6, fret):
    _, cur, nat = emus3
    tuning = [0, 0, 0, 0, 0, 0, t6]
    got = call_vec(nat, instrument, 6, fret, tuning)
    assert got[0] == call_vec(cur, instrument, 6, fret, tuning)[0] == 35 + t6 + fret


@pytest.mark.parametrize("string,base", [(6, 35), (7, 30)])
def test_pe0_eight_entries_use_the_native_bases(emus3, string, base):
    _, _, nat = emus3
    tuning = [0, 0, 0, 0, 0, 0, -1, -2]
    assert i32(call_vec(nat, 1, string, 5, tuning)[0]) == base + tuning[string] + 5


def test_pe0_bass_strings_4_5_keep_base_minus_one(emus3):
    stock, _, nat = emus3
    for s in (4, 5):
        assert call_vec(nat, 2, s, 3, [1, 2, 3, 4, 5, 6]) == call_vec(stock, 2, s, 3, [1, 2, 3, 4, 5, 6])


@pytest.mark.parametrize("string,n", [(6, 6), (7, 7), (8, 8), (9, 7)])
def test_pe0_string_past_the_vector_takes_the_stock_failure_path(emus3, string, n):
    stock, _, nat = emus3
    want = call_vec(stock, 1, string, 3, [0] * n, until=FAIL_433650)
    got = call_vec(nat, 1, string, 3, [0] * n, until=FAIL_433650)
    assert (got[0], got[1]) + got[3:] == (want[0], want[1]) + want[3:]  # all but ECX (the base: 35 / -1)
    esp = got[7]
    assert nat.read(esp, 8) == stock.read(esp, 8)  # return 0x873F13 + the 119C8A8h argument


# ---- Rs7_Pitch_Raw8: 0x873F80 port (raw int16 pointer) -------------------------------------------


def call_raw(emu: Emu, start: int, ptr: int, instrument: int, string: int, fret: int):
    esp = STACK + 0x80000
    emu.uc.mem_write(esp, struct.pack("<II", RET_MAGIC, fret))
    emu.run(
        start,
        RET_MAGIC,
        {
            UC_X86_REG_ESP: esp,
            UC_X86_REG_ECX: ptr,
            UC_X86_REG_EDX: instrument,
            UC_X86_REG_ESI: string,
            UC_X86_REG_EBP: STACK + 0x90000,
            UC_X86_REG_EAX: 0xA1A1,
            UC_X86_REG_EBX: 0xB1B1,
            UC_X86_REG_EDI: 0xD1D1,
        },
    )
    return emu.regs(*ALL_REGS)


RAW = DATA + 0x40000


@pytest.mark.parametrize("instrument", [0, 1, 2, 3])
@pytest.mark.parametrize("string", list(range(6)) + [8, 9, 0xFFFFFFFF])
def test_raw8_equals_stock_on_six_entries(emus3, instrument, string):
    stock, _, nat = emus3
    tuning = [3, -1, 0, 2, -5, 1, 7, 9]
    for emu in (stock, nat):
        emu.uc.mem_write(RAW, struct.pack("<8h", *tuning))
    raw8 = nat.exports["Rs7_Pitch_Raw8"]
    for fret in (0, 5):
        assert call_raw(nat, raw8, RAW, instrument, string, fret) == call_raw(
            stock, EXPECT_RAW, RAW, instrument, string, fret
        )
        assert call_raw(nat, raw8, 0, instrument, string, fret) == call_raw(
            stock, EXPECT_RAW, 0, instrument, string, fret
        )


def test_raw8_strings_6_7_read_the_tail(emus3):
    _, _, nat = emus3
    nat.uc.mem_write(RAW, struct.pack("<8h", 0, 0, 0, 0, 0, 0, -2, 1))
    raw8 = nat.exports["Rs7_Pitch_Raw8"]
    assert call_raw(nat, raw8, RAW, 1, 6, 4)[0] == 35 - 2 + 4
    assert call_raw(nat, raw8, RAW, 0, 7, 4)[0] == 30 + 1 + 4
    assert i32(call_raw(nat, raw8, RAW, 2, 6, 4)[0]) == 3  # bass: stock fret - 1


# ---- PC1-PC4: the 0x873F80 calls with a matcher / detector pointer --------------------------------

PC_OFF = {"PC1": (MAT, 0x44), "PC2": (MAT, 0x44), "PC3": (DET, 0x1340), "PC4": (MAT, 0x44)}


def native_objects(emu: Emu, t8: list[int], magic=MAGIC):
    emu.uc.mem_write(MAT + MAT_T8, struct.pack("<8h", *t8))
    emu.uc.mem_write(MAT + MAT_MAGIC, struct.pack("<I", magic))
    emu.uc.mem_write(DET + DET_T8, struct.pack("<8h", *t8))
    emu.uc.mem_write(DET + DET_MAGIC, struct.pack("<I", magic))
    emu.uc.mem_write(MAT + 0x44, struct.pack("<6h", *t8[:6]))
    emu.uc.mem_write(DET + DET_TUNING, struct.pack("<6h", *t8[:6]))


@pytest.mark.parametrize("sid", sorted(PC_OFF))
@pytest.mark.parametrize("string", range(6))
def test_pc_strings_0_to_5_run_the_stock_function(emus3, sid, string):
    stock, _, nat = emus3
    base, off = PC_OFF[sid]
    t8 = [3, -1, 0, 2, -5, 1, -2, 4]
    native_objects(nat, t8)
    native_objects(stock, t8)
    stub = nat.exports[f"Rs7_Stub_{sid}"]
    assert call_raw(nat, stub, base + off, 1, string, 7) == call_raw(stock, EXPECT_RAW, base + off, 1, string, 7)


@pytest.mark.parametrize("sid", sorted(PC_OFF))
@pytest.mark.parametrize("t6", [0, -2, 3])
def test_pc_string_6_of_a_native_object_is_the_current_hook_value(emus3, sid, t6):
    """Current hooks (S1 with an active side-store entry, test_mode.cpp): 35 + t6 + fret."""
    _, _, nat = emus3
    base, off = PC_OFF[sid]
    native_objects(nat, [0, 0, 0, 0, 0, 0, t6, -1])
    regs = call_raw(nat, nat.exports[f"Rs7_Stub_{sid}"], base + off, 1, 6, 5)
    assert regs[0] == 35 + t6 + 5
    assert regs[2] == base + off  # ECX restored for the caller
    assert regs[3] == 0xFFFFFFFF  # EDX = instrument - 2, as the stock function leaves it
    assert i32(call_raw(nat, nat.exports[f"Rs7_Stub_{sid}"], base + off, 1, 7, 5)[0]) == 30 - 1 + 5


@pytest.mark.parametrize("sid", sorted(PC_OFF))
@pytest.mark.parametrize("case", ["no_magic", "stock_size"])
def test_pc_without_a_tail_or_null_goes_to_the_stock_function(emus3, sid, case):
    """stock_size: an object allocated before PA1/PA2 (the magic bytes past its block happen to match):
    _msize says stock size, so the stub must not use (or read) anything past it."""
    _, _, nat = emus3
    base, off = PC_OFF[sid]
    native_objects(nat, [0] * 8, magic=0 if case == "no_magic" else MAGIC)
    if case == "stock_size":
        set_msize(nat, 0x13B8 if base == DET else 0x58)
    assert nat.uc.mem_read(nat.exports[f"g_rs7_target_{sid}"], 4) == struct.pack("<I", EXPECT_RAW)
    # The stock target is swapped for a never-translated sentinel: Unicorn bakes `until` into translated
    # blocks, and the block at 0x873F80 is cached from earlier calls.
    sentinel = CODE + 0x2000
    var = nat.exports[f"g_rs7_target_{sid}"]
    nat.uc.mem_write(var, struct.pack("<I", sentinel))
    stock_end = base + (0x13B8 if base == DET else 0x58)
    past = []
    hook = nat.uc.hook_add(
        UC_HOOK_MEM_READ, lambda uc, acc, addr, size, val, u: past.append(addr), begin=stock_end, end=base + 0x2000
    )
    try:
        for ptr in (base + off, 0):
            esp = STACK + 0x80000
            nat.uc.mem_write(esp, struct.pack("<II", RET_MAGIC, 5))
            nat.run(
                nat.exports[f"Rs7_Stub_{sid}"],
                sentinel,
                {UC_X86_REG_ESP: esp, UC_X86_REG_ECX: ptr, UC_X86_REG_EDX: 1, UC_X86_REG_ESI: 6},
            )
            assert nat.regs(UC_X86_REG_ESP, UC_X86_REG_ECX) == (esp, ptr)
    finally:
        nat.uc.mem_write(var, struct.pack("<I", EXPECT_RAW))
        set_msize(nat, 0x10000)
        nat.uc.hook_del(hook)
    if case == "stock_size":
        assert past == []  # nothing past the stock block was read


def test_pc_sites_call_0x873f80(emus3):
    _, _, nat = emus3
    for sid in PC_OFF:
        va = site_va(sid)
        assert nat.read(va, 1) == b"\xe8"


# ---- detector SetTuning 0x4E2590 (S3 guitar exit, PT1 bass exit) ---------------------------------


def run_set_tuning(emu: Emu, tuning: list[int], instrument: int):
    put_vec(emu, tuning)
    det = bytearray((i * 7 + 3) & 0xFF for i in range(DET_SIZE))
    det[DET_INSTR : DET_INSTR + 4] = struct.pack("<I", instrument)
    emu.uc.mem_write(DET, bytes(det))
    esp = STACK + 0x80000
    emu.uc.mem_write(esp, struct.pack("<I", RET_MAGIC))
    emu.run(
        0x4E2590,
        RET_MAGIC,
        {
            UC_X86_REG_ESP: esp,
            UC_X86_REG_EAX: DET,
            UC_X86_REG_ESI: VEC,
            UC_X86_REG_EBX: 0xB1B1,
            UC_X86_REG_EDI: 0xD1D1,
            UC_X86_REG_EBP: 0x1111,
        },
    )
    return emu.read(DET, DET_SIZE), emu.regs(*ALL_REGS), emu.fpu_top()


def tail(det: bytes):
    t8 = list(struct.unpack_from("<8h", det, DET_T8))
    o8 = list(struct.unpack_from("<8h", det, DET_O8))
    return t8, o8, struct.unpack_from("<II", det, DET_COUNT)


SET_TUNINGS = [
    [0] * 6,
    [-2, 0, 0, 0, 0, 0],
    [3, -1, 0, 2, -5, 1],
    [0] * 7,
    [0, 0, 0, 0, 0, 0, -2],
    [-2] * 7,
    [0] * 8,
    [0, 0, 0, 0, 0, 0, -1, -3],
    [0] * 9,
]


@pytest.mark.parametrize("instrument", [0, 1, 2])
@pytest.mark.parametrize("tuning", SET_TUNINGS, ids=str)
def test_set_tuning_stock_part_equals_the_current_build(emus3, tuning, instrument):
    stock, cur, nat = emus3
    want, got = run_set_tuning(cur, tuning, instrument), run_set_tuning(nat, tuning, instrument)
    assert got[1:] == want[1:]  # registers, FPU
    if len(tuning) >= 8 and instrument != 2:  # slot 7 also lowers (cur ignores 8+)
        assert struct.unpack_from("<i", got[0], DET_LOWEST)[0] == min(
            struct.unpack_from("<i", want[0], DET_LOWEST)[0], 35 + tuning[6], 30 + tuning[7]
        )
        g, w = bytearray(got[0][:DET_T8]), bytearray(want[0][:DET_T8])
        g[DET_LOWEST : DET_LOWEST + 4] = w[DET_LOWEST : DET_LOWEST + 4]
        assert g == w
    else:
        assert got[0][:DET_T8] == want[0][:DET_T8]  # every stock byte as today
    if len(tuning) == 6:
        assert run_set_tuning(stock, tuning, instrument)[0][:DET_T8] == got[0][:DET_T8]


@pytest.mark.parametrize("instrument", [0, 1, 2])
@pytest.mark.parametrize("tuning", SET_TUNINGS, ids=str)
def test_set_tuning_tail(emus3, tuning, instrument):
    _, _, nat = emus3
    det = run_set_tuning(nat, tuning, instrument)[0]
    t8, o8, (count, magic) = tail(det)
    n = min(len(tuning), 8)
    assert t8 == tuning[:n] + [0] * (8 - n)
    stock_open = list(struct.unpack_from("<6h", det, DET_OPEN))
    assert o8[:6] == stock_open  # slots 0..5 = the stock array
    if instrument == 2:
        assert o8[6:] == t8[6:]
    else:
        assert o8[6:] == [35 + t8[6], 30 + t8[7]]
    assert (count, magic) == ((4 if instrument == 2 else 6), MAGIC)


def test_set_tuning_small_block_is_left_alone(emus3):
    _, cur, nat = emus3
    set_msize(nat, 0x13B8)
    try:
        got = run_set_tuning(nat, [0, 0, 0, 0, 0, 0, -2], 1)
    finally:
        set_msize(nat, 0x10000)
    assert got == run_set_tuning(cur, [0, 0, 0, 0, 0, 0, -2], 1)


# ---- detector reset 0x4DBF20 up to the join (PR0) ------------------------------------------------


def run_reset(emu: Emu, instrument: int, until: int):
    det = bytearray((i * 11 + 5) & 0xFF for i in range(DET_SIZE))
    owner = DATA + 0x50000
    det[0x1238:0x123C] = struct.pack("<I", owner)
    emu.uc.mem_write(owner + 0x64, struct.pack("<I", instrument))
    emu.uc.mem_write(DET, bytes(det))
    esp = STACK + 0x80000
    emu.uc.mem_write(esp, struct.pack("<II", RET_MAGIC, DET))
    emu.run(
        0x4DBF20,
        until,
        {
            UC_X86_REG_ESP: esp,
            UC_X86_REG_EBP: STACK + 0x90000,
            UC_X86_REG_EBX: 0xB1B1,
            UC_X86_REG_ESI: 0x5151,
            UC_X86_REG_EDI: 0xD1D1,
        },
    )
    return emu.read(DET, DET_SIZE), emu.regs(*ALL_REGS), emu.fpu_top()


@pytest.mark.parametrize("instrument", [0, 1, 2])
def test_reset_tail(emus3, instrument):
    stock, _, nat = emus3
    join = site_va("PR0")
    assert join == 0x4DC07B
    want = run_reset(stock, instrument, join + 6)  # after the replayed mov eax,[ebx+11F0h]
    got = run_reset(nat, instrument, join + 6)
    assert got[1:] == want[1:]
    assert got[0][:DET_T8] == want[0][:DET_T8]
    t8, o8, (count, magic) = tail(got[0])
    assert t8 == [0] * 8
    assert o8[:6] == list(struct.unpack_from("<6h", got[0], DET_OPEN))
    assert o8[6:] == ([0, 0] if instrument == 2 else [35, 30])
    assert (count, magic) == ((4 if instrument == 2 else 6), MAGIC)


# ---- matcher: PM0 (ctor) and PM1 (SetTuning 0x4E85A0) --------------------------------------------


def test_pm0_matcher_ctor_tail(emus3):
    _, _, nat = emus3
    nat.uc.mem_write(MAT, b"\xcd" * MAT_SIZE)
    esp = STACK + 0x80000
    resume = site_va("PM0") + 7
    nat.run(nat.exports["Rs7_Stub_PM0"], resume, {UC_X86_REG_ESP: esp, UC_X86_REG_EAX: MAT, UC_X86_REG_ECX: 0})
    m = nat.read(MAT, MAT_SIZE)
    assert struct.unpack_from("<i", m, 0x40)[0] == -1
    assert m[MAT_T8 : MAT_T8 + 16] == bytes(16)
    assert struct.unpack_from("<I", m, MAT_MAGIC)[0] == MAGIC
    assert nat.regs(UC_X86_REG_EAX, UC_X86_REG_ESP) == (MAT, esp)


def run_matcher(emu: Emu, tuning: list[int], instrument: int):
    put_vec(emu, tuning)
    m = bytearray((i * 3 + 1) & 0xFF for i in range(MAT_SIZE))
    m[4:8] = struct.pack("<I", DET)
    emu.uc.mem_write(MAT, bytes(m))
    emu.uc.mem_write(DET + DET_INSTR, struct.pack("<I", instrument))
    esp = STACK + 0x80000
    emu.uc.mem_write(esp, struct.pack("<I", RET_MAGIC))
    emu.run(
        0x4E85A0,
        RET_MAGIC,
        {
            UC_X86_REG_ESP: esp,
            UC_X86_REG_ESI: MAT,
            UC_X86_REG_EDX: VEC,
            UC_X86_REG_EBP: STACK + 0x90000,
            UC_X86_REG_EBX: 0xB1B1,
            UC_X86_REG_EDI: 0xD1D1,
        },
    )
    return emu.read(MAT, MAT_SIZE), emu.regs(*ALL_REGS), emu.fpu_top()


@pytest.mark.parametrize("instrument", [0, 1, 2])
@pytest.mark.parametrize("tuning", SET_TUNINGS + [[0, 0, -2, -2, 0, 0], [1, 1, 1, 1, 0, 0]], ids=str)
def test_matcher_set_tuning(emus3, tuning, instrument):
    stock, _, nat = emus3
    want, got = run_matcher(stock, tuning, instrument), run_matcher(nat, tuning, instrument)
    assert got[1:] == want[1:]  # registers, FPU
    assert got[0][:MAT_T8] == want[0][:MAT_T8]  # tolerance +50h, flag +54h as stock
    n = min(len(tuning), 8)
    assert list(struct.unpack_from("<8h", got[0], MAT_T8)) == tuning[:n] + [0] * (8 - n)
    assert struct.unpack_from("<I", got[0], MAT_MAGIC)[0] == MAGIC
