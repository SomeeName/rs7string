"""Old-vs-new and stock oracle of the calls written into relocated function copies (call-in entries, Rs7Mid::CallIn).

A copy's call edit replaces `length` stock bytes with `call <target>` + NOPs (platform/fncopy.h). Each run places one of
three variants at the same address and runs it to the end of the edit:
- old: `call <the frozen naked helper>` (tools/freeze_stubs.py fixture, midhook_oracle.FROZEN),
- new: `call <the entry Rs7Mid_BuildCallIn built for the C++ callback>`,
- stock: the stock bytes from the game image (a rel32 call retargeted to its original callee).
Compared: the eight GPRs (but `clobbers`), the arithmetic flags and DF (`flags`), x87 status / control / tag words,
XMM0 / XMM7, the live stack from the final ESP up, every other write, and each helper call's arguments. Helpers (DLL C
functions both the old helper and the callback call) are replaced by `ret` with a chosen EAX; game functions by the
spec's stand-ins. Only code this repository emits is frozen; the game image supplies the stock bytes.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field

from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.x86_const import (
    UC_X86_REG_EAX,
    UC_X86_REG_EBP,
    UC_X86_REG_ECX,
    UC_X86_REG_EDX,
    UC_X86_REG_EFLAGS,
    UC_X86_REG_EIP,
    UC_X86_REG_ESP,
    UC_X86_REG_FPCW,
    UC_X86_REG_FPSW,
    UC_X86_REG_FPTAG,
    UC_X86_REG_ST0,
    UC_X86_REG_XMM0,
    UC_X86_REG_XMM7,
)

from emu_harness import ALL_REGS, CALLINS, CODE, RET_MAGIC, STACK, TEXT_VA, Emu, callin_entry, game_text
from midhook_oracle import (
    DEFAULT_REGS,
    DIRTY_XMM,
    EBP,
    ESP,
    FLAG_PATTERNS,
    FROZEN,
    HELPER_AT,
    MISALIGN,
    flags_dead_at,
    place_frozen,
)

SITE_AT = CODE + 0xE3000  # the edit, one variant at a time
OLD_AT = CODE + 0xE3800  # the frozen helper
ENTRY_AT = CODE + 0xE4000  # call-in entries (0x40 each)
ARGS_AT = STACK + 0x70000
FLAGS = 0x8D5 | 0x400  # CF PF AF ZF SF OF, DF
LIVE = 256  # bytes above the call's ESP compared
GAME_FCW, FCW_AT = 0x27F, CODE + 0x6100  # the game's x87 control word in every run (53-bit precision)


def build_callin(emu: Emu, name: str, at: int = ENTRY_AT) -> int:
    """Run the DLL's Rs7Mid_BuildCallIn: the entry of the call-in `name` (CALLINS) at `at`."""
    return callin_entry(emu.uc, emu.exports, name, at, ARGS_AT, RET_MAGIC)


def local_callee(emu: Emu, callback: str) -> int:
    """The one function `callback` calls directly that the DLL does not export (to its int3 padding)."""
    from emu_harness import CS

    start, exported = emu.exports[callback], set(emu.exports.values())
    found = set()
    for insn in CS.disasm(emu.read(start, 0x400), start):
        if insn.bytes[0] == 0xCC:
            break
        if insn.mnemonic == "call" and insn.op_str.startswith("0x") and int(insn.op_str, 16) not in exported:
            found.add(int(insn.op_str, 16))
    assert len(found) == 1, (callback, [hex(a) for a in found])
    return found.pop()


@dataclass
class Spec:
    name: str  # the copy's call-in object minus g_rs7_callin_
    old: str  # the frozen helper
    stock_va: int  # the edit's stock address (game image); 0: a synthetic call (no stock bytes)
    length: int
    # exact: old == new; dead: the old helper left its own flags, the callback the incoming ones (proven dead at the
    # stock continuation by flags_dead_at); free: not compared (a call's callee left them; proven dead the same way)
    flags: str = "exact"
    clobbers: tuple = ()  # caller-saved GPRs the old helper left a C call's leftovers in, dead after a call
    helpers: dict = field(default_factory=dict)  # export -> nargs (cdecl, `ret`) or (nargs, pops) (stdcall, `ret N`)
    standins: dict = field(default_factory=dict)  # game function address -> code (all three runs)
    words: dict = field(default_factory=dict)  # exported dword -> value (every run)
    entries: dict = field(default_factory=dict)  # exported dword -> the call-in (CALLINS name) whose entry it holds
    frozen: tuple = ()  # frozen functions the old helper calls ("@NAME" fixups)
    # helpers that run game code: their record includes the x87 control word they are called with (the game's,
    # GAME_FCW, as under the old helper: Rs7Mid::GameFcw)
    game_fcw: tuple = ()
    # helpers the DLL does not export (the frozen fixups name them, freeze_stubs.py --map/--local): name -> the callback
    # whose one direct call to a non-exported function is it
    local: dict = field(default_factory=dict)

    @property
    def light(self) -> bool:  # as the C++ definition declares it (emu_harness.CALLINS)
        return CALLINS[self.name][1] == 1


