"""Call-site mid-hooks (Rs7Mid_BuildCall, ARCHITECTURE.md section 15 "Call sites") executed in Unicorn against both
thunks and both placements: a caller `push imm32; call site` runs stock and hooked on identical machine state. The
callee counts its runs, returns [esp+4] + ECX (flags from the ADD) and cleans up with `ret 4`."""

import struct

import pytest
from unicorn import UcError
from unicorn.x86_const import UC_X86_REG_EFLAGS, UC_X86_REG_ESP

from emu_harness import ALL_REGS, CODE, DATA, RET_MAGIC, STACK, Emu, needs_emu
from test_midhook_emu import _fp_state, _set_ts

pytestmark = needs_emu

STOCK, HOOKED, CALLEE, CALLBACK, ENTRY = CODE + 0x1100, CODE + 0x1000, CODE + 0x4000, CODE + 0x3000, CODE + 0x2000
SITE = HOOKED + 5
RUNS = DATA + 0x700
ARG = 0x11223344
ESP0 = STACK + 0x80000
FLAGS = (0x202, 0x2D7, 0xED7)  # IF; CF|PF|AF|ZF|SF; all arithmetic + DF + OF
# light: the builder mode argument (0 full, 1 light, 2 sse)
MODES = [(light, after) for light in (0, 1, 2) for after in (0, 1)]

CTX = "8B 44 24 04"  # mov eax, [esp+4]: the Context
CALLBACKS = {
    "noop": "C3",
    # Rs7Mid::Return(c, 77, 4): jump = [c.esp], c.esp += 8, c.eax = 77
    "skip": CTX + " 8B 48 0C 8B 11 89 50 28 83 C1 08 89 48 0C C7 40 1C 4D 00 00 00 C3",
    "ecx100": CTX + " C7 40 18 64 00 00 00 C3",  # c.ecx = 100
    "double": CTX + " D1 60 1C C3",  # c.eax *= 2
}


def caller(target: int, at: int) -> bytes:
    return b"\x68" + struct.pack("<I", ARG) + b"\xe8" + struct.pack("<i", target - (at + 10)) + b"\x90"


def machine(callback: str, light: int, after: int) -> Emu:
    emu = Emu(False)
    emu.uc.mem_write(
        CALLEE, bytes.fromhex("FF 05") + struct.pack("<I", RUNS) + bytes.fromhex("8B 44 24 04 03 C1 C2 04 00")
    )
    emu.uc.mem_write(STOCK, caller(CALLEE, STOCK))
    emu.uc.mem_write(HOOKED, caller(ENTRY, HOOKED))
    emu.uc.mem_write(CALLBACK, bytes.fromhex(CALLBACKS[callback]))
    args = (RET_MAGIC, ENTRY, 256, SITE, CALLEE, ENTRY, CALLBACK, light, after)
    emu.uc.mem_write(STACK + 0x70000, struct.pack("<9I", *args))
    emu.run(emu.exports["Rs7Mid_BuildCall"], RET_MAGIC, {UC_X86_REG_ESP: STACK + 0x70000})
    assert emu.uc.reg_read(ALL_REGS[0]) == (24 if after else 15)
    return emu


def run(emu: Emu, start: int, flags: int):
    emu.uc.mem_write(RUNS, bytes(4))
    emu.uc.mem_write(ESP0 - 0x100, bytes((i * 17 + 3) & 255 for i in range(0x200)))
    regs = dict(zip(ALL_REGS, [0x111, 0x222, 5, 0x444, 0x555, 0x666, 0x777, ESP0]))
    regs[UC_X86_REG_EFLAGS] = flags
    emu.run(start, start + 10, regs)
    esp = emu.uc.reg_read(UC_X86_REG_ESP)
    return dict(
        regs=list(emu.regs(*ALL_REGS)),
        flags=emu.uc.reg_read(UC_X86_REG_EFLAGS),
        stack=emu.read(esp, 0x100),
        runs=struct.unpack("<I", emu.read(RUNS, 4))[0],
    )


@pytest.mark.parametrize("light,after", MODES)
@pytest.mark.parametrize("flags", FLAGS)
def test_noop_call_hook_matches_stock(light, after, flags):
    emu = machine("noop", light, after)
    stock = run(emu, STOCK, flags)
    assert stock["regs"][0] == ARG + 5 and stock["runs"] == 1 and stock["regs"][7] == ESP0
    assert run(emu, HOOKED, flags) == stock


@pytest.mark.parametrize("light", [0, 1, 2])
@pytest.mark.parametrize("flags", FLAGS)
def test_before_skips_the_callee_and_pops_its_arguments(light, flags):
    emu = machine("skip", light, 0)
    stock = run(emu, STOCK, flags)
    new = run(emu, HOOKED, flags)
    assert new["runs"] == 0
    assert new["flags"] == flags  # no ADD ran; the callback left the incoming flags
    stock["regs"][0], stock["flags"], stock["runs"] = 77, flags, 0
    assert new == stock


@pytest.mark.parametrize("light", [0, 1, 2])
def test_before_edits_the_callee_arguments(light):
    emu = machine("ecx100", light, 0)
    new = run(emu, HOOKED, 0x202)
    assert new["regs"][0] == ARG + 100 and new["regs"][2] == 100 and new["runs"] == 1


@pytest.mark.parametrize("light", [0, 1, 2])
@pytest.mark.parametrize("flags", FLAGS)
def test_after_replaces_the_return_value(light, flags):
    emu = machine("double", light, 1)
    stock = run(emu, STOCK, flags)
    new = run(emu, HOOKED, flags)
    stock["regs"][0] = 2 * (ARG + 5)
    assert new == stock  # flags are the callee's ADD flags, the stack is past its `ret 4`


@pytest.mark.parametrize("after", [0, 1])
def test_light_call_entry_executes_no_fp(after):
    emu = machine("noop", 1, after)
    before = _fp_state(emu, True)
    _set_ts(emu, True)
    try:
        new = run(emu, HOOKED, 0x2D7)
    finally:
        _set_ts(emu, False)
    assert new["regs"][0] == ARG + 5
    assert _fp_state(emu, False) == before
    full = machine("noop", 0, after)
    _set_ts(full, True)
    try:
        with pytest.raises(UcError):
            run(full, HOOKED, 0x2D7)
    finally:
        _set_ts(full, False)
