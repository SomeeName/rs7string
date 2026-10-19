"""Compiled allocation guards, inverse and zero-table ABIs; parent runs after build."""

import struct
import pytest
from unicorn import UC_HOOK_MEM_READ
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESI, UC_X86_REG_ESP
from emu_harness import Emu, CODE, DATA, STACK, RET_MAGIC, callin_entry, site_mid_entry

DET = DATA + 0x10000
MAT = DATA + 0x20000
TRACK = DATA + 0x30000


def p(*x):
    return struct.pack("<" + "I" * len(x), *[v & 0xFFFFFFFF for v in x])


@pytest.fixture(scope="module")
def machine():
    e = Emu(False)
    if "Rs7PublishPitch_Target" not in e.exports:
        pytest.skip("needs publish-pitch build")
    e.uc.mem_write(e.exports["g_rs7_native_pitch"], p(1))
    e.uc.mem_write(e.exports["g_rs7_publish_ports"], p(1))
    # cdecl _msize returns a programmable allocation size, with no external CRT calls.
    e.uc.mem_write(CODE + 0x1000, b"\xa1" + p(DATA + 0xF0000) + b"\xc3")
    e.uc.mem_write(e.exports["g_rs7_fn_msize"], p(CODE + 0x1000))
    e.uc.mem_write(MAT + 4, p(DET))
    e.uc.mem_write(TRACK + 0x4C19C, p(MAT))
    e.uc.mem_write(DET + 0x11EC, p(0))
    e.uc.mem_write(DET + 0x13DC, p(0x38503752))
    e.uc.mem_write(DET + 0x13D8, p(8))
    for i, name in enumerate(("PUBNL", "PUBAN", "PUBAC")):
        e.uc.mem_write(e.exports["g_rs7_pitchcopy_" + name], p(CODE + 0x2000 + 0x100 * i))
        e.uc.mem_write(e.exports["g_rs7_pitchtramp_" + name], p(CODE + 0x3000 + 0x100 * i))
    return e


def detector_block(e, size: int):
    """DET as the DLL's reset (PR0, Rs7Pitch_DetReset) leaves it in a block of `size` bytes: the native size writes the
    tail and records DET in the registry of tailed allocations, the stock size forgets it. Then an 8-string count and
    the magic (also past a stock block, where they must never be trusted)."""
    e.uc.mem_write(DATA + 0xF0000, p(size))
    sp = STACK + 0x80000
    e.uc.mem_write(sp, p(RET_MAGIC, DET))
    e.run(e.exports["Rs7Pitch_DetReset"], RET_MAGIC, {UC_X86_REG_ESP: sp})
    e.uc.mem_write(DET + 0x13DC, p(0x38503752))
    e.uc.mem_write(DET + 0x13D8, p(8))


@pytest.mark.parametrize("kind", range(3))
@pytest.mark.parametrize("size", [0x13B8, 0x13E0])
def test_guard_never_reads_stock_tail(machine, kind, size):
    e = machine
    detector_block(e, size)
    reads = []
    hook = e.uc.hook_add(
        UC_HOOK_MEM_READ, lambda uc, a, addr, n, v, u: reads.append(addr), begin=DET + 0x13B8, end=DET + 0x13DF
    )
    sp = STACK + 0x80000
    e.uc.mem_write(sp, p(RET_MAGIC, kind, MAT if kind == 0 else TRACK))
    try:
        e.run(e.exports["Rs7PublishPitch_Target"], RET_MAGIC, {UC_X86_REG_ESP: sp})
    finally:
        e.uc.hook_del(hook)
    assert e.uc.reg_read(UC_X86_REG_EAX) == CODE + (0x2000 if size >= 0x13E0 else 0x3000) + 0x100 * kind
    if size == 0x13B8:
        assert reads == []


