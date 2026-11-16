"""Song-camera site and stack contract, using the owner's unpacked image (never copied into the repo)."""

import struct

import capstone
import pefile
import pytest
from unicorn.x86_const import UC_X86_REG_EFLAGS

from emu_common import EXE, SITES, needs_exe
from emu_harness import needs_emu
from sitetable import load
import sitecheck


@needs_exe
def test_song_camera_site_and_four_outgoing_z_values():
    site = next((s for s in load(SITES)[1] if s.id == "SC1"), None)
    assert site is not None, "song-camera floor compensation site is missing"
    pe = pefile.PE(str(EXE), fast_load=True)
    text, va = sitecheck.text_section(pe)
    ok, msg, address, _ = sitecheck.check_site(text, va, site)
    assert ok, msg
    assert address == 0x593A1A and site.length == 6
    assert sitecheck.instruction_span_end(text, va, address, site.length) == address + 6
    assert not sitecheck.inbound_branches(text, va, [(address, address + 6, site.id)])
    cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    # EDX is float bits, not a pointer; its value is stored into the fourth vector after the site.
    before = [(i.mnemonic, i.op_str) for i in cs.disasm(pe.get_data(address - 0x400000 - 3, 3), address - 3)]
    assert before == [("mov", "edx, dword ptr [esi + 0x34]")]
    after = [(i.mnemonic, i.op_str) for i in cs.disasm(pe.get_data(0x193A20, 0x49), 0x593A20)]
    assert ("mov", "dword ptr [esp + 0x3c], edx") in after
    # Four vector starts: fourth (34h), then 10h, 1Ch, 28h. Their z slots are +8 each.
    assert [operand for mnemonic, operand in after if mnemonic == "lea" and operand.startswith("esi,")] == [
        "esi, [esp + 0x40]",
        "esi, [esp + 0x34]",
        "esi, [esp + 0x10]",
        "esi, [esp + 0x1c]",
        "esi, [esp + 0x28]",
    ]


@needs_emu
@pytest.mark.parametrize("enabled", [False, True])
@pytest.mark.parametrize("count", [4, 5, 6, 7, 8])
@pytest.mark.parametrize("bass", [False, True])
@pytest.mark.parametrize("inverted", [False, True])
@pytest.mark.parametrize("lefty", [False, True])
def test_song_camera_mid_hook_replays_stock_and_shifts_only_four_z_values(enabled, count, bass, inverted, lefty):
    from emu_harness import Emu, ALL_REGS, CODE, DATA, STACK, build_mid_entry, site_va

    emu = Emu(False)
    uc = emu.uc
    site = next(s for s in load(SITES)[1] if s.id == "SC1")
    address = site_va("SC1")
    emu.read(address, site.length)
    uc.mem_write(address, site.expect)

    entry = build_mid_entry(uc, emu.exports, site, address, 0, CODE + 0xA000, STACK + 0x70000, CODE + 0xC000)
    guitar, info, strings, a0, a1, grid = (DATA + n for n in (0x100, 0x200, 0x300, 0x400, 0x500, 0x10000))
    uc.mem_write(guitar, bytes(0x40))
    uc.mem_write(info, bytes([2 if bass else 1]) + bytes(7) + bytes([inverted, lefty, 0, count]) + bytes(4))
    for at, value in (
        (guitar + 0x24, info),
        (guitar + 0x0C, strings),
        (guitar + 0x10, grid),
        (strings + 4, a0),
        (strings + 8, a1),
    ):
        uc.mem_write(at, struct.pack("<I", value))
    uc.mem_write(a0, bytes(0x20))
    uc.mem_write(a1, bytes(0x20))
    uc.mem_write(a1 + 8, struct.pack("<3f", -0.25 if lefty else 0.25, 0, 2 if inverted else -2))
    uc.mem_write(grid + 0x5C40, bytes([1]) + bytes(0x37))
    uc.mem_write(grid + 0x5C58, struct.pack("<f", -20))
    uc.mem_write(emu.exports["g_rs7_frets_on"], struct.pack("<I", enabled))
    esp = STACK + 0x80000
    regs = dict(
        zip(
            ALL_REGS,
            [
                0x111,
                0xABCDEF12,
                0x333,
                struct.unpack("<I", struct.pack("<f", 100))[0],
                guitar,
                0x666,
                STACK + 0x81000,
                esp,
            ],
        )
    )
    regs[UC_X86_REG_EFLAGS] = 0xED7
    extended = count in ((5, 6) if bass else (7, 8))
    shift = (-2 if bass else -2 * (count - 6)) if enabled and extended and (inverted or (bass and count == 6)) else 0
    untouched = {
        at: bytes(uc.mem_read(at, size))
        for at, size in ((guitar, 0x40), (info, 0x10), (strings, 12), (a0, 0x20), (a1, 0x20), (grid + 0x5C40, 0x38))
    }
    initial = struct.pack("<16f", *range(16))
    results = []
    for start in (address, entry, entry):
        uc.mem_write(esp, initial)
        emu.run(start, address + 6, regs)
        results.append((emu.regs(*ALL_REGS), uc.reg_read(UC_X86_REG_EFLAGS), bytes(uc.mem_read(esp, 64))))
    expected_regs = list(results[0][0])
    expected_regs[3] = struct.unpack("<I", struct.pack("<f", 100 + shift))[0]
    expected_stack = list(range(16))
    for index in (6, 9, 12):
        expected_stack[index] += shift
    assert results[1] == results[2] == (tuple(expected_regs), results[0][1], struct.pack("<16f", *expected_stack))
    for at, before in untouched.items():
        assert bytes(uc.mem_read(at, len(before))) == before
