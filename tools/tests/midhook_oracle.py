"""Old-vs-new differential oracle for sites converted from naked stubs to C++ mid-hook callbacks.

The frozen old stubs (tools/tests/fixtures/midhook_cold_951f935.json, produced by
tools/freeze_stubs.py from the DLL built at 951f935) and the production generated entry + callback
run on identical machine state in Unicorn. Compared after the run: all eight GPRs, EFLAGS, the final
EIP (which exit was taken), x87 state, XMM0/XMM7, the live stack (from the final ESP up), a data
page, every other machine write (address and final byte), exported setting variables, and the
complete input of every helper call (arguments, memory the arguments point at, call order).

Only code emitted by this repository is frozen; no game bytes are stored. The game image is used
only for the displaced instruction bytes of a site, as in the existing emulator tests.
"""

from __future__ import annotations

import json
import struct
from dataclasses import dataclass, field
from pathlib import Path

from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.x86_const import (
    UC_X86_REG_EAX,
    UC_X86_REG_EBP,
    UC_X86_REG_EBX,
    UC_X86_REG_ECX,
    UC_X86_REG_EDI,
    UC_X86_REG_EDX,
    UC_X86_REG_EFLAGS,
    UC_X86_REG_EIP,
    UC_X86_REG_ESI,
    UC_X86_REG_ESP,
    UC_X86_REG_FPCW,
    UC_X86_REG_FPSW,
    UC_X86_REG_FPTAG,
    UC_X86_REG_ST0,
    UC_X86_REG_ST1,
    UC_X86_REG_XMM0,
    UC_X86_REG_XMM7,
)

import capstone
from capstone import x86_const as X86

from test_emu import ALL_REGS, CODE, DATA, STACK, TEXT_VA, Emu, game_text, site_target, site_va

FIXTURE = Path(__file__).with_name("fixtures") / "midhook_cold_951f935.json"
FROZEN = json.loads(FIXTURE.read_text())

OLD_AT, NEW_AT, SOURCE_AT, ARGS_AT, HELPER_AT = (
    CODE + 0x9000,
    CODE + 0xA000,
    CODE + 0xB000,
    CODE + 0xC000,
    CODE + 0xD000,
)
SLOT_AT = DATA + 0x9000  # resume/target slots of the frozen stubs
# Switches the frozen stubs read that the DLL no longer exports (retired in 1.0 with the legacy paths): the frozen code
# reads a slot holding the only value the switch can still have, so it is compared in the configuration that remains.
RETIRED_WORDS = {"g_rs7_tuner_on": 1}  # [tuner] seven_slot: always on
ESP, EBP = STACK + 0x80000, STACK + 0x81000
LIVE_ABOVE = 640  # bytes above the original ESP that are compared
ARITH = 0x8D5  # CF PF AF ZF SF OF
FLAG_PATTERNS = (0x202, 0x2D7, 0xED7)  # IF; CF|PF|AF|ZF|SF; all arithmetic flags + DF + OF
MISALIGN = (0, 4)
DEFAULT_REGS = dict(zip(ALL_REGS[:7], [0x12345678, 0x23456789, 0x3456789A, 0x456789AB, 0x56789ABC, 0x6789ABCD, EBP]))
DIRTY_XMM = (0x123456789ABCDEF, 0xFEDCBA987654321)


@dataclass(frozen=True)
class Sym:
    """An address inside an exported variable (resolved against the DLL under test)."""

    name: str
    offset: int = 0


def resolve(emu: Emu, address) -> int:
    return emu.exports[address.name] + address.offset if isinstance(address, Sym) else address


@dataclass
class Helper:
    """A C helper the stub / callback calls: replaced by `ret` and recorded."""

    nargs: int = 0
    deref: tuple = ()  # (argument index, byte count): memory the argument points at
    ret: int = 0  # EAX the call returns (caller-saved registers are dirtied)


@dataclass
class Spec:
    sid: str
    length: int  # displaced bytes
    helpers: dict = field(default_factory=dict)  # export name -> Helper
    flags: str = "exact"  # exact: old == new; dead: old arbitrary, new == incoming
    xmm: str = "restored"  # exact: old == new (old saves SSE); restored: new == incoming
    exits: tuple = ("resume",)  # where the run ends: resume / target
    pushed: int = 0  # bytes the stolen instructions push (live below the site ESP)
    patch: object = field(default_factory=dict)  # address -> bytes, or callable(machine) -> that dict
    words: dict = field(default_factory=dict)  # exported variable -> dword reset before every run
    setup_mem: dict = field(default_factory=dict)
    extra_cmp: tuple = ()  # (address | Sym, size): memory ranges compared in addition