@pytest.mark.parametrize("s,base", [(6, 35), (7, 30)])
def test_inverse_native_tail(machine, s, base):
    e = machine
    e.uc.mem_write(DET + 0x13B8, struct.pack("<8h", *([0] * 6 + [-2, -3])))
    sp = STACK + 0x80000
    e.uc.mem_write(sp, p(RET_MAGIC, 0))
    e.run(
        callin_entry(e.uc, e.exports, "g_rs7_callin_ReverseTail", CODE + 0xD1000, STACK + 0x60000, RET_MAGIC),
        RET_MAGIC,
        {
            UC_X86_REG_ESP: sp,
            UC_X86_REG_ECX: base + 8 - (2 if s == 6 else 3),
            UC_X86_REG_EDX: s,
            UC_X86_REG_ESI: DET + 0x13B8,
        },
    )
    assert e.uc.reg_read(UC_X86_REG_EAX) == 8 and e.uc.reg_read(UC_X86_REG_ESP) == sp + 4


@pytest.mark.parametrize("sid", ["PUBZ0", "PUBZ1"])  # a call site and a jmp site (after the caller's `pop ebp`)
@pytest.mark.parametrize("native", [0, 1])
@pytest.mark.parametrize("instrument,s", [(0, 3), (0, 6), (0, 7), (1, 6), (2, 3), (2, 6)])
def test_zero_table_call_and_tail_return(machine, sid, native, instrument, s):
    """The PUBZ mid-hook (Rs7_Mid_PUBZ, light) with the stack both kinds of site have: [esp] = the caller's return
    address, [esp+4] = the fret. Guitar strings 6/7 with the ports: the eight-entry bases; else the stock 0x873F80."""
    e = machine
    e.uc.mem_write(e.exports["g_rs7_publish_ports"], p(native))
    sp = STACK + 0x80000
    start = site_mid_entry(e.uc, e.exports, sid, CODE + 0xD0000, STACK + 0x60000, RET_MAGIC)
    e.uc.mem_write(sp, p(RET_MAGIC, 5))
    e.run(
        start, RET_MAGIC, {UC_X86_REG_ESP: sp, UC_X86_REG_ECX: 0x1199D2C, UC_X86_REG_EDX: instrument, UC_X86_REG_ESI: s}
    )
    if instrument == 2:
        base = [28, 33, 38, 43][s] if s < 4 else -1
    else:
        base = [40, 45, 50, 55, 59, 64, 35, 30][s] if native or s < 6 else -1
    assert e.uc.reg_read(UC_X86_REG_EAX) == base + 5
    assert e.uc.reg_read(UC_X86_REG_ESP) == sp + 4


@pytest.mark.parametrize("kind,name", enumerate(("PUBNL", "PUBAN", "PUBAC")))
@pytest.mark.parametrize("native", [0, 1])
def test_entry_tail_transfer_preserves_original_stack(machine, kind, name, native):
    e = machine
    e.uc.mem_write(e.exports["g_rs7_publish_ports"], p(native))
    detector_block(e, 0x13E0)
    sp = STACK + 0x80000
    obj = MAT if kind == 0 else TRACK
    e.uc.mem_write(sp, p(RET_MAGIC, obj, DATA + 0x40000, DATA + 0x50000))
    until = CODE + (0x2000 if native else 0x3000) + 0x100 * kind
    # The mid-hook entry (built before the registers are set: the emitter runs in the same emulator).
    start = site_mid_entry(e.uc, e.exports, name, CODE + 0xC0000 + 0x100 * kind, STACK + 0x60000, RET_MAGIC)
    top = e.fpu_top()
    e.run(
        start,
        until,
        {
            UC_X86_REG_ESP: sp,
            UC_X86_REG_EAX: 0x11223344,
            UC_X86_REG_ECX: 0x55667788,
            UC_X86_REG_EDX: 0x99AABBCC,
            UC_X86_REG_ESI: 0x12345678,
        },
    )
    assert e.uc.reg_read(UC_X86_REG_ESP) == sp
    assert e.uc.reg_read(UC_X86_REG_EAX) == 0x11223344
    assert e.uc.reg_read(UC_X86_REG_ECX) == 0x55667788
    assert e.uc.reg_read(UC_X86_REG_EDX) == 0x99AABBCC
    assert e.uc.reg_read(UC_X86_REG_ESI) == 0x12345678
    assert e.fpu_top() == top
