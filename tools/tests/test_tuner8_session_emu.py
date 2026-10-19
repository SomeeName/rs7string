"""Execute the DLL's session ownership hooks, including their stock return ABI."""

import struct
from pathlib import Path

import pefile
import pytest
from unicorn import UC_ARCH_X86, UC_MODE_32, Uc
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

from emu_harness import TEXT_VA, game_text, needs_emu, site_mid_entry, site_va

DLL = Path(__file__).resolve().parents[2] / "bin/Release/dinput8.dll"
SESSION, STACK, RET, ENTRIES = 0x10000000, 0x11000000, 0x12000000, 0x13000000
pytestmark = needs_emu  # the DLL, and the game image for the sites' bytes


def machine():
    pe = pefile.PE(str(DLL))
    base = pe.OPTIONAL_HEADER.ImageBase
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    pages = set()
    uc.mem_map(base, (pe.OPTIONAL_HEADER.SizeOfImage + 0xFFF) & ~0xFFF)
    uc.mem_write(base, pe.header)
    for sec in pe.sections:
        uc.mem_write(base + sec.VirtualAddress, sec.get_data())
    exports = {e.name.decode(): base + e.address for e in pe.DIRECTORY_ENTRY_EXPORT.symbols if e.name}
    assert "Rs7_Mid_TU8INIT" in exports, "session owner hook missing from DLL"
    for addr in (SESSION, STACK, RET, ENTRIES):
        uc.mem_map(addr, 0x10000)
    # The mid-hook entries (features/tuner/hooks_entries.cpp), built from the sites' game bytes as Rs7_Startup does.
    for k, sid in enumerate(("TU8INIT", "TU8FREE")):
        va = site_va(sid)
        page = va & ~0xFFF
        if page not in pages:
            uc.mem_map(page, 0x2000)
            uc.mem_write(page, game_text()[page - TEXT_VA : page - TEXT_VA + 0x2000])
            pages.update((page, page + 0x1000))
        exports["entry_" + sid] = site_mid_entry(uc, exports, sid, ENTRIES + 0x100 * k, STACK + 0xF000, RET)
    uc.mem_write(RET, b"\xf4")
    uc.mem_write(exports["g_rs7_native_tuner"], struct.pack("<I", 1))
    uc.mem_write(exports["g_rs7_resume_TU8FREE"], struct.pack("<I", RET))
    return uc, exports


def initialise(uc, exports):
    prefix = bytes((i * 13 + 7) & 255 for i in range(0x278))
    uc.mem_write(SESSION, prefix)
    frame = STACK + 0x8000
    uc.mem_write(frame, struct.pack("<II", 0x12345678, RET))
    uc.mem_write(frame - 4, struct.pack("<I", 0xBABA1234))
    uc.reg_write(UC_X86_REG_EBP, frame)
    uc.reg_write(UC_X86_REG_ESP, frame - 4)
    uc.reg_write(UC_X86_REG_ESI, SESSION)
    uc.reg_write(UC_X86_REG_EDI, 0xD1D1D1D1)
    uc.emu_start(exports["entry_TU8INIT"], RET, count=100000)
    assert uc.reg_read(UC_X86_REG_EIP) == RET
    assert uc.reg_read(UC_X86_REG_EAX) == SESSION
    assert uc.reg_read(UC_X86_REG_EBX) == 0xBABA1234
    assert uc.reg_read(UC_X86_REG_EBP) == 0x12345678
    assert uc.reg_read(UC_X86_REG_EDI) == 0xD1D1D1D1
    assert uc.reg_read(UC_X86_REG_ESP) == frame + 8
    assert bytes(uc.mem_read(SESSION, 0x278)) == prefix
    assert bytes(uc.mem_read(SESSION + 0x278, 4)) == struct.pack("<I", 0x38545552)
    # The tail stays unused until TS makes the session native (features/tuner/hooks.h): zero, the pick states
    # -2 like the stock ctor (0x5AE0E9), not native.
    tail = bytes(uc.mem_read(SESSION + 0x280, 0x100))
    assert tail[: 0x348 - 0x280] == b"\0" * (0x348 - 0x280)
    assert tail[0x348 - 0x280 : 0x368 - 0x280] == struct.pack("<8i", *([-2] * 8))
    assert tail[0x368 - 0x280 :] == b"\0" * (0x380 - 0x368)


def free_size(uc, exports):
    top = STACK + 0x8000
    uc.reg_write(UC_X86_REG_ESP, top)
    regs = {
        UC_X86_REG_EAX: 0xAABBCCDD,
        UC_X86_REG_EDX: 0xCCBBAADD,
        UC_X86_REG_EBX: 0xB1B1B1B1,
        UC_X86_REG_ECX: 0xC1C1C1C1,
        UC_X86_REG_ESI: SESSION,
    }
    for reg, value in regs.items():
        uc.reg_write(reg, value)
    uc.emu_start(exports["entry_TU8FREE"], RET, count=100000)
    assert uc.reg_read(UC_X86_REG_ESP) == top - 4
    for reg, value in regs.items():
        assert uc.reg_read(reg) == value
    return struct.unpack("<I", uc.mem_read(top - 4, 4))[0]


def test_ctor_return_initializes_tail_and_sized_free_unregisters_it():
    uc, exports = machine()
    initialise(uc, exports)
    assert free_size(uc, exports) == 0x380
    assert free_size(uc, exports) == 0x278


def test_sized_free_of_stock_owner_keeps_the_stock_size():
    uc, exports = machine()
    assert free_size(uc, exports) == 0x278