@dataclass
class Case:
    id: str
    regs: dict = field(default_factory=dict)
    mem: dict = field(default_factory=dict)  # address -> bytes, written before every run
    stack: dict = field(default_factory=dict)  # offset from the site's ESP -> dword (misalignment-proof)
    ebp: dict = field(default_factory=dict)  # offset from EBP -> dword
    words: dict = field(default_factory=dict)  # exported variable -> dword
    rets: dict = field(default_factory=dict)  # helper name -> EAX (overrides Helper.ret)


_FLAGS = {"AF": 1 << 0, "CF": 1 << 1, "SF": 1 << 2, "ZF": 1 << 3, "PF": 1 << 4, "OF": 1 << 5}


def _flag_set(insn, prefixes) -> set:
    return {flag for flag in _FLAGS for prefix in prefixes if insn.eflags & getattr(X86, f"X86_EFLAGS_{prefix}_{flag}")}


def flags_dead_at(address: int, budget: int = 40) -> bool:
    """Static evidence that no arithmetic flag is read at `address` before it is rewritten: linear and
    branch-following scan of the game's code. A call or return ends the scan as dead (flags are not part
    of the calling convention). False = not proven within the budget."""
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    text = game_text()
    todo, seen = [(address, frozenset())], set()
    while todo:
        pc, defined = todo.pop()
        while True:
            if budget <= 0 or (pc, defined) in seen:
                return False if budget <= 0 else True
            seen.add((pc, defined))
            budget -= 1
            insn = next(md.disasm(text[pc - TEXT_VA : pc - TEXT_VA + 16], pc), None)
            if insn is None:
                return False
            if _flag_set(insn, ("TEST",)) - defined:
                return False
            defined = defined | _flag_set(insn, ("MODIFY", "RESET", "SET", "UNDEFINED"))
            if len(defined) == 6 or insn.mnemonic in ("call", "ret"):
                break
            if insn.mnemonic == "jmp" or insn.mnemonic.startswith("j"):
                from capstone.x86_const import X86_OP_IMM

                if insn.operands[0].type != X86_OP_IMM:
                    return False
                if insn.mnemonic != "jmp":
                    todo.append((insn.operands[0].imm, defined))
                    pc += insn.size
                    continue
                pc = insn.operands[0].imm
                continue
            pc += insn.size
    return True


def place_frozen(emu: Emu, name: str, at: int, helpers_at: dict, slots: dict) -> int:
    blob = FROZEN[name]
    raw = bytearray.fromhex(blob["hex"])
    for fx in blob["fixups"]:
        sym = fx["symbol"]
        if sym.startswith("@"):
            target = helpers_at[sym[1:]] + fx["addend"]
        elif sym.startswith(("g_rs7_resume_", "g_rs7_target_")) or sym in RETIRED_WORDS:
            target = slots[sym] + fx["addend"]
        else:
            target = emu.exports[sym] + fx["addend"]
        value = target - (at + fx["offset"] + 4) if fx["relative"] else target
        struct.pack_into("<I", raw, fx["offset"], value & 0xFFFFFFFF)
    emu.uc.mem_write(at, bytes(raw))
    return at


def build_new(emu: Emu, sid: str, va: int, n: int) -> int:
    raw = game_text()[va - TEXT_VA : va - TEXT_VA + n]
    emu.uc.mem_write(SOURCE_AT, raw)
    args = [CODE + 0xE000, NEW_AT, 256, SOURCE_AT, va, n, NEW_AT, emu.exports["Rs7_Mid_" + sid]]
    emu.uc.mem_write(STACK + 0x70000, struct.pack("<8I", *args))
    emu.run(emu.exports["Rs7Mid_Build"], CODE + 0xE000, {UC_X86_REG_ESP: STACK + 0x70000})
    assert emu.uc.reg_read(UC_X86_REG_EAX) > 0, f"mid-hook builder rejected {sid}"
    return NEW_AT


def brief(old, new) -> str:
    """A short description of the first difference (bytes / tuples), not the whole value."""
    if isinstance(old, (bytes, bytearray)) and isinstance(new, (bytes, bytearray)) and len(old) == len(new):
        i = next(i for i in range(len(old)) if old[i] != new[i])
        return (
            f"first difference at +{i:#x}: old {bytes(old[i : i + 16]).hex(' ')} new {bytes(new[i : i + 16]).hex(' ')}"
        )
    return f"old {str(old)[:300]} new {str(new)[:300]}"


