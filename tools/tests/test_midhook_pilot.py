"""Differential oracle: frozen MSVC stubs from 26b7aa1 vs production C++ mid-hooks.

Only code emitted by this repo is frozen; no game bytes or archives are stored.
Relocations bind export operands to this build (and scratch resume slots).
"""

import json
import struct
from pathlib import Path

import pytest
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EFLAGS, UC_X86_REG_EIP, UC_X86_REG_XMM0, UC_X86_REG_XMM7
from emu_harness import Emu, ALL_REGS, CODE, DATA, STACK, DLL, SNAPSHOT, site_va, game_text, TEXT_VA

pytestmark = pytest.mark.skipif(
    not DLL.exists() or not (SNAPSHOT / "memory.bin").exists(), reason="needs built DLL and snapshot"
)
FIXTURE = Path(__file__).with_name("fixtures") / "midhook_26b7aa1.json"


def old_entry(emu, sid, va, n):
    frozen = json.loads(FIXTURE.read_text())[sid]
    out = CODE + 0x9000
    raw = bytearray.fromhex(frozen["hex"])
    for fix in frozen["fixups"]:
        name, off = fix["symbol"], fix["offset"]
        target = emu.exports.get(name)
        if name.startswith("g_rs7_resume_"):
            target = DATA + 0x9000
            emu.uc.mem_write(target, struct.pack("<I", va + n))
        assert target is not None, name
        value = target - (out + off + 4) if fix["relative"] else target
        struct.pack_into("<I", raw, off, value & 0xFFFFFFFF)
    emu.uc.mem_write(out, bytes(raw))
    return out


@pytest.mark.parametrize("sid,n", [("SE1", 6), ("SE2", 6), ("SE3", 8)])
@pytest.mark.parametrize("setting", [0, 1, 2, 0x80000000, 0xFFFFFFFF])
@pytest.mark.parametrize("flags", [0x202, 0x2D7, 0xED7])
@pytest.mark.parametrize("misalign", [0, 4])
@pytest.mark.parametrize("disable_in_helper", [False, True])
def test_pilot_matches_frozen_old_stub(sid, n, setting, flags, misalign, disable_in_helper):
    emu = Emu(False)
    va = site_va(sid)
    old = old_entry(emu, sid, va, n)
    new = CODE + 0xA000
    callback_name = "Rs7_Mid_" + sid
    raw = game_text()[va - TEXT_VA : va - TEXT_VA + n]
    source = CODE + 0xB000
    emu.uc.mem_write(source, raw)
    args = [CODE + 0xC000, new, 256, source, va, n, new, emu.exports[callback_name]]
    emu.uc.mem_write(STACK + 0x70000, struct.pack("<8I", *args))
    emu.run(emu.exports["Rs7Mid_Build"], CODE + 0xC000, {UC_X86_REG_ESP: STACK + 0x70000})
    assert emu.uc.reg_read(ALL_REGS[0]) > 0

    esp, ebp = STACK + 0x80000 + misalign, STACK + 0x81000
    regs = dict(zip(ALL_REGS, [0x12345678, 0x23456789, DATA + 0x100, 0x456789AB, DATA + 0x100, 0x6789ABCD, ebp, esp]))
    regs[UC_X86_REG_EFLAGS] = flags
    # Both logger interfaces are intercepted, but the production callback and
    # all its argument/flag handling execute. Record the same logical input.
    calls = []

    def capture(uc, address, size, user):
        sp = uc.reg_read(UC_X86_REG_ESP)
        number, frame = struct.unpack("<2I", bytes(uc.mem_read(sp + 4, 8)))
        saved = list(struct.unpack("<9I", bytes(uc.mem_read(frame, 36))))
        if address == emu.exports.get("Rs7End_OnContext"):
            original_sp = saved[3]
            saved[3] -= 4  # legacy PUSHAD slot follows PUSHFD
        else:
            original_sp = frame + 36
        calls.append((number, tuple(saved), bytes(uc.mem_read(original_sp, 640))))
        # Deliberately dirty caller-saved state: the thunk must restore it.
        uc.reg_write(ALL_REGS[0], 0xBAD)
        uc.reg_write(UC_X86_REG_EFLAGS, 0x2D7)
        uc.reg_write(UC_X86_REG_XMM0, 0xBAD)
        uc.reg_write(UC_X86_REG_XMM7, 0xBAD)
        uc.mem_write(DATA + 0x500, struct.pack("<I", number))
        if disable_in_helper:
            uc.mem_write(emu.exports["g_rs7_endlog"], bytes(4))

    for name in ("Rs7End_OnSite", "Rs7End_OnContext"):
        if name in emu.exports:
            addr = emu.exports[name]
            emu.uc.mem_write(addr, b"\xc3")
            emu.uc.hook_add(UC_HOOK_CODE, capture, begin=addr, end=addr)

    # Track every machine write outside the dead stack scratch, not just the
    # known data buffer. Compare final bytes as well as the live stack image.
    writes = set()

    def track_write(uc, access, address, size, value, user):
        if not STACK <= address < STACK + 0x100000:
            writes.update(range(address, address + size))

    emu.uc.hook_add(UC_HOOK_MEM_WRITE, track_write)
    results = []
    for start in (old, new):
        calls.clear()
        writes.clear()
        emu.uc.mem_write(STACK + 0x7F000, bytes(0x3000))
        emu.uc.mem_write(esp, bytes((i * 17 + 3) & 255 for i in range(640)))
        emu.uc.mem_write(DATA, bytes(0x1000))
        emu.uc.mem_write(DATA + 0x100, struct.pack("<I", DATA + 0x300))
        emu.uc.mem_write(DATA + 0x3B0, struct.pack("<I", 0xC0FFEE))
        emu.uc.mem_write(emu.exports["g_rs7_endlog"], struct.pack("<I", setting))
        emu.uc.reg_write(UC_X86_REG_XMM0, 0x123456789ABCDEF)
        emu.uc.reg_write(UC_X86_REG_XMM7, 0xFEDCBA987654321)
        emu.run(start, va + n, regs)
        # Replay PUSH instructions make these slots live; alignment-only gaps
        # and dead scratch below the original stack are deliberately excluded.
        live_start = esp - (8 if sid == "SE1" else 4 if sid == "SE2" else 0)
        results.append(
            (
                emu.regs(*ALL_REGS),
                emu.uc.reg_read(UC_X86_REG_EFLAGS),
                emu.uc.reg_read(UC_X86_REG_EIP),
                emu.read(live_start, esp + 640 - live_start),
                emu.read(DATA, 0x1000),
                tuple(calls),
                tuple((a, emu.read(a, 1)) for a in sorted(writes)),
                emu.read(emu.exports["g_rs7_endlog"], 4),
                emu.uc.reg_read(UC_X86_REG_XMM0),
                emu.uc.reg_read(UC_X86_REG_XMM7),
            )
        )
    assert results[0] == results[1]
    assert len(results[1][5]) == bool(setting)
