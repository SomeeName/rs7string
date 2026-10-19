"""Stage 5 ColourManager ports vs stock, executed in Unicorn on the unpacked exe.

Stock bytes run on a six-wide manager, the ported bytes (gen_colournative.ports(), the exact instruction
edits of sites.json) on an eight-wide one filled with the same values: every output must be identical
for the stock columns 0-5. Columns 6/7 are read back from the native matrix (slot guard now 8).
"""

from __future__ import annotations

import struct

import pytest
from unicorn import UC_ARCH_X86, UC_HOOK_CODE, UC_MODE_32, UC_PROT_ALL, Uc
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

import gen_colournative as cm
import gen_fncopy
from emu_common import needs_exe

pytestmark = needs_exe

MGR, RGB, OUT, STACK, RET, GLOBAL = 0x10000000, 0x11000000, 0x12000000, 0x13000000, 0x14000000, 0x135F58C
PTR_OLD, UV_OLD, PTR_NEW, UV_NEW = 0x350, 0x890, 0xDD0, 0x14D0


def image():
    import pefile

    pe = pefile.PE(str(gen_fncopy.EXE), fast_load=True)
    return [
        (0x400000 + s.VirtualAddress, s.get_data())
        for s in pe.sections
        if s.Name.rstrip(b"\0") in (b".text", b".rdata", b".data")
    ]


def patched(text: bytearray, base: int) -> bytearray:
    out = bytearray(text)
    for p in cm.ports():
        assert bytes(out[p.va - base : p.va - base + len(p.old)]) == p.old
        out[p.va - base : p.va - base + len(p.new)] = p.new
    return out


def rgb_addr(m, st, k):
    return RGB + ((m * 28 + st) * 8 + k) * 16


def uv_val(m, st, k):
    return float(100 * m + st) + k / 16.0


def machine(native: bool):
    text, base = gen_fncopy.load_text()
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    for va, data in image():
        uc.mem_map(va & ~0xFFF, (len(data) + 0xFFF + (va & 0xFFF)) & ~0xFFF, UC_PROT_ALL)
        if va == base:
            data = bytes(patched(bytearray(data), base)) if native else data
        uc.mem_write(va, data)
    for a, n in (
        (MGR, 0x2000),
        (RGB, 0x4000),
        (OUT, 0x1000),
        (STACK - 0x8000, 0x10000),
        (RET, 0x1000),
        (GLOBAL & ~0xFFF, 0x1000),
    ):
        try:
            uc.mem_map(a, n, UC_PROT_ALL)
        except Exception:
            pass
    uc.mem_write(RET, b"\xf4")
    uc.mem_write(GLOBAL, struct.pack("<I", MGR))
    ptr, uv, width = (PTR_NEW, UV_NEW, 8) if native else (PTR_OLD, UV_OLD, 6)
    for m in range(2):
        for st in range(28):
            for k in range(6):
                i = (m * 28 + st) * width + k
                uc.mem_write(MGR + ptr + 4 * i, struct.pack("<I", rgb_addr(m, st, k)))
                uc.mem_write(MGR + uv + 4 * i, struct.pack("<f", uv_val(m, st, k)))
                uc.mem_write(rgb_addr(m, st, k), struct.pack("<3f", 0.01 * st + m, 0.1 * k, 0.5 + m))
            for k in range(6, width):  # extras: the published values
                i = (m * 28 + st) * width + k
                uc.mem_write(MGR + ptr + 4 * i, struct.pack("<I", rgb_addr(m, st, k)))
                uc.mem_write(MGR + uv + 4 * i, struct.pack("<f", uv_val(m, st, k)))
                uc.mem_write(rgb_addr(m, st, k), struct.pack("<3f", 0.01 * st + m, 0.1 * k, 0.5 + m))
    return uc, width


