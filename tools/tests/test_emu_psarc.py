"""The plain-PSARC unit (core/plainpsarc.h, sites PP_TOC / PP_SNGF / PP_SNGD) in Unicorn on the game snapshot.

Stock and patched emulators (test_emu.Emu: the patched one has every site written, the PP_* ones
included). Encrypted inputs must take the stock path with the stock register state; plain inputs (PSARC
archiveFlags bit 4 clear, SNG flags 1) take the plain path of the plain-PSARC enabler build (PR #1).
Nothing is decrypted: the SNG decrypt the stub falls through to is replaced by a marker function.
"""

from __future__ import annotations

import struct

import pytest
from unicorn import UC_HOOK_CODE
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

from test_emu import CODE, DATA, STACK, Emu, site_va

TOC = site_va("PP_TOC")  # 0x8A3643, mov ecx,10h
SNGF = site_va("PP_SNGF")  # 0x558C1A, cmp [esp+68h],3
SNGD = site_va("PP_SNGD")  # 0x558CD4, call SNG decrypt
TOC_BLOCK, TOC_AFTER = TOC - 3, TOC + 0x4D
SNGF_OK, SNGF_REJECT = SNGF + 0xB, SNGF + 0xB + 0x636  # jne rel32 0x636 (sites.json expect)
FRAME = STACK + 0x80000
REGS = (
    UC_X86_REG_EAX,
    UC_X86_REG_EBX,
    UC_X86_REG_ECX,
    UC_X86_REG_EDX,
    UC_X86_REG_ESI,
    UC_X86_REG_EDI,
    UC_X86_REG_EBP,
    UC_X86_REG_ESP,
)
MARKER = CODE + 0x3000  # the "game decrypt": mov al,5Ah; ret


@pytest.fixture(scope="module")
def emus():
    stock, patched = Emu(patched=False), Emu(patched=True)
    patched.uc.mem_write(patched.exports["g_rs7_skip_PP_TOC"], struct.pack("<I", TOC_AFTER))
    patched.uc.mem_write(MARKER, bytes.fromhex("B0 5A C3"))
    patched.uc.mem_write(patched.exports["g_rs7_target_PP_SNGD"], struct.pack("<I", MARKER))
    return stock, patched


def test_sites_are_the_known_addresses():
    assert (TOC, SNGF, SNGD) == (0x8A3643, 0x558C1A, 0x558CD4)


def toc_setup(emu: Emu, archive_flags: int):
    toc_ptr, toc = DATA + 0x40000, DATA + 0x41000
    emu.uc.mem_write(toc_ptr, struct.pack("<I", toc))
    emu.uc.mem_write(FRAME - 0x74, struct.pack("<I", toc_ptr))
    emu.uc.mem_write(FRAME - 0x54, struct.pack("<I", archive_flags))
    regs = {
        UC_X86_REG_EBP: FRAME,
        UC_X86_REG_ESP: FRAME - 0x200,
        UC_X86_REG_EAX: 0x1111,
        UC_X86_REG_EBX: 0x2222,
        UC_X86_REG_ECX: 0x3333,
        UC_X86_REG_EDX: 0x4444,
        UC_X86_REG_ESI: 0x5555,
        UC_X86_REG_EDI: 0x6666,
    }
    return toc_ptr, toc, regs


@pytest.mark.parametrize("flags", [4, 4 | 1, 0xFF])
def test_toc_encrypted_takes_the_stock_decrypt_path(emus, flags):
    """Bit 4 set: the stub runs mov ecx,10h and resumes at mov esi,<key> with the stock registers (EFLAGS
    differ only by the test; they are dead: rep movsd reads DF only, add edi,-20h sets them anew)."""
    out = []
    for emu in emus:
        _, _, regs = toc_setup(emu, flags)
        emu.run(TOC_BLOCK, TOC + 5, regs)
        out.append(emu.regs(*REGS))
    assert out[0] == out[1]
    assert out[1][2] == 0x10


@pytest.mark.parametrize("flags", [0, 1, 0xFB])
def test_toc_plain_skips_the_decrypt_block(emus, flags):
    """Bit 4 clear: straight to the block's end with what its tail sets (edx = [ebp-74h], edi = [edx]) and
    the stack unchanged (the skipped pushes are what add esp,28h balanced)."""
    _, patched = emus
    toc_ptr, toc, regs = toc_setup(patched, flags)
    patched.run(TOC_BLOCK, TOC_AFTER, regs)
    eax, ebx, ecx, edx, esi, edi, ebp, esp = patched.regs(*REGS)
    assert (eax, edx, edi) == (toc_ptr, toc_ptr, toc)
    assert (ebx, ebp, esp) == (0x2222, FRAME, FRAME - 0x200)


def run_sngf(emu: Emu, flags: int) -> int:
    esp = FRAME - 0x400
    emu.uc.mem_write(esp + 0x68, struct.pack("<I", flags))
    stop = emu.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, u: uc.emu_stop(), begin=SNGF_REJECT, end=SNGF_REJECT)
    try:
        emu.uc.reg_write(UC_X86_REG_ESP, esp)
        emu.uc.emu_start(SNGF, SNGF_OK, count=50)
    finally:
        emu.uc.hook_del(stop)
    assert emu.uc.reg_read(UC_X86_REG_ESP) == esp
    return emu.uc.reg_read(UC_X86_REG_EIP)


@pytest.mark.parametrize("flags", [0, 1, 2, 3, 4, 0x101, 0xFFFFFFFF])
def test_sng_flags_accepts_1_and_3(emus, flags):
    stock, patched = emus
    assert run_sngf(stock, flags) == (SNGF_OK if flags == 3 else SNGF_REJECT)
    assert run_sngf(patched, flags) == (SNGF_OK if flags in (1, 3) else SNGF_REJECT)


def run_sngd(emu: Emu, flags: int, payload: bytes, out_len: int) -> tuple[int, bytes]:
    """The game's call at PP_SNGD with an SNG at DATA+0x50000 (header, 16-byte IV slot, payload)."""
    sng, key, out = DATA + 0x50000, DATA + 0x51000, DATA + 0x52000
    emu.uc.mem_write(sng, struct.pack("<II", 0x4A, flags) + b"\x00" * 16 + payload)
    emu.uc.mem_write(out, b"\xee" * 64)
    esp = FRAME - 0x800
    args = struct.pack("<IIIIII", sng + 8, 16 + len(payload), key, 0x40, out, out_len)
    emu.uc.mem_write(esp, args)
    emu.run(SNGD, SNGD + 5, {UC_X86_REG_ESP: esp, UC_X86_REG_EAX: 0})
    assert emu.uc.reg_read(UC_X86_REG_ESP) == esp
    return emu.uc.reg_read(UC_X86_REG_EAX) & 0xFF, emu.read(out, 64)


def test_sng_plain_payload_is_copied(emus):
    _, patched = emus
    payload = bytes(range(0x78, 0x78 + 40))
    al, out = run_sngd(patched, 1, payload, len(payload))
    assert al == 1
    assert out[:40] == payload and out[40:] == b"\xee" * 24


def test_sng_plain_payload_too_large_fails(emus):
    _, patched = emus
    al, out = run_sngd(patched, 1, b"\x01" * 40, 39)
    assert al == 0 and out == b"\xee" * 64


@pytest.mark.parametrize("flags", [3, 0, 2])
def test_sng_other_flags_go_to_the_game_decrypt(emus, flags):
    _, patched = emus
    al, out = run_sngd(patched, flags, b"\x01" * 40, 40)
    assert al == 0x5A and out == b"\xee" * 64
