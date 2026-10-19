"""The relocator's newer encodings (INC/DEC r/m8, IMUL, TEST r/m,imm, operand-size prefix 66) on real jmp sites:
the relocated copy (Rs7Mid_Relocate of the built DLL) executes like the stock game bytes in Unicorn.

Each run starts from the same registers, flags and memory (unmapped pages appear on first touch with content
derived from their address) and stops where control leaves the code (the fall-through end or an outbound branch
target). Compared: all eight GPRs, EFLAGS, the exit address and every memory write in order.
"""

import random
import struct

import pytest
from unicorn import UC_ARCH_X86, UC_HOOK_CODE, UC_HOOK_MEM_UNMAPPED, UC_HOOK_MEM_WRITE, UC_MODE_32, UC_PROT_ALL, Uc
from unicorn.x86_const import UC_X86_REG_EFLAGS, UC_X86_REG_EIP, UC_X86_REG_ESP

import midhook_survey
import sitecheck
from emu_common import DLL, EXE, SITES, needs_exe_and_dll
from emu_harness import ALL_REGS
from sitetable import load

pytestmark = needs_exe_and_dll

# Sites the encodings above made relocatable (survey: rejected before), one or more per new form.
SITES_NEW = {
    "TF1": "dec bl",
    "BT1": "pop edi; pop esi; pop ebx; pop ebp; ret 14h",
    "PUBEDGE": "dec al after internal branches",
    "UP1": "inc al; jb out",
    "PD3": "inc bl; jb out",
    "PD5": "inc bl; inc esi; jb out",
    "V2": "imul r,r,imm32",
    "CP1": "imul r,r,imm32",
    "CLBA": "imul r,r,imm32; lea",
    "NG1": "imul r,r,imm8",
    "CC3": "test r/m32,imm32 (SIB)",
    "CLBM": "test r/m32,imm32 (SIB)",
    "CS1": "66 mov word [r+disp32]",
    "PSU1": "66 cmp; jae out",
    "PSM1": "66 cmp; jae out",
}
# Still rejected on purpose (survey reason): none since FCOMP m32 (W) is accepted.
KNOWN_REJECTED: dict[str, str] = {}
OUT = 0x30000000  # where the relocated copy runs
STACK = 0x31000000
FLAGS = (0x202, 0x2D7, 0xED7)  # IF; all arithmetic flags; plus DF and OF


@pytest.fixture(scope="module")
def game():
    _, sites = load(SITES)
    text, text_va = midhook_survey.game_text(EXE)
    return text, text_va, {s.id: s for s in sites}


@pytest.fixture(scope="module")
def relocate():
    return midhook_survey.Relocator(DLL)


def site_bytes(game, sid):
    text, text_va, sites = game
    ok, msg, va, _ = sitecheck.check_site(text, text_va, sites[sid])
    assert ok, f"{sid}: {msg}"
    return va, text[va - text_va : va - text_va + sites[sid].length]


def run(game, code_va, code, regs, flags):
    """(GPRs, EFLAGS, exit address, writes) of executing `code` at `code_va` until control leaves it."""
    text, text_va, _ = game
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    page = code_va & ~0xFFF
    uc.mem_map(page, 0x2000, UC_PROT_ALL)
    uc.mem_write(code_va, code)
    writes, exits = [], []

    def lazy(uc, access, address, size, value, user):
        base = address & ~0xFFF
        uc.mem_map(base, 0x1000, UC_PROT_ALL)
        uc.mem_write(base, random.Random(base).randbytes(0x1000))
        return True

    def leave(uc, address, size, user):
        if not code_va <= address < code_va + len(code):
            exits.append(address)
            uc.emu_stop()

    uc.hook_add(UC_HOOK_MEM_UNMAPPED, lazy)
    uc.hook_add(UC_HOOK_MEM_WRITE, lambda uc, a, address, size, value, u: writes.append((address, size, value)))
    uc.hook_add(UC_HOOK_CODE, leave)
    for r, v in zip(ALL_REGS, regs):
        uc.reg_write(r, v)
    uc.reg_write(UC_X86_REG_ESP, STACK + 0x800)
    uc.reg_write(UC_X86_REG_EFLAGS, flags)
    uc.emu_start(code_va, 0xFFFFFFFF, count=64)
    assert exits, f"did not leave the code: stopped at {uc.reg_read(UC_X86_REG_EIP):#x}"
    regs_out = tuple(uc.reg_read(r) for r in ALL_REGS)
    return regs_out, uc.reg_read(UC_X86_REG_EFLAGS), exits[0], writes


def register_sets(seed):
    """Random GPR values; every second set has small low bytes so the 8-bit loop counters take both branches."""
    rng = random.Random(seed)
    for k in range(8):
        regs = [rng.getrandbits(32) for _ in ALL_REGS]
        if k % 2:
            regs = [v & ~0xFF | rng.randrange(8) for v in regs]
        yield regs


@pytest.mark.parametrize("sid", sorted(SITES_NEW))
def test_relocated_copy_executes_like_the_stock_bytes(game, relocate, sid):
    va, raw = site_bytes(game, sid)
    copy = relocate(raw, va, OUT)
    assert copy is not None, f"{sid} ({SITES_NEW[sid]}) rejected"
    back = struct.pack("<Bi", 0xE9, va + len(raw) - (OUT + len(copy) + 5))  # as Rs7Mid_Build's jump back
    exits = set()
    for regs in register_sets(va):
        for flags in FLAGS:
            stock = run(game, va, raw, regs, flags)
            relocated = run(game, OUT, copy + back, regs, flags)
            assert relocated == stock, f"{sid} regs {[hex(v) for v in regs]} flags {flags:#x}"
            exits.add(stock[2])
    if sid.startswith(("UP", "PD", "PSU", "PSM")):
        assert len(exits) == 2, f"{sid}: both the fall-through and the outbound branch ran ({exits})"


def test_survey_rejects_only_the_known_sites(game, relocate):
    text, text_va, sites = game
    rows = midhook_survey.survey(text, text_va, sites.values(), relocate)
    rejected = {sid: reason for sid, _, _, reason in rows if reason}
    assert rejected == KNOWN_REJECTED