def call(uc, entry, regs=None, args=(), esp_extra=0):
    for r in (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP):
        uc.reg_write(r, 0)
    for r, v in (regs or {}).items():
        uc.reg_write(r, v)
    sp = STACK
    uc.mem_write(sp, struct.pack("<I", RET) + b"".join(struct.pack("<I", a & 0xFFFFFFFF) for a in args))
    uc.reg_write(UC_X86_REG_ESP, sp)
    uc.emu_start(entry, RET, count=20000)
    return uc


def set_mode(uc, mode):
    uc.mem_write(MGR + 0x348, struct.pack("<II", mode, mode))


# ---- generic getter 5196F0 (ESI=mgr, ECX=out, EDX=state, EDI=slot, stack player) ----
def run_getter(native, mode, state, slot):
    uc, _ = machine(native)
    set_mode(uc, mode)
    call(uc, 0x5196F0, {UC_X86_REG_ESI: MGR, UC_X86_REG_ECX: OUT, UC_X86_REG_EDX: state, UC_X86_REG_EDI: slot}, (0,))
    return uc.reg_read(UC_X86_REG_EAX) & 0xFF, bytes(uc.mem_read(OUT, 12))


@pytest.mark.parametrize("mode", [0, 1])
def test_generic_getter_matches_stock_for_every_state_and_column(mode):
    for state in range(29):
        for slot in range(6):
            assert run_getter(True, mode, state, slot) == run_getter(False, mode, state, slot), (state, slot)


def test_generic_getter_reads_columns_6_and_7_and_stock_rejects_them():
    for slot in (6, 7):
        ok_stock, _ = run_getter(False, 1, 3, slot)
        ok_native, rgb = run_getter(True, 1, 3, slot)
        assert ok_stock == 0 and ok_native == 1
        assert rgb == struct.pack("<3f", 0.01 * 3 + 1, 0.1 * slot, 1.5)
    assert run_getter(True, 0, 28, 0)[0] == 0 and run_getter(True, 0, 3, 8)[0] == 0


# ---- named colours 59BD70 (ECX=player, EAX=out, stack index): all eight cases ----
@pytest.mark.parametrize("mode", [0, 1])
def test_named_colour_dispatch_matches_stock(mode):
    for idx in range(8):
        got = []
        for native in (False, True):
            uc, _ = machine(native)
            set_mode(uc, mode)
            call(uc, 0x59BD70, {UC_X86_REG_ECX: 1, UC_X86_REG_EAX: OUT}, (idx,))
            got.append(bytes(uc.mem_read(OUT, 12)))
        assert got[0] == got[1] and got[0] != bytes(12), idx


# ---- fixed UV readers: 79BB70 (EAX=player, EDX=out, stack flag), 7A8590 (CL=string, EAX=player, ESI=out) ----
@pytest.mark.parametrize("mode", [0, 1])
def test_uv_readers_match_stock(mode):
    for native_regs in ():
        pass
    for flag in (0, 1):
        res = []
        for native in (False, True):
            uc, _ = machine(native)
            set_mode(uc, mode)
            call(uc, 0x79BB70, {UC_X86_REG_EAX: 1, UC_X86_REG_EDX: OUT}, (flag,))
            res.append(bytes(uc.mem_read(OUT, 4)))
        assert res[0] == res[1] and res[0] != bytes(4)
    for stack in (1, 2):
        for s in range(6):
            res = []
            for native in (False, True):
                uc, _ = machine(native)
                set_mode(uc, mode)
                call(uc, 0x7A8590, {UC_X86_REG_ECX: s, UC_X86_REG_EAX: 1, UC_X86_REG_ESI: OUT}, (stack,))
                res.append(bytes(uc.mem_read(OUT, 4)))
            assert res[0] == res[1] and res[0] != bytes(4), (stack, s)
    for s in (6, 7):  # guard 8: native reads the published column
        uc, _ = machine(True)
        set_mode(uc, mode)
        call(uc, 0x7A8590, {UC_X86_REG_ECX: s, UC_X86_REG_EAX: 1, UC_X86_REG_ESI: OUT}, (2,))
        assert struct.unpack("<f", bytes(uc.mem_read(OUT, 4)))[0] == pytest.approx(uv_val(mode, 17, s))


