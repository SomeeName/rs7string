"""Publication grid DLL stubs: real native cells, rejected queries and guard boundaries.
Parent runs after building; no game launch. Uses existing snapshot emulator memory only.
"""

import struct
import pytest
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import *
from test_emu import Emu, DATA, STACK, RET_MAGIC, DLL, SNAPSHOT

pytestmark = pytest.mark.skipif(not DLL.exists() or not (SNAPSHOT / "memory.bin").exists(), reason="needs DLL/snapshot")


@pytest.fixture(scope="module")
def emu():
    e = Emu(False)
    e.uc.mem_write(e.exports["g_rs7_fg_row"], struct.pack("<I", 0x264))  # the deferred FretGrid layout is written
    e.uc.mem_write(e.exports["g_rs7_native_fretgrid"], struct.pack("<I", 1))
    return e


def put(e, addr, *words):
    e.uc.mem_write(addr, struct.pack("<" + "I" * len(words), *words))


@pytest.mark.parametrize("count", [6, 7, 8])
def test_native_queries_and_invalid_indices(emu, count):
    e = emu
    grid = DATA
    guitar = DATA + 0xB000
    info = DATA + 0xC000
    out = DATA + 0xD000
    esp = STACK + 0x80000
    put(e, grid, guitar)
    put(e, guitar + 0x24, info)
    e.uc.mem_write(info + 0xB, bytes([count]))
    put(e, e.exports["g_rs7_native_publish"], 1)
    for string, k, fret in [(s, k, 26) for s in range(count) for k in range(3)] + [
        (count, 0, 26),
        (0, 3, 26),
        (0, 0, 0),
        (0, 0, 27),
    ]:
        good = string < count and k < 3 and 1 <= fret <= 26
        if good:
            e.uc.mem_write(grid + 0x30 + 25 * 0x264 + string * 0x4C + k * 12, struct.pack("<fff", string, k, 123))
        e.uc.mem_write(out, struct.pack("<fff", -1, -2, -3))
        put(e, esp, RET_MAGIC, grid, k, string)
        e.run(e.exports["Rs7_Stub_PUBGP"], RET_MAGIC, {UC_X86_REG_ESP: esp, UC_X86_REG_EAX: fret, UC_X86_REG_ECX: out})
        assert e.uc.reg_read(UC_X86_REG_EAX) & 255 == int(good)
        assert struct.unpack("<fff", e.uc.mem_read(out, 12)) == (
            (float(string), float(k), 123.0) if good else (-1.0, -2.0, -3.0)
        )


@pytest.mark.parametrize(
    "sid,reg,info_reg",
    [
        ("PUBGS", UC_X86_REG_EAX, UC_X86_REG_EDI),
        ("PUBGE1", UC_X86_REG_EDX, UC_X86_REG_ESI),
        ("PUBGE2", UC_X86_REG_EBX, UC_X86_REG_ESI),
    ],
)
@pytest.mark.parametrize("count", [6, 7, 8])
def test_active_guard(emu, sid, reg, info_reg, count):
    e = emu
    info = DATA + 0xC000
    e.uc.mem_write(info + 0xB, bytes([count]))
    put(e, e.exports["g_rs7_resume_" + sid], RET_MAGIC)
    put(e, e.exports["g_rs7_target_" + sid], RET_MAGIC + 16)
    for s in range(10):
        hit = []

        def stop(uc, addr, size, user):
            if addr in [RET_MAGIC, RET_MAGIC + 16]:
                hit.append(addr)
                uc.emu_stop()

        h = e.uc.hook_add(UC_HOOK_CODE, stop)
        e.uc.reg_write(reg, s)
        e.uc.reg_write(info_reg, info)
        e.uc.reg_write(UC_X86_REG_ESP, STACK + 0x80000)
        e.uc.emu_start(e.exports["Rs7_Stub_" + sid], RET_MAGIC + 32, count=100)
        e.uc.hook_del(h)
        assert hit == [RET_MAGIC if s < count else RET_MAGIC + 16]


