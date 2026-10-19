"""Wrap placement (Rs7Mid_BuildWrap, ARCHITECTURE.md section 15 "Wrap placement") executed in Unicorn against both
thunks and both cleanups: a caller `push A1; push A0; call site; [add esp,8]` runs stock and hooked on identical
machine state. The callee counts its runs, returns A0 + A1 + ECX (flags from the ADD), scribbles over its first
argument slot and cleans up with `ret 8` (stdcall) or `ret` (cdecl). A function-entry wrap replays the prologue of a
small framed function."""

import struct

import pytest
from unicorn import UcError
from unicorn.x86_const import UC_X86_REG_EFLAGS, UC_X86_REG_ESP

from emu_harness import ALL_REGS, CODE, DATA, RET_MAGIC, STACK, Emu, needs_emu
from test_midhook_emu import _fp_state, _set_ts

pytestmark = needs_emu

STOCK, HOOKED, CALLEE, ENTRY = CODE + 0x1100, CODE + 0x1000, CODE + 0x4000, CODE + 0x2000
FRAMED, FRAMED_HOOKED, BEFORE, AFTER = CODE + 0x5000, CODE + 0x5100, CODE + 0x3000, CODE + 0x3100
SITE = HOOKED + 10
RUNS, SEEN = DATA + 0x700, DATA + 0x710
A0, A1 = 0x11223344, 0x01020304
ESP0 = STACK + 0x80000
FLAGS = (0x202, 0x2D7, 0xED7)  # IF; CF|PF|AF|ZF|SF; all arithmetic + DF + OF
# light: the builder mode argument (0 full, 1 light, 2 sse)
MODES = [(light, cdecl) for light in (0, 1, 2) for cdecl in (0, 1)]

CTX = "8B 44 24 04 8B 48 0C"  # eax = &Context, ecx = Context::esp (the frame, no locals)
CALLBACKS = {
    "noop": "C3",
    "double": "8B 44 24 04 D1 60 1C C3",  # c.eax *= 2
    # [SEEN] = Frame::Arg(0), [SEEN+4] = Frame::ret, [SEEN+8] = c.eax
    "record": CTX
    + " 8B 51 14 89 15"
    + struct.pack("<I", SEEN).hex()
    + " 8B 51 10 89 15"
    + struct.pack("<I", SEEN + 4).hex()
    + " 8B 50 1C 89 15"
    + struct.pack("<I", SEEN + 8).hex()
    + " C3",
}


def skip(pops: int) -> str:
    """Rs7Mid::WrapReturn(c, {2, conv, 0}, 77): esp past the frame, jump = the return address, then its pops."""
    return CTX + f" 83 C1 10 8B 11 89 50 28 83 C1 {4 + pops:02X} 89 48 0C C7 40 1C 4D 00 00 00 C3"


def caller(target: int, at: int, cdecl: int) -> bytes:
    code = b"\x68" + struct.pack("<I", A1) + b"\x68" + struct.pack("<I", A0)
    code += b"\xe8" + struct.pack("<i", target - (at + 15))
    return code + (bytes.fromhex("83 C4 08") if cdecl else b"") + b"\x90"


def callee(cdecl: int) -> bytes:
    body = bytes.fromhex("FF 05") + struct.pack("<I", RUNS)
    body += bytes.fromhex("8B 44 24 04 03 44 24 08 03 C1 C7 44 24 04 EF BE AD DE")
    return body + (b"\xc3" if cdecl else bytes.fromhex("C2 08 00"))


# push ebp; mov ebp,esp; sub esp,8 | mov eax,[ebp+8]; add eax,[ebp+0Ch]; add eax,ecx; mov esp,ebp; pop ebp; ret 8 / ret
PROLOGUE = bytes.fromhex("55 8B EC 83 EC 08")


def framed(cdecl: int) -> bytes:
    return (
        PROLOGUE + bytes.fromhex("8B 45 08 03 45 0C 03 C1 8B E5 5D") + (b"\xc3" if cdecl else bytes.fromhex("C2 08 00"))
    )


def machine(before: str | None, after: str | None, light: int, cdecl: int, entry: bool = False) -> Emu:
    emu = Emu(False)
    emu.uc.mem_write(CALLEE, callee(cdecl))
    emu.uc.mem_write(FRAMED, framed(cdecl))
    hooked = bytearray(framed(cdecl))
    hooked[:5] = b"\xe9" + struct.pack("<i", ENTRY - (FRAMED_HOOKED + 5))
    emu.uc.mem_write(FRAMED_HOOKED, bytes(hooked))
    emu.uc.mem_write(STOCK, caller(FRAMED if entry else CALLEE, STOCK, cdecl))
    emu.uc.mem_write(HOOKED, caller(FRAMED_HOOKED if entry else ENTRY, HOOKED, cdecl))
    pops = 0 if cdecl else 8
    if before:
        emu.uc.mem_write(BEFORE, bytes.fromhex(skip(pops) if before == "skip" else CALLBACKS[before]))
    if after:
        emu.uc.mem_write(AFTER, bytes.fromhex(CALLBACKS[after]))
    source = STACK + 0x60000
    emu.uc.mem_write(source, PROLOGUE)
    args = (RET_MAGIC, ENTRY, 256, ENTRY, 0 if entry else CALLEE, source if entry else 0, FRAMED_HOOKED, 6)
    args += (BEFORE if before else 0, AFTER if after else 0, 2, pops, 0, light)
    emu.uc.mem_write(STACK + 0x70000, struct.pack(f"<{len(args)}I", *args))
    emu.run(emu.exports["Rs7Mid_BuildWrap"], RET_MAGIC, {UC_X86_REG_ESP: STACK + 0x70000})
    assert emu.uc.reg_read(ALL_REGS[0]) > 0
    return emu


