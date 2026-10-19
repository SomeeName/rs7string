"""Confirmed tuner/StringAction GuitarStrings accesses, executed as x86.

Whole highlight/clear routines are compared to stock for six strings and bass.
The StringAction guard and both child-read blocks are exercised separately;
the game's string/message subsystem is outside this emulator's boundary.
"""

import importlib
import struct
from pathlib import Path

import pytest
from unicorn import UC_ARCH_X86, UC_MODE_32, Uc
from unicorn.x86_const import (
    UC_X86_REG_EAX,
    UC_X86_REG_EBX,
    UC_X86_REG_ECX,
    UC_X86_REG_EDX,
    UC_X86_REG_EIP,
    UC_X86_REG_ESI,
    UC_X86_REG_ESP,
)

import gen_fncopy

pytestmark = pytest.mark.skipif(not gen_fncopy.EXE.exists(), reason="needs unpacked exe")
SESSION, GUITAR, GS, OWNER, INFO = 0x10000, 0x11000, 0x12000, 0x13000, 0x14000
KIDS, STACK, RET, COPY = 0x20000, 0x40000, 0x50000, 0x30000000


def port_module():
    path = Path(__file__).resolve().parents[1] / "gen_tuner8.py"
    assert path.exists(), "native tuner/StringAction function port generator missing"
    return importlib.import_module("gen_tuner8")


def copy_bytes(fid):
    mod = port_module()
    text, base = gen_fncopy.load_text()
    fn = next(f for f in mod.FNS if f.id == fid)
    code, edits, rels, _, _ = gen_fncopy.build(fn, text, base)
    patched = bytearray(code)
    patched.extend(bytes(1) * (0x900 - len(patched)))
    # the PubEdge call-in (publish off): AL = first ? 0 : active count - 1 (the stock expression, +40h)
    patched.extend(bytes.fromhex("84C9 7506 8A4040 FEC8 C3 32C0 C3".replace(" ", "")))
    for e in edits:
        if e.sym:
            assert e.sym == "g_rs7_callin_PubEdge", e.sym
            patched[e.off : e.off + len(e.expect)] = bytes([0x90]) * len(e.expect)
            patched[e.off] = 0xE8
            patched[e.off + 1 : e.off + 5] = struct.pack("<i", (COPY + 0x900) - (COPY + e.off + 5))
            continue
        patched[e.off : e.off + len(e.write)] = e.write
    for off, target in rels:
        patched[off : off + 4] = struct.pack("<i", target - (COPY + off + 4))
    return fn, bytes(patched)


def machine(fid, native, count):
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    for a, n in ((SESSION, 0x5000), (KIDS, 0x4000), (STACK - 0x8000, 0x10000), (RET, 0x1000), (COPY, 0x1000)):
        uc.mem_map(a, n)
    text, base = gen_fncopy.load_text()
    fn, patched = copy_bytes(fid)
    uc.mem_map(fn.start & ~0xFFF, 0x3000)
    uc.mem_write(fn.start, text[fn.start - base : fn.end - base])
    uc.mem_write(COPY, patched)
    # ClearHighlight calls the native GuitarStrings reset entry. Stop at that
    # call's target to observe the highlight loop without mocking the reset.
    uc.mem_map(0x59A000, 0x1000)
    uc.mem_write(RET, b"\xf4")

    def dword(a, v):
        uc.mem_write(a, struct.pack("<I", v))

    dword(SESSION + 0x2C, GUITAR)
    dword(GUITAR + 0x0C, GS)
    dword(GUITAR + 0x24, INFO)
    dword(GS, OWNER)
    dword(OWNER + 0x24, INFO)
    uc.mem_write(INFO + 0xB, bytes([min(count, 6)]))
    uc.mem_write(INFO + 0x40, bytes([count, count]))
    # Poison all prefix pointers: the native loop must use +40h exclusively.
    for s in range(8):
        dword(GS + 0x40 + 4 * s, KIDS + 0x200 * s)
        dword(KIDS + 0x200 * s + 4, 1)
        uc.mem_write(KIDS + 0x200 * s + 0x6D, b"\x01")
        uc.mem_write(KIDS + 0x200 * s + 0xFC, b"\x01")
    for s in range(6):
        dword(GS + 4 + 4 * s, (0xDEAD0000 if native else KIDS + 0x200 * s))
    return uc, fn, (COPY if native else fn.start)


def run_highlight(native, count, selected, missing=()):
    uc, _, entry = machine("TU8H", native, count)
    for s in missing:
        uc.mem_write(KIDS + 0x200 * s + 4, b"\0" * 4)
    uc.mem_write(STACK, struct.pack("<II", RET, selected))
    uc.reg_write(UC_X86_REG_ESP, STACK)
    uc.reg_write(UC_X86_REG_EAX, SESSION)
    uc.emu_start(entry, RET, count=10000)
    assert uc.reg_read(UC_X86_REG_EIP) == RET
    assert uc.reg_read(UC_X86_REG_ESP) == STACK + 8
    return [bytes(uc.mem_read(KIDS + 0x200 * s, 0x110)) for s in range(8)]