@dataclass
class Case:
    id: str
    regs: dict = field(default_factory=dict)
    mem: dict = field(default_factory=dict)  # address -> bytes
    data: dict = field(default_factory=dict)  # export -> bytes written there
    stack: dict = field(default_factory=dict)  # offset from the call's ESP -> dword
    words: dict = field(default_factory=dict)
    rets: dict = field(default_factory=dict)  # helper -> EAX
    stock: bool = True  # the six-string path: new must equal the stock bytes


class Machine:
    def __init__(self, spec: Spec):
        self.spec, self.emu = spec, Emu(False)
        emu, uc = self.emu, self.emu.uc
        for name, via in spec.local.items():
            emu.exports[name] = local_callee(emu, via)
        helpers_at = {name: HELPER_AT + 0x100 * i for i, name in enumerate(spec.frozen)}
        for name, at in helpers_at.items():
            place_frozen(emu, name, at, helpers_at, {})
        self.old = place_frozen(emu, spec.old, OLD_AT, helpers_at, {})
        self.new = build_callin(emu, spec.name)
        for i, (word, name) in enumerate(spec.entries.items(), 1):
            entry = build_callin(emu, name, ENTRY_AT + 0x40 * i)
            uc.mem_write(emu.exports[word], struct.pack("<I", entry))
        for address, code in spec.standins.items():
            emu.read(address, len(code))
            uc.mem_write(address, bytes.fromhex(code))
        self.stock = bytearray(game_text()[spec.stock_va - TEXT_VA : spec.stock_va - TEXT_VA + spec.length])
        if self.stock and self.stock[0] == 0xE8:  # a rel32 call: its absolute callee from SITE_AT
            callee = spec.stock_va + 5 + struct.unpack("<i", self.stock[1:5])[0]
            self.stock[1:5] = struct.pack("<i", callee - (SITE_AT + 5))
        self.calls: list = []
        self.writes: dict = {}
        for name, nargs in spec.helpers.items():
            nargs, pops = nargs if isinstance(nargs, tuple) else (nargs, 0)
            uc.mem_write(emu.exports[name], struct.pack("<BH", 0xC2, pops) if pops else b"\xc3")
            uc.hook_add(UC_HOOK_CODE, self._helper(name, nargs), begin=emu.exports[name], end=emu.exports[name])
        uc.hook_add(UC_HOOK_MEM_WRITE, self._write)
        end = SITE_AT + spec.length
        uc.hook_add(UC_HOOK_CODE, lambda u, a, s, d: u.emu_stop(), begin=end, end=end)
        self.case: Case | None = None

    def _helper(self, name, nargs):
        def hook(uc, address, size, user):
            sp = uc.reg_read(UC_X86_REG_ESP)
            args = struct.unpack(f"<{nargs}I", bytes(uc.mem_read(sp + 4, 4 * nargs)))
            fcw = uc.reg_read(UC_X86_REG_FPCW) if name in self.spec.game_fcw else None
            self.calls.append((name, args, fcw))
            uc.reg_write(UC_X86_REG_EAX, self.case.rets.get(name, 0))
            uc.reg_write(UC_X86_REG_ECX, 0xC0C0C0C0)  # caller-saved registers dirtied
            uc.reg_write(UC_X86_REG_EDX, 0xD0D0D0D0)

        return hook

    def _write(self, uc, access, address, size, value, user):
        for i in range(size):
            self.writes[address + i] = (value >> (8 * i)) & 0xFF

    def code(self, which: str) -> bytes:
        if which == "stock":
            return bytes(self.stock)
        target = self.old if which == "old" else self.new
        return b"\xe8" + struct.pack("<i", target - (SITE_AT + 5)) + b"\x90" * (self.spec.length - 5)

    def run(self, which: str, case: Case, flags: int, misalign: int) -> dict:
        emu, uc, spec = self.emu, self.emu.uc, self.spec
        self.case = case
        uc.mem_write(SITE_AT, self.code(which).ljust(16, b"\xcc"))
        uc.ctl_remove_cache(SITE_AT, SITE_AT + 16)
        esp = ESP - misalign
        uc.mem_write(STACK + 0x7F000, bytes(0x3000))
        uc.mem_write(esp, bytes((i * 17 + 3) & 255 for i in range(LIVE)))
        uc.mem_write(EBP - 0x100, bytes((i * 29 + 7) & 255 for i in range(0x200)))
        for address, data in case.mem.items():
            emu.read(address, len(data))
            uc.mem_write(address, data)
        for name, data in case.data.items():
            uc.mem_write(emu.exports[name], data)
        for offset, value in case.stack.items():
            uc.mem_write(esp + offset, struct.pack("<I", value & 0xFFFFFFFF))
        for name, value in {**spec.words, **case.words}.items():
            uc.mem_write(emu.exports[name], struct.pack("<I", value))
        # fninit; fldcw [GAME_FCW]; fld1; fldpi: a game control word unlike the thunk's 37Fh
        uc.mem_write(FCW_AT, struct.pack("<H", GAME_FCW))
        preload = bytes.fromhex("DB E3 D9 2D") + struct.pack("<I", FCW_AT) + bytes.fromhex("D9 E8 D9 EB")
        uc.mem_write(CODE + 0x6000, preload)
        uc.emu_start(CODE + 0x6000, CODE + 0x6000 + len(preload))
        uc.reg_write(UC_X86_REG_XMM0, DIRTY_XMM[0])
        uc.reg_write(UC_X86_REG_XMM7, DIRTY_XMM[1])
        for reg, value in {**DEFAULT_REGS, UC_X86_REG_EBP: EBP, **case.regs, UC_X86_REG_ESP: esp}.items():
            uc.reg_write(reg, value)
        uc.reg_write(UC_X86_REG_EFLAGS, flags)
        self.calls.clear()
        self.writes.clear()
        uc.emu_start(SITE_AT, 0xFFFFFFF0, count=20000)
        assert uc.reg_read(UC_X86_REG_EIP) == SITE_AT + spec.length, (
            f"{which} stopped at {uc.reg_read(UC_X86_REG_EIP):#x}"
        )
        final = uc.reg_read(UC_X86_REG_ESP)
        return dict(
            regs=dict(zip(("eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp"), emu.regs(*ALL_REGS))),
            flags=uc.reg_read(UC_X86_REG_EFLAGS) & FLAGS,
            fpu=(uc.reg_read(UC_X86_REG_FPSW), uc.reg_read(UC_X86_REG_FPCW), uc.reg_read(UC_X86_REG_FPTAG)),
            xmm=(uc.reg_read(UC_X86_REG_XMM0), uc.reg_read(UC_X86_REG_XMM7)),
            st0=uc.reg_read(UC_X86_REG_ST0),  # an x87 result (the glow read's `fld`)
            stack=emu.read(final, esp + LIVE - final) if final <= esp + LIVE else b"",
            frame=emu.read(EBP - 0x100, 0x200),  # the copy's game frame (EBP-relative slots)
            writes={a: v for a, v in self.writes.items() if not STACK <= a < STACK + 0x100000},
            calls=list(self.calls),
        )


