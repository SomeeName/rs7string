"""GLW (0x54438B, stock Rnd_StringRenderer_UpdateGlow) and the GSGL copy's call-in Rs7_CallIn_GlowColour against the
stock bytes: RSMods' custom colours give a string with an Enabled hue of 0..3 a stale glow (features/colours/colours.h).

With g_rs7_glow_fix = 0, or an Enabled hue outside 0..3, the hooked code leaves exactly the stock state; with
the switch on and a pure red Enabled colour, the glow RGB copied to [ebp-28h..-20h] is HSL(356, 1, 0.5).
"""

from __future__ import annotations

import struct

import pytest
from unicorn.x86_const import (
    UC_X86_REG_EAX,
    UC_X86_REG_EBP,
    UC_X86_REG_EBX,
    UC_X86_REG_ECX,
    UC_X86_REG_EDI,
    UC_X86_REG_EDX,
    UC_X86_REG_EFLAGS,
    UC_X86_REG_ESI,
    UC_X86_REG_ESP,
    UC_X86_REG_FPSW,
)

from emu_common import DLL
from callin_oracle import build_callin
from emu_harness import ALL_REGS, DATA, RET_MAGIC, SNAPSHOT, STACK, Emu, site_va

pytestmark = pytest.mark.skipif(
    not DLL.exists() or not (SNAPSHOT / "memory.bin").exists(), reason="needs built DLL and snapshot"
)

LOAD, COPY_END = 0x544384, 0x54439C  # the glow pointer load .. the first instruction after the RGB copy
MGR = DATA + 0x10000
RGB = DATA + 0x20000  # [0] glow, [1] Enabled
FRAME = STACK + 0x81000
STALE = (0.02, 0.01, 0.0)
RED = (128 / 255, 0.0, 0.0)
GREEN = (0.0, 1.0, 0.0)
FIXED = (1.0, 0.0, 1 - (356 / 60 - 5))  # HSL(356, 1, 0.5)


@pytest.fixture(scope="module")
def emus():
    return Emu(False), Emu(True)


def floats(emu: Emu, va: int) -> tuple[float, ...]:
    return struct.unpack("<3f", emu.read(va, 12))


def setup(emu: Emu, mode: int, string: int, enabled, native: int, fix: int) -> int:
    """A colour manager whose state-3 (glow) and state-1 (Enabled) pointers for (mode, string) are set."""
    emu.read(MGR, 1)
    emu.uc.mem_write(MGR, bytes(0x1BD0))
    emu.uc.mem_write(MGR + 0x348, struct.pack("<I", mode))
    emu.uc.mem_write(RGB, struct.pack("<3f", *STALE) + struct.pack("<3f", *enabled))
    stride, glow, ena = (0xE0, 0xE30, 0xDF0) if native else (0xA8, 0x398, 0x368)
    index = mode * stride + string
    emu.uc.mem_write(MGR + glow + index * 4, struct.pack("<I", RGB))
    emu.uc.mem_write(MGR + ena + index * 4, struct.pack("<I", RGB + 12))
    emu.uc.mem_write(emu.exports["g_rs7_glow_fix"], struct.pack("<I", fix))
    emu.uc.mem_write(emu.exports["g_rs7_native_colours"], struct.pack("<I", native))
    return mode * 0xA8 + string  # the copy and the stock code both index with the stock stride


def machine(eax: int, string: int, esp: int) -> dict[int, int]:
    return {
        UC_X86_REG_EAX: eax,
        UC_X86_REG_EBX: 0x555,
        UC_X86_REG_ECX: MGR,
        UC_X86_REG_EDX: string,
        UC_X86_REG_ESI: 0x222,
        UC_X86_REG_EDI: 0x111,
        UC_X86_REG_EBP: FRAME,
        UC_X86_REG_ESP: esp,
        UC_X86_REG_EFLAGS: 0x202,
    }


def run_stock_path(emu: Emu, mode: int, string: int, enabled, fix: int):
    eax = setup(emu, mode, string, enabled, 0, fix)
    emu.uc.mem_write(FRAME - 0x28, bytes(12))
    regs = machine(eax, string, STACK + 0x80000)
    emu.run(LOAD, COPY_END, regs)
    others = emu.regs(UC_X86_REG_EBX, UC_X86_REG_EDX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_ESP)
    return emu.regs(*ALL_REGS), others, floats(emu, FRAME - 0x28)


@pytest.mark.parametrize("mode", [0, 1])
@pytest.mark.parametrize("string", [0, 5])
@pytest.mark.parametrize("enabled,fix", [(RED, 0), (GREEN, 1), (GREEN, 0)])
def test_glw_is_stock_when_off_or_not_pure_red(emus, mode, string, enabled, fix):
    stock, patched = emus
    assert run_stock_path(stock, mode, string, enabled, fix) == run_stock_path(patched, mode, string, enabled, fix)


@pytest.mark.parametrize("mode", [0, 1])
@pytest.mark.parametrize("string", [0, 5])
def test_glw_gives_pure_red_its_wrapped_glow(emus, mode, string):
    stock, patched = emus
    _, others_stock, rgb_stock = run_stock_path(stock, mode, string, RED, 1)
    _, others, rgb = run_stock_path(patched, mode, string, RED, 1)
    assert rgb_stock == pytest.approx(STALE, abs=1e-6)
    assert rgb == pytest.approx(FIXED, abs=1e-6)
    assert others == others_stock


def call_glow_colour(emu: Emu, mode: int, string: int, enabled, native: int, fix: int):
    eax = setup(emu, mode, string, enabled, native, fix)
    esp = STACK + 0x80000
    emu.uc.mem_write(esp, struct.pack("<I", RET_MAGIC))
    regs = machine(eax, string, esp)
    fpsw = emu.uc.reg_read(UC_X86_REG_FPSW)
    if not hasattr(emu, "glow_callin"):  # the GSGL copy's call-in entry (Rs7Mid::CallIn, full mode)
        emu.glow_callin = build_callin(emu, "GlowColour")
    emu.run(emu.glow_callin, RET_MAGIC, regs)
    kept = emu.regs(UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP)
    assert kept == (0x555, MGR, string, 0x222, 0x111, FRAME)
    assert emu.uc.reg_read(UC_X86_REG_ESP) == esp + 4
    assert emu.uc.reg_read(UC_X86_REG_FPSW) == fpsw
    pointer = emu.uc.reg_read(UC_X86_REG_EAX)
    return pointer, floats(emu, pointer)


@pytest.mark.parametrize("native", [0, 1])
@pytest.mark.parametrize("mode", [0, 1])
def test_gsgl_copy_call_corrects_only_pure_red_with_the_switch(emus, native, mode):
    _, patched = emus
    assert call_glow_colour(patched, mode, 0, RED, native, 0) == (RGB, pytest.approx(STALE, abs=1e-6))
    assert call_glow_colour(patched, mode, 0, GREEN, native, 1) == (RGB, pytest.approx(STALE, abs=1e-6))
    pointer, rgb = call_glow_colour(patched, mode, 0, RED, native, 1)
    assert pointer != RGB and rgb == pytest.approx(FIXED, abs=1e-6)


def test_glw_site_is_the_rgb_copy():
    assert site_va("GLW") == 0x54438B