class Machine:
    """One Emu with the old stub, the new entry and the helper hooks installed for one site."""

    def __init__(self, spec: Spec):
        self.spec = spec
        self.emu = emu = Emu(False)
        self.va = site_va(spec.sid)
        try:
            self.target = site_target(spec.sid)
        except AssertionError:
            self.target = None
        self.resume = self.va + spec.length
        slots = {}
        for i, sym in enumerate(sorted(self._slot_symbols())):
            slots[sym] = SLOT_AT + 8 * i
        self.slots = slots
        helpers_at = {}
        for i, name in enumerate(sorted(self._frozen_helpers())):
            helpers_at[name] = HELPER_AT + 0x100 * i
            place_frozen(emu, name, helpers_at[name], helpers_at, slots)
        self.old = place_frozen(emu, "Rs7_Stub_" + spec.sid, OLD_AT, helpers_at, slots)
        self.new = build_new(emu, spec.sid, self.va, spec.length)
        patch = spec.patch(self) if callable(spec.patch) else spec.patch
        for address, data in patch.items():
            emu.read(address, len(data))
            emu.uc.mem_write(address, data)
        self.calls: list = []
        self.cur_case: Case | None = None
        for name, helper in spec.helpers.items():
            address = emu.exports[name]
            emu.uc.mem_write(address, b"\xc3")
            emu.uc.hook_add(UC_HOOK_CODE, self._capture(name, helper), begin=address, end=address)
        self.writes: set = set()
        emu.uc.hook_add(UC_HOOK_MEM_WRITE, self._track)
        self.exit_addresses = [self.resume if e == "resume" else self.target for e in spec.exits]
        assert all(self.exit_addresses), spec
        for address in self.exit_addresses:
            emu.uc.hook_add(UC_HOOK_CODE, self._stop_at_exit, begin=address, end=address)

    def _slot_symbols(self):
        out = set()
        for name in [("Rs7_Stub_" + self.spec.sid), *self._frozen_helpers()]:
            out |= {
                f["symbol"]
                for f in FROZEN[name]["fixups"]
                if f["symbol"].startswith(("g_rs7_resume_", "g_rs7_target_")) or f["symbol"] in RETIRED_WORDS
            }
        return out

    def _frozen_helpers(self):
        return {f["symbol"][1:] for f in FROZEN["Rs7_Stub_" + self.spec.sid]["fixups"] if f["symbol"].startswith("@")}

    # --- hooks -------------------------------------------------------------------------------
    def _capture(self, name, helper):
        def hook(uc, address, size, user):
            sp = uc.reg_read(UC_X86_REG_ESP)
            args = (
                struct.unpack(f"<{helper.nargs}I", bytes(uc.mem_read(sp + 4, 4 * helper.nargs))) if helper.nargs else ()
            )
            derefs = tuple(bytes(uc.mem_read(args[i], n)) for i, n in helper.deref)
            self.calls.append((name, args, derefs))
            ret = self.cur_case.rets.get(name, helper.ret) if self.cur_case else helper.ret
            # Dirty every caller-saved register, the flags and SSE: the thunk / stub must cope.
            uc.reg_write(UC_X86_REG_EAX, ret)
            uc.reg_write(UC_X86_REG_ECX, 0xBAD1)
            uc.reg_write(UC_X86_REG_EDX, 0xBAD2)
            uc.reg_write(UC_X86_REG_EFLAGS, 0x2D7)
            uc.reg_write(UC_X86_REG_XMM0, 0xBAD)
            uc.reg_write(UC_X86_REG_XMM7, 0xBAD)
            uc.mem_write(DATA + 0x500 + 4 * (len(self.calls) % 8), struct.pack("<I", len(self.calls)))

        return hook

    def _track(self, uc, access, address, size, value, user):
        if not STACK <= address < STACK + 0x100000:
            self.writes.update(range(address, address + size))

    @staticmethod
    def _stop_at_exit(uc, address, size, user):
        uc.emu_stop()

    # --- one run -----------------------------------------------------------------------------
    def run(self, which: str, case: Case, flags: int, misalign: int):
        emu, uc, spec = self.emu, self.emu.uc, self.spec
        self.cur_case = case
        self.calls.clear()
        self.writes.clear()
        esp = ESP + misalign
        uc.mem_write(STACK + 0x7F000, bytes(0x3000))
        uc.mem_write(ebp_region := EBP - 0x100, bytes((i * 29 + 7) & 255 for i in range(0x200)))
        uc.mem_write(esp, bytes((i * 17 + 3) & 255 for i in range(LIVE_ABOVE)))
        uc.mem_write(DATA, bytes(0x1000))
        for address, data in {**spec.setup_mem, **case.mem}.items():
            address = resolve(emu, address)
            emu.read(address, len(data))
            uc.mem_write(address, data)
        for offset, value in case.stack.items():
            uc.mem_write(esp + offset, struct.pack("<I", value & 0xFFFFFFFF))
        for offset, value in case.ebp.items():
            uc.mem_write(EBP + offset, struct.pack("<I", value & 0xFFFFFFFF))
        for name, value in {**spec.words, **case.words}.items():
            uc.mem_write(emu.exports[name], struct.pack("<I", value))
        for sym, slot in self.slots.items():
            value = (
                RETIRED_WORDS[sym]
                if sym in RETIRED_WORDS
                else self.resume
                if sym.startswith("g_rs7_resume_")
                else self.target
            )
            uc.mem_write(slot, struct.pack("<I", value or 0))
        for sym in (f"g_rs7_resume_{spec.sid}", f"g_rs7_target_{spec.sid}"):
            if sym in emu.exports:
                value = self.resume if "resume" in sym else self.target
                uc.mem_write(emu.exports[sym], struct.pack("<I", value or 0))
        # x87 with two live slots, SSE with marker values
        uc.mem_write(CODE + 0x6000, bytes.fromhex("DB E3 D9 E8 D9 EB"))  # fninit; fld1; fldpi
        uc.emu_start(CODE + 0x6000, CODE + 0x6006)
        uc.reg_write(UC_X86_REG_XMM0, DIRTY_XMM[0])
        uc.reg_write(UC_X86_REG_XMM7, DIRTY_XMM[1])
        regs = {**DEFAULT_REGS, **case.regs, UC_X86_REG_ESP: esp, UC_X86_REG_EFLAGS: flags}
        for reg, value in regs.items():
            uc.reg_write(reg, value)
        start = self.old if which == "old" else self.new
        uc.emu_start(start, 0xFFFFFFF0, count=20000)
        eip = uc.reg_read(UC_X86_REG_EIP)
        assert eip in self.exit_addresses, f"{which} stopped at {eip:#x}"
        final_esp = uc.reg_read(UC_X86_REG_ESP)
        compare = tuple((resolve(emu, a), n) for a, n in (*spec.extra_cmp, *((a, len(d)) for a, d in case.mem.items())))
        return dict(
            regs=emu.regs(*ALL_REGS),
            flags=uc.reg_read(UC_X86_REG_EFLAGS),
            eip=eip,
            fpu=(
                uc.reg_read(UC_X86_REG_ST0),
                uc.reg_read(UC_X86_REG_ST1),
                uc.reg_read(UC_X86_REG_FPSW),
                uc.reg_read(UC_X86_REG_FPCW),
                uc.reg_read(UC_X86_REG_FPTAG),
            ),
            xmm=(uc.reg_read(UC_X86_REG_XMM0), uc.reg_read(UC_X86_REG_XMM7)),
            # Bytes between the pushes and the final ESP are uninitialised locals (`sub esp,N`) and the
            # dead scratch below the live stack: neither the stock code nor the hook ABI defines them.
            stack=emu.read(esp - spec.pushed, LIVE_ABOVE + spec.pushed),
            esp=esp,
            final_esp=final_esp,
            data=emu.read(DATA, 0x1000),
            ebp_area=emu.read(ebp_region, 0x200),
            writes=tuple((a, emu.read(a, 1)) for a in sorted(self.writes)),
            mem=tuple(emu.read(a, n) for a, n in compare),
            words=tuple(emu.read(emu.exports[name], 4) for name in sorted({**spec.words, **case.words})),
            calls=tuple(self.calls),
        )

    def compare(self, case: Case, flags: int, misalign: int):
        """Run old and new; assert equivalence per the spec's flag / XMM contract. Returns new's state."""
        old = self.run("old", case, flags, misalign)
        new = self.run("new", case, flags, misalign)
        spec = self.spec
        for key in ("regs", "eip", "fpu", "stack", "final_esp", "data", "ebp_area", "writes", "mem", "words", "calls"):
            assert old[key] == new[key], f"{key}: {brief(old[key], new[key])}"
        # Arithmetic flags (CF PF AF ZF SF OF): "exact" = the old stub's result (its replayed or compared
        # instruction writes them); "dead" = the old stub leaves whatever the helper call left and no
        # continuation reads them, so the new hook must simply leave the incoming ones. Every other flag
        # bit (DF, IF, ...) is always the incoming one in the new hook; the old stubs let the helper
        # call clear DF.
        if spec.flags == "exact":
            assert old["flags"] & ARITH == new["flags"] & ARITH, (hex(old["flags"]), hex(new["flags"]))
        else:
            assert new["flags"] & ARITH == flags & ARITH, (hex(new["flags"]), hex(flags))
        assert new["flags"] & ~ARITH == flags & ~ARITH, (hex(new["flags"]), hex(flags))
        if spec.xmm == "exact":
            assert old["xmm"] == new["xmm"]
        assert new["xmm"] == DIRTY_XMM, "the mid-hook thunk must restore SSE"
        return new