def diff(spec: Spec, want: dict, got: dict, flags_in: int) -> list[str]:
    """The keys where `got` (the new run) differs from `want` (old or stock)."""
    out = []
    for reg in want["regs"]:
        if reg not in spec.clobbers and want["regs"][reg] != got["regs"][reg]:
            out.append(f"{reg} {want['regs'][reg]:#x} != {got['regs'][reg]:#x}")
    if spec.flags == "exact" and want["flags"] != got["flags"]:
        out.append(f"flags {want['flags']:#x} != {got['flags']:#x}")
    if spec.flags == "dead" and got["flags"] != flags_in & FLAGS:
        out.append(f"flags {got['flags']:#x} not the incoming {flags_in & FLAGS:#x}")
    out += [key for key in ("fpu", "st0", "xmm", "stack", "frame", "writes", "calls") if want[key] != got[key]]
    return out


def matrix(specs: dict, which: str) -> list:
    """(spec name, case id) pairs: every case for "old", the six-string cases for "stock"."""
    return [(n, c.id) for n, (s, cases) in specs.items() for c in cases if which == "old" or (c.stock and s.stock_va)]


def check(machines: dict, specs: dict, name: str, case_id: str, against: str):
    spec, cases = specs[name]
    case = next(c for c in cases if c.id == case_id)
    machine = machines.setdefault(name, Machine(spec))
    for flags in FLAG_PATTERNS:
        for misalign in MISALIGN:
            want = machine.run(against, case, flags, misalign)
            got = machine.run("new", case, flags, misalign)
            assert not diff(spec, want, got, flags), (against, flags, misalign, diff(spec, want, got, flags))


def flags_dead(spec: Spec) -> bool:
    return flags_dead_at(spec.stock_va + spec.length)


__all__ = ["FROZEN", "Case", "Machine", "Spec", "build_callin", "check", "flags_dead", "matrix"]