def run(emu: Emu, start: int, flags: int, cdecl: int):
    emu.uc.mem_write(RUNS, bytes(0x20))
    emu.uc.mem_write(ESP0 - 0x100, bytes((i * 17 + 3) & 255 for i in range(0x200)))
    regs = dict(zip(ALL_REGS, [0x111, 0x222, 5, 0x444, 0x555, 0x666, 0x777, ESP0]))
    regs[UC_X86_REG_EFLAGS] = flags
    emu.run(start, start + (18 if cdecl else 15), regs)
    esp = emu.uc.reg_read(UC_X86_REG_ESP)
    return dict(
        regs=list(emu.regs(*ALL_REGS)),
        flags=emu.uc.reg_read(UC_X86_REG_EFLAGS),
        stack=emu.read(esp, 0x100),
        runs=struct.unpack("<I", emu.read(RUNS, 4))[0],
        seen=struct.unpack("<3I", emu.read(SEEN, 12)),
    )


@pytest.mark.parametrize("light,cdecl", MODES)
@pytest.mark.parametrize("flags", FLAGS)
def test_after_matches_stock_and_reads_the_original_arguments(light, cdecl, flags):
    emu = machine(None, "record", light, cdecl)
    stock = run(emu, STOCK, flags, cdecl)
    assert stock["regs"][0] == A0 + A1 + 5 and stock["runs"] == 1 and stock["regs"][7] == ESP0
    new = run(emu, HOOKED, flags, cdecl)
    assert new["seen"] == (A0, SITE + 5, A0 + A1 + 5)  # the caller's slot (no DEADBEEF), return address, result
    new["seen"] = stock["seen"]
    assert new == stock


@pytest.mark.parametrize("light,cdecl", MODES)
@pytest.mark.parametrize("flags", FLAGS)
def test_before_skips_the_callee(light, cdecl, flags):
    emu = machine("skip", "record", light, cdecl)
    stock = run(emu, STOCK, flags, cdecl)
    new = run(emu, HOOKED, flags, cdecl)
    assert new["runs"] == 0 and new["seen"] == (0, 0, 0)  # neither the callee nor the after callback ran
    stock["regs"][0], stock["runs"], stock["seen"] = 77, 0, (0, 0, 0)
    if not cdecl:
        stock["flags"] = flags  # no ADD ran (cdecl: both end with the caller's `add esp, 8`)
    assert new == stock


@pytest.mark.parametrize("light,cdecl", MODES)
def test_after_replaces_the_return_value(light, cdecl):
    emu = machine(None, "double", light, cdecl)
    stock = run(emu, STOCK, 0x2D7, cdecl)
    new = run(emu, HOOKED, 0x2D7, cdecl)
    stock["regs"][0] *= 2
    assert new == stock


@pytest.mark.parametrize("light,cdecl", MODES)
@pytest.mark.parametrize("flags", FLAGS)
def test_function_entry_wrap_replays_the_prologue(light, cdecl, flags):
    emu = machine("noop", "record", light, cdecl, entry=True)
    stock = run(emu, STOCK, flags, cdecl)
    assert stock["regs"][0] == A0 + A1 + 5
    new = run(emu, HOOKED, flags, cdecl)
    assert new["seen"] == (A0, HOOKED + 15, A0 + A1 + 5)
    new["seen"] = stock["seen"]
    assert new == stock


@pytest.mark.parametrize("cdecl", [0, 1])
def test_light_wrap_entry_executes_no_fp(cdecl):
    emu = machine("noop", "noop", 1, cdecl)
    before = _fp_state(emu, True)
    _set_ts(emu, True)
    try:
        new = run(emu, HOOKED, 0x2D7, cdecl)
    finally:
        _set_ts(emu, False)
    assert new["regs"][0] == A0 + A1 + 5
    assert _fp_state(emu, False) == before
    full = machine("noop", "noop", 0, cdecl)
    _set_ts(full, True)
    try:
        with pytest.raises(UcError):
            run(full, HOOKED, 0x2D7, cdecl)
    finally:
        _set_ts(full, False)
