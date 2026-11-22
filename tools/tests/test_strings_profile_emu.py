"""Profile assignment must cap stock-layout fallbacks before the first per-string consumer."""

import struct

import pytest
from unicorn.x86_const import UC_X86_REG_EBX, UC_X86_REG_EDI, UC_X86_REG_EFLAGS, UC_X86_REG_ESI

import sitetable
from emu_harness import (
    ALL_REGS,
    CODE,
    DATA,
    RET_MAGIC,
    S7_REGS,
    SITES,
    STACK,
    build_mid_entry,
    needs_emu,
    site_target,
    site_va,
)

pytestmark = needs_emu


@pytest.fixture(scope="module")
def profile_emus(emus):
    _, patched = emus
    site = next(s for s in sitetable.load(SITES)[1] if s.id == "GIP1")
    va = site_va("GIP1")
    # The shared harness defaults to stock strings, so explicitly install this native-strings hook.
    entry = build_mid_entry(
        patched.uc, patched.exports, site, va, site_target("GIP1"), CODE + 0xF0000, STACK + 0x70000, RET_MAGIC
    )
    patched.uc.mem_write(va, b"\xe8" + struct.pack("<i", entry - (va + 5)))
    return emus


@pytest.mark.parametrize("count", [4, 6, 7, 8])
def test_profile_assignment_caps_unregistered_info_before_stock_consumers(profile_emus, count):
    holder, guitar, gs, info, profile = (DATA + 0x2000 + i * 0x100 for i in range(5))
    results = []
    for emu in profile_emus:
        emu.uc.mem_write(holder, bytes(0x500))
        emu.uc.mem_write(holder + 4, struct.pack("<I", guitar))
        emu.uc.mem_write(guitar + 0x0C, struct.pack("<I", gs))
        emu.uc.mem_write(guitar + 0x24, struct.pack("<I", info))
        # Registry exhaustion leaves a stock-sized InstrumentInfo; the native strings still own their extras.
        emu.uc.mem_write(gs + 0x3A, struct.pack("<H", 0x5852))
        emu.uc.mem_write(gs + 0xAC, struct.pack("<I", 0x47585352))
        emu.uc.mem_write(info + 0x0B, b"\x06")
        emu.uc.mem_write(profile + 0x38, bytes([count]))
        regs = {**S7_REGS, UC_X86_REG_EBX: holder, UC_X86_REG_EDI: info, UC_X86_REG_ESI: profile}
        # Stop immediately after FromProfile, before the stock per-string initialization at 0x4B2416.
        emu.run(0x4B240B, 0x4B2410, regs)
        results.append((emu.regs(*ALL_REGS, UC_X86_REG_EFLAGS), emu.fpu_top(), emu.read(info, 0x38)))
    stock, patched = results
    assert stock[2][0x0B] == count
    assert patched[2][0x0B] == min(count, 6)
    assert patched[:2] == stock[:2]
    assert patched[2][:0x0B] == stock[2][:0x0B]
    assert patched[2][0x0C:] == stock[2][0x0C:]