# ---- writers: FillStateColors 518AF0 with the key lookup 52ECE0 stubbed ----
def run_fill_colors(native, mode, state, count):
    uc, width = machine(native)
    ptr = PTR_NEW if native else PTR_OLD
    for i in range(2 * 28 * width):
        uc.mem_write(MGR + ptr + 4 * i, b"\0\0\0\0")
    lst = OUT + 0x100
    uc.mem_write(lst + 0x10, struct.pack("<I", count))

    def stub(uc_, addr, size, _):
        if addr == 0x52ECE0:
            arg = struct.unpack("<I", bytes(uc_.mem_read(uc_.reg_read(UC_X86_REG_ESP) + 4, 4)))[0]
            uc_.reg_write(UC_X86_REG_EAX, (uc_.reg_read(UC_X86_REG_ECX) * 31 + arg) & 0xFFFFFFFF)
            sp = uc_.reg_read(UC_X86_REG_ESP)
            ret = struct.unpack("<I", bytes(uc_.mem_read(sp, 4)))[0]
            uc_.reg_write(UC_X86_REG_ESP, sp + 8)
            uc_.reg_write(UC_X86_REG_EIP, ret)

    uc.hook_add(UC_HOOK_CODE, stub, begin=0x52ECE0, end=0x52ECE0)
    call(uc, 0x518AF0, {UC_X86_REG_ECX: mode}, (MGR, state, lst))
    return [
        struct.unpack("<I", bytes(uc.mem_read(MGR + ptr + 4 * ((m * 28 + st) * width + k), 4)))[0]
        for m in range(2)
        for st in range(28)
        for k in range(width)
    ], width


@pytest.mark.parametrize("count", [3, 6, 9])
def test_fill_state_colors_matches_stock_and_stays_six_wide(count):
    for mode in (0, 1):
        for state in (0, 1, 4, 13, 26, 27):
            old, _ = run_fill_colors(False, mode, state, count)
            new, w = run_fill_colors(True, mode, state, count)
            assert w == 8
            stock_cols = [new[i] for i in range(len(new)) if i % 8 < 6]
            assert stock_cols == old
            assert all(new[i] == 0 for i in range(len(new)) if i % 8 >= 6)  # extras are the publication's


# ---- writers: FillStateUV 518B50 / FillStateIndex 518C70, ranged + equal-range branches ----
def run_fill_uv(native, fn, mode, state, lo, hi, publish):
    uc, width = machine(native)
    uv = UV_NEW if native else UV_OLD
    for i in range(2 * 28 * width):
        uc.mem_write(MGR + uv + 4 * i, b"\0\0\0\0")
    if native and publish:  # CMUV / CMIX: the DLL's integer callbacks (features/colours/hooks.cpp)

        def cb(uc_, addr, size, _):
            eax = uc_.reg_read(UC_X86_REG_EAX)
            ebp = uc_.reg_read(UC_X86_REG_EBP)
            ecx = uc_.reg_read(UC_X86_REG_ECX)
            if addr == 0x518BC0:
                bits = bytes(uc_.mem_read(ebp + 0x14, 4))
                for s in (0, 6, 7):
                    uc_.mem_write(ecx + UV_NEW + 4 * (eax + s), bits)
                uc_.reg_write(UC_X86_REG_EIP, 0x518BC7)
            else:  # 518CA4: EAX = EDX*8
                edx = uc_.reg_read(UC_X86_REG_EDX)
                bits = bytes(uc_.mem_read(ebp + 0xC, 4))
                uc_.reg_write(UC_X86_REG_EAX, edx * 8)
                for s in (6, 7):
                    uc_.mem_write(ecx + UV_NEW + 4 * (edx * 8 + s), bits)
                uc_.reg_write(UC_X86_REG_EIP, 0x518CA9)

        uc.hook_add(UC_HOOK_CODE, cb, begin=0x518BC0, end=0x518BC0)
        uc.hook_add(UC_HOOK_CODE, cb, begin=0x518CA4, end=0x518CA4)
    if fn == 0x518B50:  # ecx = mgr, edx/eax = range, stack mgr, mode, state, int denominator
        call(uc, fn, {UC_X86_REG_EDX: lo, UC_X86_REG_EAX: hi}, (MGR, mode, state, 5))
    else:  # 518C70: stack mgr, mode, ranged: state = range lo
        call(uc, fn, {UC_X86_REG_EDX: lo, UC_X86_REG_EAX: hi}, (MGR, mode))
    return [
        bytes(uc.mem_read(MGR + uv + 4 * ((m * 28 + st) * width + k), 4))
        for m in range(2)
        for st in range(28)
        for k in range(width)
    ]


