"""Old-vs-new differential oracle for sites converted from naked stubs to C++ mid-hook callbacks.

The frozen old stubs (tools/tests/fixtures/midhook_cold_951f935.json, midhook_0cf5874.json, midhook_24bfba8.json,
midhook_b8c0ed7.json, midhook_6d1a62c.json, midhook_44135a2.json, midhook_b6071a8.json, midhook_318618f.json, midhook_9c0e44f.json, midhook_b72bc52.json, midhook_c15ca8d.json, midhook_72da588.json, midhook_fdfddcf.json and midhook_7f80b7b.json, produced by tools/freeze_stubs.py from the DLLs
built at those commits) and the production generated entry + callback run on identical machine state in Unicorn. Compared after the run: all eight GPRs, EFLAGS, the final
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

from emu_harness import (
    ALL_REGS,
    CALL_AFTER,
    CALL_WRAP,
    CODE,
    DATA,
    JMP_BUILDERS,
    LIGHT_CALLBACKS,
    MID_CALLBACKS,
    STACK,
    TEXT_VA,
    Emu,
    game_text,
    site_target,
    site_va,
    mid_mode,
    wrap_builder_args,
)

FIXTURES = (
    "midhook_7f80b7b.json",
    "midhook_cold_951f935.json",
    "midhook_0cf5874.json",
    "midhook_24bfba8.json",
    "midhook_b8c0ed7.json",
    "midhook_6d1a62c.json",
    "midhook_44135a2.json",
    "midhook_b6071a8.json",
    "midhook_318618f.json",
    "midhook_9c0e44f.json",
    "midhook_b72bc52.json",
    "midhook_c15ca8d.json",
    "midhook_72da588.json",
    "midhook_fdfddcf.json",
    "midhook_304b9d3.json",  # the call-in helpers (callin_oracle.py)
    "midhook_06733ba.json",  # Rs7_Call_GlowRead (callin_oracle.py)
)
FROZEN = {
    name: blob
    for fixture in FIXTURES
    for name, blob in json.loads((Path(__file__).with_name("fixtures") / fixture).read_text()).items()
}

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
# g_rs7_wrong_stock: the old W stub's copy of the game's 34.0f (W now reads the game's constant, ref site X_WTHR).
RETIRED_WORDS = {"g_rs7_tuner_on": 1, "g_rs7_wrong_stock": 0x42080000}  # [tuner] seven_slot: always on
ESP, EBP = STACK + 0x80000, STACK + 0x81000
LIVE_ABOVE = 640  # bytes above the original ESP that are compared
ARITH = 0x8D5  # CF PF AF ZF SF OF
FSW_EXCEPTIONS = 0x3F  # x87 IE DE ZE OE UE PE (sticky)
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
    deref: tuple = ()  # (argument index, byte count[, offset]): memory the argument points at (+ offset)
    ret: int = 0  # EAX the call returns (caller-saved registers are dirtied)
    # Arguments compared by what they point at (deref) only: a pointer to a frame the stub and the callback build at
    # different stack addresses.
    opaque: tuple = ()

    def __post_init__(self):
        assert set(self.opaque) <= {d[0] for d in self.deref}, "an opaque pointer argument needs a deref"


@dataclass
class Spec:
    sid: str
    length: int  # displaced bytes
    helpers: dict = field(default_factory=dict)  # export name -> Helper
    # exact: old == new; dead: old arbitrary, new == incoming; free: old and new arbitrary (the old stub left its own
    # leftovers, the hook the replay's or the incoming flags). dead and free are only valid where no exit reads them.
    flags: str = "exact"
    xmm: str = "restored"  # exact: old == new (old saves SSE); restored: new == incoming
    # x87 status word: exact: old == new; sticky: the exception flags (FSW bits 0-5) are excluded, the hook leaves the
    # incoming ones (its float math runs on SSE / the thunk's FNINIT state; the old stub's x87 arithmetic raised them).
    fsw: str = "exact"
    # where the run ends: resume / target / ("resume", offset): a join after the site / Sym: a DLL function entry the hook
    # continues in (a C wrapper with the game function's signature)
    exits: tuple = ("resume",)
    pushed: int = 0  # bytes the stolen instructions push (live below the site ESP, compared)
    # Bytes the stolen instructions reserve below the pushes (`sub esp,N`; not compared) and the alignment of an `and
    # esp,-N`. An exit that does not pop must end at exactly (esp - pushed - locals) rounded down to `align`.
    locals: int = 0
    align: int = 1
    patch: object = field(default_factory=dict)  # address -> bytes, or callable(machine) -> that dict
    words: dict = field(default_factory=dict)  # exported variable -> dword reset before every run
    setup_mem: dict = field(default_factory=dict)
    extra_cmp: tuple = ()  # (address | Sym, size): memory ranges compared in addition
    # A call site (Rs7Mid_BuildCall): both runs start at the callee's entry, so a case puts the return address (site + 5)
    # at stack offset 0; length is 5 and "target" (the callee) is the Before placement's exit.
    call: bool = False
    # A call site whose old stub was a jmp over `lead` bytes of game code before the call too (GSA1: `push 40h; call`):
    # the old stub starts at site - lead, the new and the stock runs execute the original bytes from there (the new run's
    # call retargeted to its entry). Cases describe the state at site - lead.
    lead: int = 0
    # caller-saved GPRs ("ecx", "edx") dead at every exit (a return to the caller): not compared old vs new, where the old
    # stub left a C helper's leftovers in them and the hook the incoming values
    clobbers: tuple = ()
    # exports the new hook calls but the old stub did not (a log-once diagnostic): replaced by `ret`, not recorded
    quiet: tuple = ()
    # the frozen old stub, when it is not Rs7_Stub_<sid> (one stub served several sites: Rs7_Publish_ZeroRaw)
    stub: str = ""

    @property
    def old_stub(self) -> str:
        return self.stub or "Rs7_Stub_" + self.sid


@dataclass
class Case:
    id: str
    regs: dict = field(default_factory=dict)
    mem: dict = field(default_factory=dict)  # address -> bytes, written before every run
    stack: dict = field(default_factory=dict)  # offset from the site's ESP -> dword (misalignment-proof)
    ebp: dict = field(default_factory=dict)  # offset from EBP -> dword
    words: dict = field(default_factory=dict)  # exported variable -> dword
    rets: dict = field(default_factory=dict)  # helper name -> EAX (overrides Helper.ret)
    stores: dict = field(
        default_factory=dict
    )  # helper name -> ((argument index, dword), ...) the helper writes through
    x87: bytes = b""  # extra x87 setup after the default fld1; fldpi (e.g. more loads for a full stack)


_FLAGS = {"AF": 1 << 0, "CF": 1 << 1, "SF": 1 << 2, "ZF": 1 << 3, "PF": 1 << 4, "OF": 1 << 5}


def _flag_set(insn, prefixes) -> set:
    return {flag for flag in _FLAGS for prefix in prefixes if insn.eflags & getattr(X86, f"X86_EFLAGS_{prefix}_{flag}")}


def flags_dead_at(address: int, budget: int = 40) -> bool:
    """Static evidence that no arithmetic flag is read at `address` before it is rewritten: linear and
    branch-following scan of the game's code. A call or return ends the scan as dead (flags are not part
    of the calling convention). False = not proven within the budget."""
    text = game_text()
    if not TEXT_VA <= address < TEXT_VA + len(text):
        return True  # a DLL function entry (a Sym exit): flags are not part of the C calling convention
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    todo, seen = [(address, frozenset())], set()
    while todo:
        pc, defined = todo.pop()
        while True:
            if budget <= 0:
                return False
            if (pc, defined) in seen:
                break  # this path was examined already; the other pending branches still are
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
        if sym.startswith("@") or (sym in helpers_at and sym not in emu.exports):  # a frozen (or retired) function
            target = helpers_at[sym.lstrip("@")] + fx["addend"]
        elif sym.startswith(("g_rs7_resume_", "g_rs7_target_")) or sym in RETIRED_WORDS:
            target = slots[sym] + fx["addend"]
        else:
            target = emu.exports[sym] + fx["addend"]
        value = target - (at + fx["offset"] + 4) if fx["relative"] else target
        struct.pack_into("<I", raw, fx["offset"], value & 0xFFFFFFFF)
    emu.uc.mem_write(at, bytes(raw))
    return at


def build_new(emu: Emu, sid: str, va: int, n: int, callee: int | None = None) -> int:
    callback = emu.exports[MID_CALLBACKS.get(sid, "Rs7_Mid_" + sid)]
    if sid in CALL_WRAP:
        emu.uc.mem_write(SOURCE_AT, game_text()[va - TEXT_VA : va - TEXT_VA + n])
        args = [
            CODE + 0xE000,
            *wrap_builder_args(emu.exports, sid, va, callee, SOURCE_AT, NEW_AT, mid_mode(sid)),
        ]
        builder = "Rs7Mid_BuildWrap"
    elif callee is not None:  # call site
        args = [CODE + 0xE000, NEW_AT, 256, va, callee, NEW_AT, callback, mid_mode(sid), sid in CALL_AFTER]
        builder = "Rs7Mid_BuildCall"
    else:
        emu.uc.mem_write(SOURCE_AT, game_text()[va - TEXT_VA : va - TEXT_VA + n])
        args = [CODE + 0xE000, NEW_AT, 256, SOURCE_AT, va, n, NEW_AT, callback]
        builder = JMP_BUILDERS[mid_mode(sid)]
    emu.uc.mem_write(STACK + 0x70000, struct.pack(f"<{len(args)}I", *map(int, args)))
    emu.run(emu.exports[builder], CODE + 0xE000, {UC_X86_REG_ESP: STACK + 0x70000})
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

    def __init__(self, spec: Spec, real_helpers: bool = False):
        """real_helpers: the hook's C helpers run for real on the DLL's default (six-string) state instead of `ret`
        (the stock-reference runs, stock_differences)."""
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
        # a site with no frozen stub here (the SE pilot, test_midhook_pilot.py) only has stock and new runs
        stub = spec.old_stub
        self.old = place_frozen(emu, stub, OLD_AT, helpers_at, slots) if stub in FROZEN else None
        self.new = build_new(emu, spec.sid, self.va, spec.length, self.target if spec.call or spec.lead else None)
        patch = spec.patch(self) if callable(spec.patch) else spec.patch
        for address, data in patch.items():
            emu.read(address, len(data))
            emu.uc.mem_write(address, data)
        self.calls: list = []
        self.cur_case: Case | None = None
        for name in spec.quiet:
            emu.uc.mem_write(emu.exports[name], b"\xc3")
        for name, helper in () if real_helpers else spec.helpers.items():
            address = emu.exports[name]
            emu.uc.mem_write(address, b"\xc3")
            emu.uc.hook_add(UC_HOOK_CODE, self._capture(name, helper), begin=address, end=address)
        self.writes: set = set()
        self.pristine: dict = {}  # address -> byte before the first run wrote it (restored before every run)
        emu.uc.hook_add(UC_HOOK_MEM_WRITE, self._track)
        self.exit_addresses = [self._exit(e) for e in spec.exits]
        self.entry_exits = {self._exit(e) for e in spec.exits if isinstance(e, Sym)}
        assert all(self.exit_addresses), spec
        for address in self.exit_addresses:
            emu.uc.hook_add(UC_HOOK_CODE, self._stop_at_exit, begin=address, end=address)
        # Real helpers keep state in the DLL (registries, latches): every run starts from the state after setup.
        self.dll_state = None
        if real_helpers:
            from emu_common import dll_image

            base, size = dll_image()[:2]
            self.dll_state = (base, emu.read(base, size))

    def stock_start(self) -> int:
        """Where the unhooked game runs from: the site with its original bytes, or a call site's callee (the game's own
        `call` already pushed the return address the case puts at stack offset 0)."""
        if self.spec.call:
            return self.target
        self.emu.read(self.va, self.spec.length)  # map the page
        self.emu.uc.mem_write(self.va, game_text()[self.va - TEXT_VA : self.va - TEXT_VA + self.spec.length])
        return self.va

    def lead_start(self, hooked: bool) -> int:
        """A `lead` site: the original bytes from site - lead through the call, which goes to the new entry (hooked) or
        to the original callee."""
        at = self.va - self.spec.lead
        code = bytearray(game_text()[at - TEXT_VA : self.va + 5 - TEXT_VA])
        if hooked:
            struct.pack_into("<i", code, self.spec.lead + 1, self.new - (self.va + 5))
        self.emu.read(at, len(code))
        self.emu.uc.mem_write(at, bytes(code))
        return at

    def _exit(self, name):
        if isinstance(name, int):  # an absolute stop address (RET_AT)
            return name
        if isinstance(name, Sym):
            return resolve(self.emu, name)
        if isinstance(name, tuple):
            return self.resume + name[1]
        return self.resume if name == "resume" else self.target

    def _slot_symbols(self):
        out = set()
        for name in [n for n in (self.spec.old_stub,) if n in FROZEN] + sorted(self._frozen_helpers()):
            out |= {
                f["symbol"]
                for f in FROZEN[name]["fixups"]
                if f["symbol"].startswith(("g_rs7_resume_", "g_rs7_target_")) or f["symbol"] in RETIRED_WORDS
            }
        return out

    def _frozen_helpers(self):
        """The frozen functions the old stub calls: its "@NAME" fixups, and functions the DLL no longer exports whose
        frozen copy exists (Rs7_Pitch_Raw8, called by stubs frozen before it was retired)."""
        blob = FROZEN.get(self.spec.old_stub, {"fixups": ()})
        out = set()
        for f in blob["fixups"]:
            sym = f["symbol"]
            if sym.startswith("@"):
                out.add(sym[1:])
            elif sym in FROZEN and sym not in self.emu.exports:
                out.add(sym)
        return out

    # --- hooks -------------------------------------------------------------------------------
    def _capture(self, name, helper):
        def hook(uc, address, size, user):
            sp = uc.reg_read(UC_X86_REG_ESP)
            args = (
                struct.unpack(f"<{helper.nargs}I", bytes(uc.mem_read(sp + 4, 4 * helper.nargs))) if helper.nargs else ()
            )
            derefs = tuple(bytes(uc.mem_read(args[d[0]] + (d[2] if len(d) > 2 else 0), d[1])) for d in helper.deref)
            shown = tuple(None if i in helper.opaque else a for i, a in enumerate(args))
            self.calls.append((name, shown, derefs))
            for index, value in self.cur_case.stores.get(name, ()) if self.cur_case else ():
                uc.mem_write(args[index], struct.pack("<I", value))
            ret = self.cur_case.rets.get(name, helper.ret) if self.cur_case else helper.ret
            # Dirty every caller-saved register, the flags and SSE: the thunk / stub must cope.
            uc.reg_write(UC_X86_REG_EAX, ret)
            uc.reg_write(UC_X86_REG_ECX, 0xBAD1)
            uc.reg_write(UC_X86_REG_EDX, 0xBAD2)
            uc.reg_write(UC_X86_REG_EFLAGS, 0x2D7)
            if self.spec.sid not in LIGHT_CALLBACKS:  # a light site's helpers are proven free of SSE code
                uc.reg_write(UC_X86_REG_XMM0, 0xBAD)
                uc.reg_write(UC_X86_REG_XMM7, 0xBAD)
            uc.mem_write(DATA + 0x500 + 4 * (len(self.calls) % 8), struct.pack("<I", len(self.calls)))

        return hook

    def _track(self, uc, access, address, size, value, user):
        if not STACK <= address < STACK + 0x100000:
            self.writes.update(range(address, address + size))
            for a, old in zip(range(address, address + size), bytes(uc.mem_read(address, size))):
                self.pristine.setdefault(a, old)  # the hook fires before the write

    @staticmethod
    def _stop_at_exit(uc, address, size, user):
        uc.emu_stop()

    # --- one run -----------------------------------------------------------------------------
    def run(self, which: str, case: Case, flags: int, misalign: int):
        emu, uc, spec = self.emu, self.emu.uc, self.spec
        self.cur_case = case
        if self.dll_state:
            uc.mem_write(*self.dll_state)
        for address, old in self.pristine.items():  # no game memory a previous run wrote carries over
            uc.mem_write(address, bytes([old]))
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
        preload = bytes.fromhex("DB E3 D9 E8 D9 EB") + case.x87  # fninit; fld1; fldpi; the case's extra
        uc.mem_write(CODE + 0x6000, preload)
        uc.emu_start(CODE + 0x6000, CODE + 0x6000 + len(preload))
        fsw_in = uc.reg_read(UC_X86_REG_FPSW)
        uc.reg_write(UC_X86_REG_XMM0, DIRTY_XMM[0])
        uc.reg_write(UC_X86_REG_XMM7, DIRTY_XMM[1])
        regs = {**DEFAULT_REGS, **case.regs, UC_X86_REG_ESP: esp, UC_X86_REG_EFLAGS: flags}
        for reg, value in regs.items():
            uc.reg_write(reg, value)
        if spec.lead:
            start = self.old if which == "old" else self.lead_start(which == "new")
        else:
            start = {"old": self.old, "new": self.new, "stock": self.stock_start()}[which]
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
                uc.reg_read(UC_X86_REG_FPSW) & (~FSW_EXCEPTIONS if spec.fsw == "sticky" else 0xFFFF),
                uc.reg_read(UC_X86_REG_FPCW),
                uc.reg_read(UC_X86_REG_FPTAG),
            ),
            xmm=(uc.reg_read(UC_X86_REG_XMM0), uc.reg_read(UC_X86_REG_XMM7)),
            # Bytes between the pushes and the final ESP are uninitialised locals (`sub esp,N`) and the
            # dead scratch below the live stack: neither the stock code nor the hook ABI defines them. At a call
            # site whose callee is skipped (Rs7Mid::Return), the popped return address and arguments are dead too; so
            # is everything below the final ESP of an exit that pops (a `ret N`, an epilogue).
            stack=emu.read(
                live := max(esp if spec.call else esp - spec.pushed, final_esp), max(0, esp + LIVE_ABOVE - live)
            ),
            esp=esp,
            final_esp=final_esp,
            data=emu.read(DATA, 0x1000),
            ebp_area=emu.read(ebp_region, 0x200),
            writes=tuple((a, emu.read(a, 1)) for a in sorted(self.writes)),
            mem=tuple(emu.read(a, n) for a, n in compare),
            words=tuple(emu.read(emu.exports[name], 4) for name in sorted({**spec.words, **case.words})),
            calls=tuple(self.calls),
            exceptions=(uc.reg_read(UC_X86_REG_FPSW) & FSW_EXCEPTIONS, fsw_in & FSW_EXCEPTIONS),
        )

    def compare(self, case: Case, flags: int, misalign: int):
        """Run old and new; assert equivalence per the spec's flag / XMM contract. Returns new's state."""
        old = self.run("old", case, flags, misalign)
        new = self.run("new", case, flags, misalign)
        spec = self.spec
        if spec.clobbers:
            dead = {
                i
                for i, name in enumerate(("eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp"))
                if name in spec.clobbers
            }
            for state in (old, new):
                state["regs"] = tuple(0 if i in dead else v for i, v in enumerate(state["regs"]))
        for key in ("regs", "eip", "fpu", "stack", "final_esp", "data", "ebp_area", "writes", "mem", "words", "calls"):
            assert old[key] == new[key], f"{key}: {brief(old[key], new[key])}"
        if old["final_esp"] <= old["esp"] and not spec.call:  # no pop: the declared pushes and locals are all there is
            want = (old["esp"] - spec.pushed - spec.locals) & ~(spec.align - 1)
            if old["eip"] in self.entry_exits:  # continues at a DLL function entry, before the replay: the entry stack
                want = old["esp"]
            assert old["final_esp"] == want, f"stack: final ESP {old['final_esp']:#x}, declared {want:#x}"
        # Arithmetic flags (CF PF AF ZF SF OF): "exact" = the old stub's result (its replayed or compared
        # instruction writes them); "dead" = the old stub leaves whatever the helper call left and no
        # continuation reads them, so the new hook must simply leave the incoming ones. Every other flag
        # bit (DF, IF, ...) is always the incoming one in the new hook; the old stubs let the helper
        # call clear DF.
        if spec.flags == "exact":
            assert old["flags"] & ARITH == new["flags"] & ARITH, (hex(old["flags"]), hex(new["flags"]))
        elif spec.flags == "dead":
            assert new["flags"] & ARITH == flags & ARITH, (hex(new["flags"]), hex(flags))
        assert new["flags"] & ~ARITH == flags & ~ARITH, (hex(new["flags"]), hex(flags))
        if spec.fsw == "sticky":
            assert new["exceptions"][0] == new["exceptions"][1], "the hook must leave the incoming x87 exception flags"
        if spec.xmm == "exact":
            assert old["xmm"] == new["xmm"]
        assert new["xmm"] == DIRTY_XMM, "the mid-hook thunk must restore SSE"
        return new


def oracle_tests(specs: dict, always_calls: set):
    """The three tests of a site table {sid: (Spec, [Case])}; a test module assigns them to module-level names."""
    import itertools
    from functools import lru_cache

    import pytest

    @lru_cache(maxsize=None)
    def machine(sid: str) -> Machine:
        return Machine(specs[sid][0])

    def cases():
        for sid, (_, cs) in specs.items():
            for case, flags, misalign in itertools.product(cs, FLAG_PATTERNS, MISALIGN):
                yield pytest.param(sid, case, flags, misalign, id=f"{sid}-{case.id}-f{flags:x}-a{misalign}")

    @pytest.mark.parametrize("sid,case,flags,misalign", list(cases()))
    def test_mid_hook_matches_frozen_stub(sid, case, flags, misalign):
        machine(sid).compare(case, flags, misalign)

    @pytest.mark.parametrize("sid", [sid for sid, (spec, _) in specs.items() if spec.flags in ("dead", "free")])
    def test_dead_flags_are_rewritten_before_any_read(sid):
        """The flag contracts "dead" (the hook leaves the incoming arithmetic flags, the old stub left helper
        leftovers) and "free" are only valid where no continuation reads them: scan the game code after each exit."""
        m = machine(sid)
        for exit_address in m.exit_addresses:
            assert flags_dead_at(exit_address), f"{sid}: flags may be live at {exit_address:#x}"

    def test_every_registered_site_calls_its_helper_at_least_once():
        """The matrix is not vacuous: for every site some case reaches each helper (and some case skips it), and
        every exit is taken."""
        for sid, (spec, cs) in specs.items():
            reached = {name: False for name in spec.helpers}
            skipped = {name: False for name in spec.helpers}
            exits = set()
            for case in cs:
                state = machine(sid).compare(case, FLAG_PATTERNS[0], 0)
                exits.add(state["eip"])
                calls = {c[0] for c in state["calls"]}
                for name in spec.helpers:
                    (reached if name in calls else skipped)[name] = True
            assert exits == set(machine(sid).exit_addresses), (sid, [hex(e) for e in exits])
            assert all(reached.values()), (sid, reached)
            assert all(skipped.values()) or sid in always_calls, (sid, skipped)

    return (
        test_mid_hook_matches_frozen_stub,
        test_dead_flags_are_rewritten_before_any_read,
        test_every_registered_site_calls_its_helper_at_least_once,
    )