@pytest.mark.parametrize("count", [4, 6])
def test_highlight_matches_stock_for_every_stock_string(count):
    for selected in range(count):
        assert run_highlight(True, count, selected) == run_highlight(False, count, selected)


@pytest.mark.parametrize("count", [7, 8])
def test_highlight_uses_extra_slots_without_touching_prefix(count):
    for selected in range(count):
        rows = run_highlight(True, count, selected)
        for s in range(count):
            assert rows[s][0x6D] == rows[s][0xFC] == int(s == selected)
        for s in range(count, 8):
            assert rows[s][0x6D] == rows[s][0xFC] == 1


def test_highlight_ignores_strings_whose_model_is_not_loaded():
    assert run_highlight(True, 6, 2, (0, 2, 5)) == run_highlight(False, 6, 2, (0, 2, 5))
    rows = run_highlight(True, 8, 7, (6,))
    assert rows[6][0x6D] == rows[6][0xFC] == 1
    assert rows[7][0x6D] == rows[7][0xFC] == 1


@pytest.mark.parametrize("count", [4, 6, 7, 8])
def test_clear_highlight_visits_only_active_native_strings(count):
    uc, _, entry = machine("TU8C", True, count)
    uc.mem_write(STACK, struct.pack("<I", RET))
    uc.reg_write(UC_X86_REG_ESP, STACK)
    uc.reg_write(UC_X86_REG_ESI, SESSION)
    uc.emu_start(entry, 0x59AC20, count=10000)
    assert uc.reg_read(UC_X86_REG_EIP) == 0x59AC20
    for s in range(8):
        expected = int(s >= count)
        assert bytes(uc.mem_read(KIDS + 0x200 * s + 0x6D, 1)) == bytes([expected])
        assert bytes(uc.mem_read(KIDS + 0x200 * s + 0xFC, 1)) == bytes([expected])


@pytest.mark.parametrize("count", [4, 6])
def test_clear_matches_stock_before_the_unchanged_reset_call(count):
    results = []
    for native in (False, True):
        uc, _, entry = machine("TU8C", native, count)
        uc.mem_write(STACK, struct.pack("<I", RET))
        uc.reg_write(UC_X86_REG_ESP, STACK)
        uc.reg_write(UC_X86_REG_ESI, SESSION)
        uc.emu_start(entry, 0x59AC20, count=10000)
        assert uc.reg_read(UC_X86_REG_EIP) == 0x59AC20
        results.append([bytes(uc.mem_read(KIDS + 0x200 * s, 0x110)) for s in range(8)])
    assert results[0] == results[1]


@pytest.mark.parametrize("site,reg", [(0x7E7460, UC_X86_REG_EAX), (0x7E7497, UC_X86_REG_ECX)])
@pytest.mark.parametrize("slot", range(8))
def test_stringaction_reads_native_child_array(site, reg, slot):
    uc, fn, _ = machine("TU8A", True, 8)
    uc.reg_write(UC_X86_REG_ESI, GS)
    uc.reg_write(UC_X86_REG_ESP, STACK)
    uc.mem_write(STACK + 0x14, struct.pack("<I", slot))
    start = COPY + site - fn.start
    end = start + 9
    uc.emu_start(start, end, count=20)
    assert uc.reg_read(UC_X86_REG_EAX) == KIDS + 0x200 * slot


@pytest.mark.parametrize("count", [4, 6, 7, 8])
@pytest.mark.parametrize("slot", [-1, 0, 3, 5, 6, 7, 8, 255, 0x10000])
def test_stringaction_guard_accepts_exactly_active_slots(count, slot):
    uc, fn, _ = machine("TU8A", True, count)
    uc.reg_write(UC_X86_REG_EBX, SESSION)
    uc.mem_write(SESSION + 0x37C, struct.pack("<I", GUITAR))
    uc.reg_write(UC_X86_REG_ESP, STACK)
    uc.mem_write(STACK + 0x14, struct.pack("<i", slot))
    # Execute the guard through its conditional branch; don't execute the
    # message-string destructor on the rejected path.
    uc.emu_start(COPY + 0x7E7407 - fn.start, COPY + 0x7E741A - fn.start, count=5)
    end = uc.reg_read(UC_X86_REG_EIP)
    target = 0x7E7443 if 0 <= slot < count else 0x7E741A
    assert end == COPY + target - fn.start


def test_generated_header_matches_unpacked_image():
    mod = port_module()
    text, base = gen_fncopy.load_text()
    assert mod.render(text, base) == mod.OUT.read_text(encoding="utf-8")