def test_grid_build_stages_the_world_cells_of_strings_6_and_7(emu):
    e = emu
    grid = DATA
    guitar = DATA + 0xB000
    info = DATA + 0xC000
    gs = DATA + 0xE000
    esp = STACK + 0x80000
    put(e, grid, guitar)
    put(e, guitar + 0x10, grid)
    put(e, guitar + 0x24, info)
    put(e, guitar + 0xC, gs)
    e.uc.mem_write(info + 0xB, bytes([6, 1]))
    e.uc.mem_write(guitar + 0x2C, struct.pack("<fff", 10, 20, 30))
    for s in [0, 1]:
        c = grid + 8 + s * 0x4C
        e.uc.mem_write(c, b"\x01")
        e.uc.mem_write(c + 4, struct.pack("<9f", *[float(s * 4 + i) for i in range(9)]))
    for s in [6, 7]:
        child = DATA + 0x20000 + s * 0x1000
        entity = DATA + 0x40000 + s * 0x1000
        put(e, gs + 0x40 + s * 4, child)
        put(e, child + 4, entity)
        put(e, child + 0x10C, 1)
        e.uc.mem_write(entity + 0xD8, b"\x01")
        put(e, entity + 0xDC, 1)
    oldready = bytes(e.uc.mem_read(e.exports["Rs7Gs_ExtraReady"], 6))
    oldroot = e.read(0x411130, 1)  # e.read maps the snapshot page
    e.uc.mem_write(e.exports["Rs7Gs_ExtraReady"], bytes.fromhex("B8 01 00 00 00 C3"))
    e.uc.mem_write(0x411130, b"\xc3")

    def call(name, *args):
        put(e, esp, RET_MAGIC, *args)
        e.run(e.exports[name], RET_MAGIC, {UC_X86_REG_ESP: esp})
        return e.uc.reg_read(UC_X86_REG_EAX)

    try:
        assert call("Rs7Publish_GridBuild", grid, guitar) == 2
        # Sixth extension local z=-2, world z=28; eighth extension local z=-6, worldz24.
        assert struct.unpack("<fff", e.uc.mem_read(grid + 0x30 + 6 * 0x4C, 12)) == (6.0, 17.0, 28.0)
        assert struct.unpack("<fff", e.uc.mem_read(grid + 0x30 + 7 * 0x4C, 12)) == (2.0, 13.0, 24.0)
    finally:
        e.uc.mem_write(e.exports["Rs7Gs_ExtraReady"], oldready)
        e.uc.mem_write(0x411130, oldroot)


@pytest.mark.parametrize("s,hi", [(0, 0), (0, 0x1A2B3C00), (5, 0x7FFFFF00), (8, 0x11111100), (255, 0)])
def test_attach_point_string_is_a_byte(emu, s, hi):
    """0x594530 takes a BYTE string; GSLD pushes a stale dword (0x59A5DD). Stock strings (low byte < 6) must reach the
    stock body whatever the upper bytes; low byte >= 8 is rejected (eax 0)."""
    e = emu
    esp = STACK + 0x80000
    put(e, e.exports["g_rs7_resume_PUBGA"], RET_MAGIC + 16)
    put(e, esp, RET_MAGIC, hi | s, DATA + 0xD000)
    hit = []

    def stop(uc, addr, size, user):
        if addr in (RET_MAGIC, RET_MAGIC + 16):
            hit.append(addr)
            uc.emu_stop()

    h = e.uc.hook_add(UC_HOOK_CODE, stop)
    try:
        e.uc.reg_write(UC_X86_REG_ESP, esp)
        e.uc.reg_write(UC_X86_REG_EAX, 0x55)
        e.uc.emu_start(e.exports["Rs7_Stub_PUBGA"], RET_MAGIC + 32, count=100)
    finally:
        e.uc.hook_del(h)
    if s < 6:
        assert hit == [RET_MAGIC + 16]
    else:
        assert hit == [RET_MAGIC] and e.uc.reg_read(UC_X86_REG_EAX) == 0


# ---- R1 (Rnd_FretGrid_GetCellPos 0x58FE00) restored coverage: the native cell lookup (PUBGP) ----
# Replaces the emulator tests of the removed string-6 extrapolation (test_r1_cell_pos,
# test_r1_string6_follows_the_native_fretgrid_row_stride, test_r1_fret_out_of_range_is_stock).
ROW = 0x264
CELL = 0x4C


def _r1_setup(e, count, rowstride=ROW):
    grid = DATA
    guitar = DATA + 0xB000
    info = DATA + 0xC000
    e.uc.mem_write(grid, bytes(0x5000))
    put(e, grid, guitar)
    put(e, guitar + 0x24, info)
    e.uc.mem_write(info + 0xB, bytes([count]))
    e.uc.mem_write(e.exports["g_rs7_fg_row"], struct.pack("<I", rowstride))
    return grid


def _r1_point(s, k, f):
    return (1.0 + 0.5 * k + 0.25 * s, -16.5 + 2.0 * k + f, 5.0 - 1.5 * s + 0.125 * k)