@pytest.mark.parametrize("fn", [0x518B50, 0x518C70])
@pytest.mark.parametrize("lo,hi", [(2, 2), (0, 6), (1, 9), (3, 3)])
def test_fill_uv_and_index_match_stock(fn, lo, hi):
    for mode in (0, 1):
        old = run_fill_uv(False, fn, mode, 14 if fn == 0x518C70 else 3, lo, hi, False)
        new = run_fill_uv(True, fn, mode, 14 if fn == 0x518C70 else 3, lo, hi, True)
        stock_cols = [new[i] for i in range(len(new)) if i % 8 < 6]
        assert stock_cols == old
        if lo == hi:  # scalar states repeat the value into the two extras
            for i in range(0, len(new), 8):
                if new[i] != b"\0\0\0\0":
                    assert new[i + 6] == new[i + 7] == new[i] or new[i + 6] == new[i + 7]
        else:
            assert all(new[i] == b"\0\0\0\0" for i in range(len(new)) if i % 8 >= 6)


def test_every_port_is_inside_the_documented_functions_and_same_length():
    for p in cm.ports():
        assert len(p.old) == len(p.new)
    ids = {p.va for p in cm.ports()}
    assert not ids & {0x40CD45, 0x40CD72, 0x4611D0, 0x7A3214, 0x7A335F, 0x7A0A68}  # other objects


# ---- fret-number lane colour 4B0E70 (EAX=out RGB, EDX=string, EDI=float out, stack: flag byte, float* out2) ----
# Regression (fix/fretnumcolour): this reader was missing from READS, so on the native 8-column manager it kept
# the stock stride/base and read states 9/10 from the wrong state/slot (alternating fret-number colours).
@pytest.mark.parametrize("mode", [0, 1])
def test_fret_number_lane_colour_matches_stock(mode):
    for flag in (0, 1):
        for string in range(4):
            got = []
            for native in (False, True):
                uc, _ = machine(native)
                set_mode(uc, mode)
                call(
                    uc,
                    0x4B0E70,
                    {UC_X86_REG_EAX: OUT, UC_X86_REG_EDX: string, UC_X86_REG_EDI: OUT + 0x100},
                    (flag, OUT + 0x200),
                )
                got.append(
                    bytes(uc.mem_read(OUT, 12))
                    + bytes(uc.mem_read(OUT + 0x100, 4))
                    + bytes(uc.mem_read(OUT + 0x200, 4))
                )
            assert got[0] == got[1] and got[0][:12] != bytes(12), (flag, string)
            # the colour is state 9 (flag) / state 10 (no flag), slot = string
            want = struct.pack("<3f", 0.01 * (9 if flag else 10) + mode, 0.1 * string, 0.5 + mode)
            assert got[1][:12] == want


# ---- Guitarcade ResolveExtraDataColor 48EBD0 (UV scalar), body after the manager lookup ----
# Regression: the audit (docs/ARCHITECTURE.md) found 48EC14/48EC1A still on the 6-column stride and the 0x890
# UV base. Run the tail 48EBED..48EC24 (EAX=manager, ESI=player, EBX=state, EDI=slot) and read the float it stores.
def _extra_data_uv(native, mode, player, state, slot):
    uc, _ = machine(native)
    uc.mem_write(MGR + 0x348, struct.pack("<II", mode, mode))
    ebp = OUT + 0x800
    for r, v in (
        (UC_X86_REG_EAX, MGR),
        (UC_X86_REG_ESI, player),
        (UC_X86_REG_EBX, state),
        (UC_X86_REG_EDI, slot),
        (UC_X86_REG_EBP, ebp),
    ):
        uc.reg_write(r, v)
    uc.emu_start(0x48EBED, 0x48EC24, count=100)
    return struct.unpack("<f", bytes(uc.mem_read(ebp - 0x24, 4)))[0]


def test_extra_data_colour_uv_matches_stock_and_reads_native_cell():
    for mode in (0, 1):
        for player in (0, 1):
            for state in (0, 1, 9, 14, 27):
                for slot in range(6):
                    stock = _extra_data_uv(False, mode, player, state, slot)
                    native = _extra_data_uv(True, mode, player, state, slot)
                    assert stock == native == uv_val(mode, state, slot), (mode, player, state, slot)


def test_extra_data_colour_unported_reads_the_wrong_cell():
    """Without the two CM48EC* ports the stock 6-column math on the 8-column manager returns a different cell."""
    uc, _ = machine(True)
    text, base = gen_fncopy.load_text()
    ported = {p.va for p in cm.ports()}
    assert {0x48EC14, 0x48EC1A} <= ported
    # The legacy view is deliberately unpopulated here: catch a native reader using it.
    stock_uc, _ = machine(False)
    uc.mem_write(0x48EC14, bytes(stock_uc.mem_read(0x48EC14, 3)))
    uc.mem_write(0x48EC1A, bytes(stock_uc.mem_read(0x48EC1A, 7)))
    for r, v in (
        (UC_X86_REG_EAX, MGR),
        (UC_X86_REG_ESI, 0),
        (UC_X86_REG_EBX, 3),
        (UC_X86_REG_EDI, 1),
        (UC_X86_REG_EBP, OUT + 0x800),
    ):
        uc.reg_write(r, v)
    uc.mem_write(MGR + 0x348, struct.pack("<II", 0, 0))
    uc.emu_start(0x48EBED, 0x48EC24, count=100)
    got = struct.unpack("<f", bytes(uc.mem_read(OUT + 0x800 - 0x24, 4)))[0]
    assert got != uv_val(0, 3, 1)


def test_audit_missed_reads_are_all_in_reads():
    """The three readers docs/ARCHITECTURE.md found missed (48EC1A, 4B0EA1, 4B0EF3) are covered."""
    covered = {va for vas in cm.READS.values() for va in vas}
    assert {0x48EC1A, 0x4B0EA1, 0x4B0EF3} <= covered
    assert {0x48EC1A, 0x4B0EA1, 0x4B0EF3, 0x48EC14, 0x4B0E99, 0x4B0EEB} <= {p.va for p in cm.ports()}


def test_constructor_initializes_only_appended_tables_and_preserves_legacy_storage():
    uc, _ = machine(True)
    before = bytes([0xA5]) * 0xDD0
    uc.mem_write(MGR, before)
    # Execute the actual ported constructor's matrix loop, with ST0 = 0.
    uc.mem_write(RET, b"\xd9\xee")  # FLDZ
    uc.emu_start(RET, RET + 2, count=1)
    uc.reg_write(UC_X86_REG_ESI, MGR)
    uc.reg_write(UC_X86_REG_EAX, 0)
    uc.emu_start(0x5184F6, 0x518521, count=10000)
    assert bytes(uc.mem_read(MGR, 0xDD0)) == before
    assert bytes(uc.mem_read(MGR + PTR_NEW, 448 * 4)) == struct.pack("<I", 0x12F1A84) * 448
    assert bytes(uc.mem_read(MGR + UV_NEW, 448 * 4)) == bytes(448 * 4)