def _r1_fill(e, grid, count):
    for f in (1, 7, 26):
        for s in range(count):
            for k in range(3):
                e.uc.mem_write(grid + 0x30 + (f - 1) * ROW + s * CELL + k * 12, struct.pack("<3f", *_r1_point(s, k, f)))


def _r1_run(e, grid, fret, k, string):
    out = DATA + 0xD000
    esp = STACK + 0x80000
    e.uc.mem_write(out, struct.pack("<3I", 0xEEEEEEEE, 0xEEEEEEEE, 0xEEEEEEEE))
    # fret = AL with garbage above; string = a byte pushed as a dword with stale upper bytes (0x59A5DD)
    put(e, esp, RET_MAGIC, grid, k, 0xABCDEF00 | string)
    regs = {
        UC_X86_REG_ESP: esp,
        UC_X86_REG_EAX: 0x12345600 | fret,
        UC_X86_REG_ECX: out,
        UC_X86_REG_EDX: 0xD0D0,
        UC_X86_REG_EBX: 0xB1B1,
        UC_X86_REG_ESI: 0x5151,
        UC_X86_REG_EDI: 0xD1D1,
        UC_X86_REG_EBP: 0x1111,
    }
    e.run(e.exports["Rs7_Stub_PUBGP"], RET_MAGIC, regs)
    r = {
        n: e.uc.reg_read(c)
        for n, c in dict(
            eax=UC_X86_REG_EAX,
            ebx=UC_X86_REG_EBX,
            ecx=UC_X86_REG_ECX,
            edx=UC_X86_REG_EDX,
            esi=UC_X86_REG_ESI,
            edi=UC_X86_REG_EDI,
            ebp=UC_X86_REG_EBP,
            esp=UC_X86_REG_ESP,
        ).items()
    }
    return bytes(e.uc.mem_read(out, 12)), r


@pytest.mark.parametrize("fret", [1, 7, 26])
@pytest.mark.parametrize("k", [0, 1, 2])
@pytest.mark.parametrize("string", range(8))
def test_r1_cell_pos(emu, string, k, fret):
    """Every string of an 8-string build (incl. 6 and 7: real native cells, no extrapolation) at several frets,
    with garbage in the string's upper bytes; callee-saved registers and the 0Ch stack cleanup as stock."""
    e = emu
    grid = _r1_setup(e, 8)
    _r1_fill(e, grid, 8)
    pos, r = _r1_run(e, grid, fret, k, string)
    assert struct.unpack("<3f", pos) == _r1_point(string, k, fret)
    assert r["eax"] & 255 == 1
    assert (r["ebx"], r["ecx"], r["edx"], r["esi"], r["edi"], r["ebp"]) == (
        0xB1B1,
        DATA + 0xD000,
        0xD0D0,
        0x5151,
        0xD1D1,
        0x1111,
    )
    assert r["esp"] == STACK + 0x80010


@pytest.mark.parametrize("string", [0, 6, 7])
def test_r1_string_beyond_the_guitar_build_is_rejected(emu, string):
    """A 6-string build (info +0Bh = 6): string 6/7 has no cell, the query fails and writes nothing."""
    e = emu
    grid = _r1_setup(e, 6)
    _r1_fill(e, grid, 8)
    pos, r = _r1_run(e, grid, 7, 1, string)
    if string < 6:
        assert r["eax"] & 255 == 1 and struct.unpack("<3f", pos) == _r1_point(string, 1, 7)
    else:
        assert r["eax"] & 255 == 0 and pos == struct.pack("<3I", *[0xEEEEEEEE] * 3)


@pytest.mark.parametrize("fret", [0, 27, 0x80])
def test_r1_fret_out_of_range_is_rejected(emu, fret):
    e = emu
    grid = _r1_setup(e, 8)
    _r1_fill(e, grid, 8)
    _, r = _r1_run(e, grid, fret, 1, 3)
    assert r["eax"] & 255 == 0


def test_r1_stock_row_stride_is_rejected(emu):
    """The lookup serves the native 264h rows only; with the stock 1CCh stride (g_rs7_fg_row) it answers 0."""
    e = emu
    grid = _r1_setup(e, 8, 0x1CC)
    _r1_fill(e, grid, 8)
    try:
        _, r = _r1_run(e, grid, 7, 1, 3)
        assert r["eax"] & 255 == 0
    finally:
        e.uc.mem_write(e.exports["g_rs7_fg_row"], struct.pack("<I", ROW))
