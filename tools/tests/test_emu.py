"""Run the rs7string stubs in Unicorn on the game snapshot and compare them with stock code.

Two emulators share the same setup; only the patched one has the code sites (+5 data/code
references) rewritten exactly as the DLL does (jmp/rel32/call into the DLL's stubs, resume/target
variables set, ref variables bound to the dword at their site). S1 and S4
call C code that uses the side store and are covered by the C++ unit tests (test_mode.cpp)
instead; S3's C function is pure and runs in the emulator. The diagnostic
logger calls (S2, and D1-D5 in R2, R1, D2, D3, D5) are neutralised (see VOID_C_CALLS); the C
loggers themselves are covered by test_diag.cpp.

On Windows, pytest's faulthandler prints "Windows fatal exception: access violation" during the
first Uc.mem_map: a first-chance exception Unicorn 2.1.4 raises and handles internally (it shows
up in a bare script too). It is not a test failure.
"""

from __future__ import annotations

import struct
import sys
from contextlib import contextmanager
from functools import lru_cache
from pathlib import Path

import capstone
import pefile
import pytest
from unicorn import (
    UC_ARCH_X86,
    UC_HOOK_CODE,
    UC_HOOK_MEM_FETCH_UNMAPPED,
    UC_HOOK_MEM_READ_UNMAPPED,
    UC_HOOK_MEM_WRITE_UNMAPPED,
    UC_MODE_32,
    UC_PROT_ALL,
    Uc,
)
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
    UC_X86_REG_FPSW,
    UC_X86_REG_FS,
    UC_X86_REG_GDTR,
    UC_X86_REG_SS,
    UC_X86_REG_DS,
    UC_X86_REG_ES,
)

import sitecheck
import sitetable

ROOT = Path(__file__).resolve().parents[3]
REPO = Path(__file__).resolve().parents[2]  # this checkout (a worktree too), not ROOT/rs7string
DLL = REPO / "bin" / "Release" / "dinput8.dll"
SITES = REPO / "sites.json"
SNAPSHOT = ROOT / "rskeys" / "snapshot"
sys.path.insert(0, str(ROOT / "scripts"))

pytestmark = pytest.mark.skipif(
    not DLL.exists() or not (SNAPSHOT / "memory.bin").exists(), reason="needs the built DLL and the memory snapshot"
)

STACK = 0x5D000000  # free in the snapshot (checked when the base address was chosen)
DATA = 0x5D100000
CODE = 0x5D200000  # helper code: fstp dword [DATA_OUT]
DATA_OUT = 0x5D1F0000
RET_MAGIC = 0x5D2F0000
TEB = 0x5D300000  # fake TEB page (fs base), see Emu.__init__
FLAGS_MASK = 0x8D5  # CF PF AF ZF SF OF

# Stubs whose C calls are diagnostics only (S2 -> Rs7_NoteString6; R2 -> D1, D2, D3,
# D5 -> Rs7_Diag*: void cdecl loggers). The loggers need Win32 imports the emulator does not
# provide, so their entries are replaced with `ret`; the stubs' own pushad / pushfd / push / call /
# stack restore / popfd / popad still run.
VOID_C_CALLS = ("S2", "R2", "D2", "D3", "D5")
MID_CALLBACKS = {
    "D2": "Rs7_Mid_D2",
    "D3": "Rs7_Mid_D3",
    "SE1": "Rs7_Mid_SE1",
    "SE2": "Rs7_Mid_SE2",
    "SE3": "Rs7_Mid_SE3",
    # cold sites converted from naked stubs (src/hooks/mid_cold.cpp, test_midhook_cold.py)
    "TK": "Rs7_Mid_TK",
    "TS": "Rs7_Mid_TS",
    "TUT": "Rs7_Mid_TUT",
    "XB1": "Rs7_Mid_XB1",
    "N1": "Rs7_Mid_N1",
    "N21": "Rs7_Mid_N21",
    "GSZ1": "Rs7_Mid_GSZ1",
    "GIZ1": "Rs7_Mid_GIZ1",
    "PSA2": "Rs7_Mid_PSA2",
    "PSD1": "Rs7_Mid_PSD1",
    "PSD2": "Rs7_Mid_PSD2",
    "PSU3": "Rs7_Mid_PSU3",
    "PSC1": "Rs7_Mid_PSC1",
    "PSL1": "Rs7_Mid_PSL1",
    "PSM2": "Rs7_Mid_PSM2",
    "PSR1": "Rs7_Mid_PSR1",
    "PSN1": "Rs7_Mid_PSN1",
    "GSSS": "Rs7_Mid_GSSS",
    "GSSE": "Rs7_Mid_GSSE",
}
# Ref sites (never written): the variable Rs7_Startup stores the site's dword in.
REF_VARS = {
    "X_DKEY": "g_rs7_dkey",
    "X_DNUMBER": "g_rs7_dnumber",
    "X_DSET": "g_rs7_dset",
    "X_DDROP": "g_rs7_ddrop",
    "X_ATTACH": "g_rs7_fn_attach",
    "X_CM": "g_rs7_colormgr",
    "X_TCS": "g_rs7_texlist_cs",
    "X_THEAD": "g_rs7_texlist_head",
    "X_SPAWN": "g_rs7_fn_spawn",
    "X_ADV3": "g_rs7_fn_adv3",
    "X_ALLOC": "g_rs7_fn_alloc",
    "X_STRCTOR": "g_rs7_fn_strctor",
    "X_SHAPE": "g_rs7_fn_shape",
    "X_SHOW": "g_rs7_fn_show",
    "X_DETACH": "g_rs7_fn_detach",
    "X_SETPOS": "g_rs7_fn_setpos",
    "X_SETROT": "g_rs7_fn_setrot",
    "X_DT": "g_rs7_frame_global",
    "X_WORLD": "g_rs7_fn_world",
    "X_ROOT": "g_rs7_fn_root",
    "X_SETSCALE": "g_rs7_fn_setscale",
    "X_EULER": "g_rs7_fn_euler",
    "X_PEGNAMES": "g_rs7_pegnames",
    "X_PEGSELECT": "g_rs7_fn_pegselect",
    "X_PEGPLACE": "g_rs7_fn_pegplace",
    "X_PEGLAYER": "g_rs7_fn_peglayer",
    "X_TMGR": "g_rs7_tuner_mgr",
    "X_FADEMGR": "g_rs7_fade_mgr",
    "X_FADEIN": "g_rs7_fn_fadein",
    "X_FADEOUT": "g_rs7_fn_fadeout",
    "X_FIXSTR": "g_rs7_fn_fixstr",
    "X_STRPARAMS": "g_rs7_fn_strparams",
    "X_DDSH": "g_rs7_fn_dds",
    "X_NFCTOR": "g_rs7_fn_nfctor",
    "X_NFDTOR": "g_rs7_fn_nfdtor",
    "X_GETDET": "g_rs7_fn_getdet",
    "X_VGSLOT": "g_rs7_vg_slotmap",
    "X_VGDI": "g_rs7_vg_dimgr",
    "X_SST": "g_rs7_fn_sst",
    "X_STRCON": "g_rs7_fn_strcon",
    "X_STRASG": "g_rs7_fn_strasg",
    "X_LOC": "g_rs7_fn_loc",
    "X_STRDTOR": "g_rs7_fn_strdtor",
    "X_MSORT": "g_rs7_fn_msort",
    "X_MINS1": "g_rs7_fn_mins1",
    "X_MINS2": "g_rs7_fn_mins2",
    "X_MCMP": "g_rs7_fn_mcmp",
    "X_MTAB": "g_rs7_matcher_table",
    "X_EVREL": "g_rs7_fn_evrel",
}
# Void cdecl loggers called by stubs that also call testable C code (NG9 -> Rs7_NeckRow6N runs for
# real, its once-per-song logger Rs7_NeckLog needs Win32 and is replaced with `ret`).
VOID_C_EXPORTS = ("Rs7_NeckLog", "Rs7_S7LogMask", "Rs7_HeadLog")
# Win32 clocks returning 0 (= "no clock": issue #27's N5 then skips its time-based rules).
ZERO_C_EXPORTS = ("Rs7_S7Now", "Rs7Song_Note", "Rs7Publish_GridWait")
ALL_REGS = (
    UC_X86_REG_EAX,
    UC_X86_REG_EBX,
    UC_X86_REG_ECX,
    UC_X86_REG_EDX,
    UC_X86_REG_ESI,
    UC_X86_REG_EDI,
    UC_X86_REG_EBP,
    UC_X86_REG_ESP,
)
CS = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)


UNPACKED = ROOT / "original" / "Rocksmith2014.unpacked.exe"
TEXT_VA, TEXT_SIZE = 0x401000, 0xCB503C


@lru_cache(maxsize=1)
def snapshot():
    """The 700 MB memory snapshot, loaded once for all tests."""
    from resolve_imports import Memory

    return Memory()


@lru_cache(maxsize=1)
def game_text() -> bytes:
    """Unpacked .text (file offset == RVA in Rocksmith2014.unpacked.exe)."""
    return UNPACKED.read_bytes()[TEXT_VA - 0x400000 : TEXT_VA - 0x400000 + TEXT_SIZE]


@lru_cache(maxsize=None)
def site_va(sid: str) -> int:
    _, sites = sitetable.load(SITES)
    s = next(x for x in sites if x.id == sid)
    ok, msg, va, _ = sitecheck.check_site(game_text(), TEXT_VA, s)
    assert ok, f"{sid}: {msg}"
    return va


@lru_cache(maxsize=None)
def site_target(sid: str) -> int:
    """The site's original branch target (its continuation), as Rs7_Startup binds g_rs7_target_<id>."""
    _, sites = sitetable.load(SITES)
    s = next(x for x in sites if x.id == sid)
    ok, msg, _, target = sitecheck.check_site(game_text(), TEXT_VA, s)
    assert ok and target, f"{sid}: {msg}"
    return target


class Emu:
    native_pitch = False  # test_emu_pitch.py: also write the [neck] native_pitch sites (P*)
    native_chordloops = False  # test_emu_chordloops.py: opt in to CL* only

    def __init__(self, patched: bool):
        self.mem = snapshot()
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.mapped: set[int] = set()
        for base in (STACK, DATA, CODE):
            self.uc.mem_map(base, 0x100000, UC_PROT_ALL)
        self.uc.hook_add(UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED | UC_HOOK_MEM_FETCH_UNMAPPED, self._map)
        self.uc.mem_write(CODE, bytes.fromhex("D9 1D") + struct.pack("<I", DATA_OUT))  # fstp dword [out]
        # Minimal TEB so DLL helpers with MSVC __try frames can register SEH (fs:[0] = end of chain).
        self.uc.mem_map(TEB, 0x1000, UC_PROT_ALL)
        self.uc.mem_write(TEB, struct.pack("<II", 0xFFFFFFFF, STACK + 0x100000) + struct.pack("<I", STACK))
        self.uc.mem_write(TEB + 0x18, struct.pack("<I", TEB))
        # 32-bit unicorn has no FS_BASE register: GDT entry 1 = flat-limit data segment at TEB.
        gdt = TEB + 0x800
        desc = 0xFFFF | (TEB & 0xFFFFFF) << 16 | 0x92 << 40 | 0xF << 48 | 0xC << 52 | (TEB >> 24) << 56
        flat = 0xFFFF | 0x92 << 40 | 0xF << 48 | 0xC << 52  # entry 2: flat data (SS/DS/ES)
        self.uc.mem_write(gdt + 8, struct.pack("<QQ", desc, flat))
        self.uc.reg_write(UC_X86_REG_GDTR, (0, gdt, 0x18 - 1, 0))
        self.uc.reg_write(UC_X86_REG_FS, 0x8)
        for seg in (UC_X86_REG_SS, UC_X86_REG_DS, UC_X86_REG_ES):
            self.uc.reg_write(seg, 0x10)
        self.exports = self._load_dll()
        for name in ZERO_C_EXPORTS:  # DLL-side, patched or not
            self.uc.mem_write(self.exports[name], bytes.fromhex("31 C0 C3"))  # xor eax, eax; ret
        if patched:
            self._patch()

    def _map(self, uc, access, address, size, value, user):
        import bisect

        i = bisect.bisect_right(self.mem.bases, address) - 1
        if i < 0:
            return False
        r = self.mem.regions[i]
        if not (r["base"] <= address < r["base"] + r["size"]) or r["base"] in self.mapped:
            return False
        uc.mem_map(r["base"], r["size"], UC_PROT_ALL)
        uc.mem_write(r["base"], self.mem.blob[r["offset"] : r["offset"] + r["size"]])
        self.mapped.add(r["base"])
        return True

    def _load_dll(self) -> dict[str, int]:
        pe = pefile.PE(str(DLL))
        base = pe.OPTIONAL_HEADER.ImageBase
        size = (pe.OPTIONAL_HEADER.SizeOfImage + 0xFFF) & ~0xFFF
        self.uc.mem_map(base, size, UC_PROT_ALL)
        self.uc.mem_write(base, pe.header)
        for s in pe.sections:
            self.uc.mem_write(base + s.VirtualAddress, s.get_data())
        return {e.name.decode(): base + e.address for e in pe.DIRECTORY_ENTRY_EXPORT.symbols if e.name}

    def _patch(self):
        # The legacy switch set these tests were written against: native_strings, native_fretgrid,
        # native_nextgrid, native_colours = 0 (the shipped default is 1; the native paths are
        # covered by the test_publish / test_tuner8 / test_fretgrid / test_nextgrid modules and by
        # the native sites of test_emu_pitch.py and test_emu_chordloops.py).
        _, sites = sitetable.load(SITES)
        self.mid_entries = {}
        for s in sites:
            if s.publish == "native":  # native_publish=0 (legacy; shipped 1)
                continue
            if s.id.startswith("CL") and not self.native_chordloops:
                continue
            if s.id in sitecheck.CHORDLOOPS_OFF and self.native_chordloops:
                continue
            if s.id.startswith("CM"):  # [neck] native_colours=0 (legacy; shipped 1): the colour ports stay unwritten
                continue
            if (
                s.strings == "native" or s.fretgrid == "native" or s.nextgrid == "native"
            ):  # native_strings / native_fretgrid / native_nextgrid = 0 (legacy; shipped 1): unwritten
                continue
            if s.pitch == "native" and not self.native_pitch:  # [neck] native_pitch=0 (legacy; shipped 1)
                continue
            if (
                s.tuner == "native"
            ):  # [neck] native_tuner=0 (legacy; shipped 1): TU8* / TS8* unwritten, legacy tuner sites written
                continue
            if s.id in ("LSA", "LSG", "LSC", "LSE"):  # [debug] publish_trace=0 (default): LoadStep probes unwritten
                continue
            if s.id.startswith("GK"):  # [neck] clite=0 (default, core/clite.h): GK* unwritten
                continue
            ok, msg, va, target = sitecheck.check_site(game_text(), TEXT_VA, s)
            assert ok, f"{s.id}: {msg}"
            if s.kind in sitetable.REF_KINDS:  # never written: the dword / callee goes to the variable
                self.uc.mem_write(self.exports[REF_VARS[s.id]], struct.pack("<I", target))
                continue
            self.read(va, 1)  # map the page
            if s.kind == "bytes":  # in-place byte patch, no stub
                self.uc.mem_write(va, s.write)
                continue
            if s.id in MID_CALLBACKS:
                stub = self._build_mid(s, va)
                self.mid_entries[s.id] = stub
            else:
                stub = self.exports[f"Rs7_Stub_{s.id}"]
            if s.kind in ("jmp", "jmp_atomic8"):
                rel = stub - (va + 5)
                self.uc.mem_write(va, b"\xe9" + struct.pack("<i", rel) + b"\x90" * (s.length - 5))
            elif s.kind == "rel32":
                self.uc.mem_write(va, struct.pack("<i", stub - (va + 4)))
            elif s.kind == "call":
                self.uc.mem_write(va + 1, struct.pack("<i", stub - (va + 5)))
            if f"g_rs7_resume_{s.id}" in self.exports:
                self.uc.mem_write(self.exports[f"g_rs7_resume_{s.id}"], struct.pack("<I", va + s.length))
            if f"g_rs7_target_{s.id}" in self.exports:
                self.uc.mem_write(self.exports[f"g_rs7_target_{s.id}"], struct.pack("<I", target))
        for sid in VOID_C_CALLS:
            address = self.exports[MID_CALLBACKS[sid]] if sid in MID_CALLBACKS else self.exports[f"Rs7_Stub_{sid}"]
            for callee in self._stub_calls(address, tail=sid in MID_CALLBACKS):
                self.uc.mem_write(callee, b"\xc3")  # cdecl void -> plain ret
        for name in VOID_C_EXPORTS:
            self.uc.mem_write(self.exports[name], b"\xc3")
        # All 21 code sites written = rs7string.ini [debug] diagnostics=1 (with 0 the DLL leaves D2/D3/D5
        # unwritten and R2/R1 skip their D1/D4 calls; the `diag` fixture flips it).
        self.set_diag(1)

    def _build_mid(self, site, va):
        """Run the actual DLL emitter before patching the real game site."""
        out = CODE + 0x80000 + len(self.mid_entries) * 0x100
        esp = STACK + 0x70000
        args = (RET_MAGIC, out, 256, va, va, site.length, out, self.exports[MID_CALLBACKS[site.id]])
        self.uc.mem_write(esp, struct.pack("<8I", *args))
        self.run(self.exports["Rs7Mid_Build"], RET_MAGIC, {UC_X86_REG_ESP: esp})
        assert self.uc.reg_read(UC_X86_REG_EAX) > 0, f"mid-hook rejected {site.id}"
        return out

    def _stub_calls(self, stub: int, tail=False) -> list[int]:
        """Targets of the `call rel32` instructions in a naked stub. The Release DLL is linked
        without /INCREMENTAL, so exports and calls are direct (test_exports_are_direct)."""
        out = []
        for insn in CS.disasm(self.read(stub, 0x400), stub):
            if insn.bytes[0] == 0xCC:  # int3 padding after the naked function
                break
            if insn.bytes[0] == 0xE8 or (tail and insn.bytes[0] == 0xE9):
                out.append(insn.address + 5 + struct.unpack("<i", insn.bytes[1:5])[0])
        assert out, f"no call found in the stub at {stub:#x}"
        return out

    def read(self, va: int, n: int) -> bytes:
        try:
            return bytes(self.uc.mem_read(va, n))
        except Exception:
            self._map(self.uc, 0, va, n, 0, None)
            return bytes(self.uc.mem_read(va, n))

    def run(self, start: int, until: int, regs: dict[int, int]):
        for r, v in regs.items():
            self.uc.reg_write(r, v)
        self.uc.emu_start(start, until, count=5000)
        assert self.uc.reg_read(UC_X86_REG_EIP) == until, f"stopped at {self.uc.reg_read(UC_X86_REG_EIP):#x}"

    def st0(self) -> float:
        """Pop ST0 into memory with the helper `fstp` and return it as float32."""
        eip = self.uc.reg_read(UC_X86_REG_EIP)
        self.uc.emu_start(CODE, CODE + 6)
        self.uc.reg_write(UC_X86_REG_EIP, eip)
        return struct.unpack("<f", self.read(DATA_OUT, 4))[0]

    def regs(self, *names: int) -> tuple[int, ...]:
        return tuple(self.uc.reg_read(n) for n in names)

    def fpu_top(self) -> int:
        """x87 TOP (FPSW bits 11-13): the FPU stack depth relative to the start."""
        return (self.uc.reg_read(UC_X86_REG_FPSW) >> 11) & 7

    def set_diag(self, value: int):
        self.uc.mem_write(self.exports["g_rs7_diag"], struct.pack("<I", value))

    @contextmanager
    def count_calls(self, sid: str):
        """Addresses of the C calls the stub of `sid` makes while the block runs."""
        hits: list[int] = []
        hooks = [
            self.uc.hook_add(UC_HOOK_CODE, lambda uc, addr, size, user: hits.append(addr), begin=c, end=c)
            for c in set(
                self._stub_calls(
                    self.exports[MID_CALLBACKS[sid]] if sid in MID_CALLBACKS else self.exports[f"Rs7_Stub_{sid}"],
                    tail=sid in MID_CALLBACKS,
                )
            )
        ]  # R1: 2 calls, 1 callee
        try:
            yield hits
        finally:
            for h in hooks:
                self.uc.hook_del(h)


@pytest.fixture(scope="module")
def emus():
    return Emu(patched=False), Emu(patched=True)


@pytest.fixture(params=[1, 0], ids=["diag", "nodiag"])
def diag(request, emus):
    """rs7string.ini [debug] diagnostics: 1 = R2/R1 call the D1/D4 loggers, 0 = they do not."""
    _, patched = emus
    patched.set_diag(request.param)
    yield request.param
    patched.set_diag(1)


def test_exports_are_direct(emus):
    """No incremental-link thunks: every stub export is the stub itself (no stub starts with a jmp),
    and the image has no ILT (the linker's thunks are `jmp rel32` runs at the start of .text)."""
    _, patched = emus
    stubs = {n: a for n, a in patched.exports.items() if n.startswith("Rs7_Stub_")}
    # 1.0 legacy removal: - C1-C5 N11 GSCL (7), - the B-phase tuner T1 TA TR M1 M2 L1 L2 TLC TLI TLO1 TLO2 TF PK PE TD (15),
    # - the stage-2 peg PG1-PG4 PH1 PH2 (6), - the S7 object N4 N6 N10 N14 N15 N16 N17 (7), - R7b N9 (2)
    assert (
        len([s for s in stubs if not s.startswith("Rs7_Stub_PUB")])
        == 194 + 5 + 5 + 21 + 3 - 3 - 22 - 7 - 15 - 6 - 7 - 2
    )  # TK TS TUT XB1 N1 N21 GSZ1 GIZ1 PSA2 PSD1 PSD2 PSU3 PSC1 PSL1 PSM2 PSR1 PSN1 GSSS GSSE N2 N3 N7 (cold mid-hook callbacks, mid_cold.cpp); SE1-SE3 context callbacks; + LT1 TF1 IN1 (fix/headstock2: 3+3 letters, tuner framing, inlays); + PP_TOC PP_SNGF PP_SNGD (plain PSARC); + 5 TU8 + 21 TS8 copies; CLBM CLBA CLGU CLGE CLVB; - DGR1 DGR2 (diagfix: chord-diagram movies at the FileOpener vtable) + FGRB (native FretGrid, issue #18 stage 5) + PM0 PR0 PT1 PM1 PE0 PC1-PC4 (native_pitch, issue #18 stage 5 area 3) + NG1 NGP NG9 (native next-note grid, issue #18 stage 5 area 2) + + GSFR-GSHC (16 copied-function entries), GSA1 GSC1 GSZ1 GIA1 GIZ1 GRM1-GRM3 GSSS GSSE GSCL (native GuitarStrings, issue #18 stage 4) + PSA2 PSD1 PSD2 PSU1-PSU3 PSM1 PSM2 (guarded: native PegSets only) + PSC1 PSL1 PSL2 PSR1 PSN1 PSN2 TLC TLI TLO1 TLO2 (native N-slot PegSet) - TP9 TP10 (stale capo frets) + CC3 CH3 RL1 RL2 AG1-AG3 UP1-UP3 PD2-PD5 (native chord tracking) + TP12, PL1 (7th string pluck in chords) - Q1 Q2 E1 Q3a Q3b (native detection: + MD1 LU3 AM1 WC2 EV1-EV5 BT1 CS1) - H1 H2 G1 NT1 FN1 FN3 FK SM1 (native chord rendering) + TM1-TM6, DS1 + TP1-TP11, CP1, CP2, PP1, SF1 (native N-slot chord data, issue #18) + XB1 (RS7X extension block, issue #18) + SE1, SE2, SE3 (song end log) + TJ, TU, TUT, TN (tuning names) + PH1, PH2 (7th knob tint) + TICK, JUD1, JUD2, AUD1 (virtual guitarist), M4 chords: Q1 Q2 Q3a Q3b E1 H1 H2 B1 B2 FN1-FN4 G1 SM1 NT1 FK
    for name, addr in stubs.items():
        assert patched.read(addr, 1)[0] != 0xE9, f"{name} is an ILT thunk"
    pe = pefile.PE(str(DLL), fast_load=True)
    text = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
    assert text.get_data()[:5][0] != 0xE9 or text.get_data()[5] != 0xE9, ".text starts with an ILT"


# ---- S2: plain single-note expected MIDI (0x873E90) ----------------------------------------


def call_twin(emu: Emu, string: int, fret: int, tuning: list[int]) -> int:
    vec_data = DATA + 0x100
    emu.uc.mem_write(vec_data, struct.pack(f"<{len(tuning)}h", *tuning))
    emu.uc.mem_write(DATA, struct.pack("<II", vec_data, vec_data + 2 * len(tuning)))
    esp = STACK + 0x80000
    emu.uc.mem_write(esp, struct.pack("<III", RET_MAGIC, fret, DATA))
    emu.run(
        0x873E90,
        RET_MAGIC,
        {UC_X86_REG_ESP: esp, UC_X86_REG_EAX: 1, UC_X86_REG_EDX: string, UC_X86_REG_EBP: STACK + 0x90000},
    )
    return emu.uc.reg_read(UC_X86_REG_EAX)


@pytest.mark.parametrize("string", range(6))
@pytest.mark.parametrize("tuning", [[0] * 6, [0] * 7, [-2, 0, 0, 0, 0, 0, -1]])
def test_s2_strings_0_to_5_are_stock(emus, string, tuning):
    stock, patched = emus
    assert call_twin(patched, string, 3, tuning) == call_twin(stock, string, 3, tuning)


@pytest.mark.parametrize("fret,expected", [(0, 35), (3, 38), (5, 40), (7, 42)])
def test_s2_string_6_in_a_7_entry_song_is_low_b(emus, fret, expected):
    _, patched = emus
    assert call_twin(patched, 6, fret, [0] * 7) == expected


def test_s2_string_6_uses_its_tuning_offset(emus):
    _, patched = emus
    assert call_twin(patched, 6, 3, [0, 0, 0, 0, 0, 0, -1]) == 37


# ---- S3: exit of Play_Detector_SetTuning (0x4E2590), whole function -------------------------
# Rs7_OnDetectorTuned is pure C without calls, so it runs in the emulator as built.

SET_TUNING = 0x4E2590
DET = DATA + 0x7000
DET_SIZE = 0x1360
DET_INSTRUMENT, DET_LOWEST = 0x11EC, 0x11F8


def run_set_tuning(emu: Emu, tuning: list[int], instrument: int):
    vec_data = DATA + 0x100
    emu.uc.mem_write(vec_data, struct.pack(f"<{len(tuning)}h", *tuning))
    emu.uc.mem_write(DATA, struct.pack("<II", vec_data, vec_data + 2 * len(tuning)))
    det = bytearray((i * 5 + 1) & 0xFF for i in range(DET_SIZE))
    det[DET_INSTRUMENT : DET_INSTRUMENT + 4] = struct.pack("<I", instrument)
    emu.uc.mem_write(DET, bytes(det))
    esp = STACK + 0x80000
    emu.uc.mem_write(esp, struct.pack("<I", RET_MAGIC))
    emu.run(
        SET_TUNING,
        RET_MAGIC,
        {
            UC_X86_REG_ESP: esp,
            UC_X86_REG_EAX: DET,
            UC_X86_REG_ESI: DATA,
            UC_X86_REG_EBX: 0xB1B1,
            UC_X86_REG_EDI: 0xD1D1,
            UC_X86_REG_EBP: 0x1111,
        },
    )
    return emu.read(DET, DET_SIZE), emu.regs(*ALL_REGS), emu.fpu_top()


def lowest(det: bytes) -> int:
    return struct.unpack_from("<i", det, DET_LOWEST)[0]


S3_TUNINGS = [
    [0] * 6,
    [-2, 0, 0, 0, 0, 0],
    [0] * 7,
    [0, 0, 0, 0, 0, 0, -2],
    [0, 0, 0, 0, 0, 0, 10],
    [-2] * 7,
    [0] * 8,
    [-4] * 7,
    [0, 0, 0, 0, 0, 0, -4],
    [0, 0, 0, 0, 0, 0, -5],
]


@pytest.mark.parametrize("instrument", [0, 1, 2])
@pytest.mark.parametrize("tuning", S3_TUNINGS, ids=lambda t: f"{len(t)}x{t[-1]}" if t else "empty")
def test_s3_set_tuning(emus, tuning, instrument):
    stock, patched = emus
    assert SET_TUNING < site_va("S3") < SET_TUNING + 0xC0
    want, got = run_set_tuning(stock, tuning, instrument), run_set_tuning(patched, tuning, instrument)
    assert got[1:] == want[1:]  # registers, FPU as stock
    if len(tuning) == 7 and instrument != 2:
        assert lowest(got[0]) == min(lowest(want[0]), 35 + tuning[6])
        assert got[0][:DET_LOWEST] + got[0][DET_LOWEST + 4 :] == want[0][:DET_LOWEST] + want[0][DET_LOWEST + 4 :]
    else:
        assert got[0] == want[0]


@pytest.mark.parametrize("t6,low", [(0, 35), (-2, 33), (-4, 31), (-5, 30)])
def test_s3_lowest_open_is_the_7th_string_for_extended_low_tunings(emus, t6, low):
    """Item 2: the song-play lowest-open policy (+11F8h) reaches Drop A 33, G 31, F#1 30 (guitar only)."""
    stock, patched = emus
    tuning = [0, 0, 0, 0, 0, 0, t6]
    assert lowest(run_set_tuning(patched, tuning, 1)[0]) == low
    assert lowest(run_set_tuning(patched, tuning, 0)[0]) == low
    assert lowest(run_set_tuning(patched, tuning, 2)[0]) == lowest(run_set_tuning(stock, tuning, 2)[0])  # bass stock
    assert lowest(run_set_tuning(patched, [0] * 6, 1)[0]) == lowest(run_set_tuning(stock, [0] * 6, 1)[0])


def test_s3_six_entries_after_a_seven_entry_song_are_stock(emus):
    """Final review F1: the tuner/Guitarcade re-tune with a 6-entry vector after a 7-string song."""
    stock, patched = emus
    seven = run_set_tuning(patched, [0] * 7, 1)
    assert lowest(seven[0]) == 35
    assert run_set_tuning(patched, [0] * 6, 1) == run_set_tuning(stock, [0] * 6, 1)


# ---- W: UI_PlayerTuner_CheckWrongInstrument 0x5B2DDE, `fcomp float [34.0]` (low range) -----------
# ST0 = the mean of ten detected notes, ESI = tuner session. Stock warns when mean <= 34 (C0 or C3
# set by the compare, tested at 0x5B2DE6). 7-string sessions (the exported table the C++ keeps) get
# min(34, lowest open - 1); everything else, and [tuner] lowrange=0, stays stock.

SESSION_7, SESSION_6 = 0x5D400000, 0x5D400400
SEVEN_TUNINGS = {  # name -> (lowest open MIDI of the arrangement, expected threshold)
    "B": (35, 34),
    "Drop A": (33, 32),
    "A": (33, 32),
    "G": (31, 30),
    "8str F#1": (30, 29),
    "tuned up": (40, 34),
}


def run_wrong(emu: Emu, mean: float, session: int, table: list[tuple[int, int]], lowrange: int = 1, any_: int = 1):
    w = lambda name, fmt, *v: emu.uc.mem_write(emu.exports[name], struct.pack(fmt, *v))
    w("g_rs7_lowrange", "<I", lowrange)
    w("g_rs7_tuner_any", "<I", any_)
    w("g_rs7_wrong_tab", "<8I", *([x for e in table for x in e] + [0] * (8 - 2 * len(table))))
    va = site_va("W")
    emu.uc.mem_write(DATA + 0x40, struct.pack("<f", mean))
    # helper code: fld dword [DATA+0x40]; jmp site
    j = CODE + 0x20 + 6
    emu.uc.mem_write(
        CODE + 0x20, bytes.fromhex("D9 05") + struct.pack("<I", DATA + 0x40) + b"\xe9" + struct.pack("<i", va - (j + 5))
    )
    esp = STACK + 0x80000
    emu.uc.mem_write(esp, struct.pack("<I", RET_MAGIC))
    emu.run(
        CODE + 0x20,
        va + 6,
        {
            UC_X86_REG_ESI: session,
            UC_X86_REG_ESP: esp,
            UC_X86_REG_EAX: 0xA1A1,
            UC_X86_REG_EBX: 0xB1B1,
            UC_X86_REG_ECX: 0xC1C1,
            UC_X86_REG_EDX: 0xD1D1,
            UC_X86_REG_EDI: 0xE1E1,
            UC_X86_REG_EBP: 0x1111,
            UC_X86_REG_FPSW: 0,
        },
    )
    sw = emu.uc.reg_read(UC_X86_REG_FPSW)
    w("g_rs7_tuner_any", "<I", 0)  # the emulators are shared: no tuner state afterwards
    w("g_rs7_lowrange", "<I", 1)
    warn = bool(sw & ((1 << 8) | (1 << 14)))  # C0 (mean < thr) or C3 (equal): `test ah,41h` != 0
    return warn, emu.fpu_top(), emu.regs(*ALL_REGS)


MEANS = [29.0, 30.0, 31.0, 32.0, 33.0, 33.5, 34.0, 34.5, 35.0, 36.0, 41.0]


@pytest.mark.parametrize("mean", MEANS)
@pytest.mark.parametrize("case", ["6-string/bass (no state)", "other session", "lowrange=0", "no tuner state"])
def test_w_everything_else_is_stock(emus, mean, case):
    stock, patched = emus
    table = [(SESSION_7, 30)] if case != "6-string/bass (no state)" else []
    session = SESSION_6 if case in ("other session", "6-string/bass (no state)") else SESSION_7
    kw = {"lowrange": 0} if case == "lowrange=0" else {"any_": 0} if case == "no tuner state" else {}
    want = run_wrong(stock, mean, session, table)
    got = run_wrong(patched, mean, session, table, **kw)
    assert want[0] == (mean <= 34.0)
    assert got == want  # warns like stock; x87 depth and every register as stock


@pytest.mark.parametrize("mean", MEANS)
@pytest.mark.parametrize("name", SEVEN_TUNINGS)
def test_w_seven_string_threshold_follows_the_lowest_string(emus, name, mean):
    stock, patched = emus
    _, thr = SEVEN_TUNINGS[name]
    table = [(SESSION_6, 12), (SESSION_7, thr)]
    got = run_wrong(patched, mean, SESSION_7, table)
    want = run_wrong(stock, mean, SESSION_7, table)
    assert got[0] == (mean <= thr)
    assert got[1:] == want[1:]  # x87 depth and registers as stock


def test_w_detects_the_correct_open_note_without_warning(emus):
    """The point: the open lowest string (A1/G1/F#1) no longer warns, one semitone below it still does."""
    _, patched = emus
    for name, (low, thr) in SEVEN_TUNINGS.items():
        table = [(SESSION_7, thr)]
        assert not run_wrong(patched, float(low), SESSION_7, table)[0], name
        if thr < 34:
            assert run_wrong(patched, float(low - 2), SESSION_7, table)[0], name


# ---- lane hooks: 2 * cell0 - cell1 for string 6, stock otherwise ---------------------------

GRID = DATA + 0x1000
CELL0 = (10.0, 20.0, 30.0)
CELL1 = (7.0, 16.0, 25.0)  # one string further in: lane 6 = (13, 24, 35)


def write_row(emu: Emu, row: int):
    """Six cells of 0x4C bytes from `row`; worldPts[1] (x,y,z) at cell + 0x3C."""
    for s in range(6):
        vals = CELL0 if s == 0 else CELL1 if s == 1 else (1.0 + s, 2.0 + s, 3.0 + s)
        emu.uc.mem_write(row + s * 0x4C + 0x3C, struct.pack("<3f", *vals))
    emu.uc.mem_write(row + 6 * 0x4C + 0x3C, struct.pack("<3f", -99.0, -99.0, -99.0))  # stock string-6 read


def run_r2(emu: Emu, string: int):
    va = site_va("R2")
    write_row(emu, GRID)
    ebp = STACK + 0x90000
    emu.uc.mem_write(ebp + 0x1C, struct.pack("<I", GRID - 8))  # grid base; EDX row offset below
    emu.uc.mem_write(ebp - 0x30, struct.pack("<f", -4.99))  # floor z (read by D1)
    emu.uc.mem_write(ebp + 0x10, struct.pack("<I", 0))  # open flag (D1)
    emu.uc.mem_write(ebp + 0x14, struct.pack("<I", string))  # string (D1)
    emu.uc.mem_write(ebp + 0x18, struct.pack("<I", 7))  # fret (D1)
    emu.run(
        va,
        va + 7,
        {
            UC_X86_REG_EBP: ebp,
            UC_X86_REG_EDX: 8,
            UC_X86_REG_EBX: string * 0x4C,
            UC_X86_REG_EAX: 0xA1A1,
            UC_X86_REG_ECX: 0xC1C1,
            UC_X86_REG_ESI: 0x5151,
            UC_X86_REG_EDI: 0xD1D1,
            UC_X86_REG_ESP: STACK + 0x80000,
            UC_X86_REG_EFLAGS: 0x202,
        },
    )
    top = emu.fpu_top()
    return emu.st0(), emu.regs(*ALL_REGS), emu.regs(UC_X86_REG_EFLAGS)[0] & FLAGS_MASK, top


@pytest.mark.parametrize("string", range(7))
def test_r2_note_head_lane(emus, diag, string):
    stock, patched = emus
    results = [run_r2(emu, string) for emu in (stock, patched)]
    if string < 6:
        assert results[1] == results[0]
    else:
        assert results[1][0] == pytest.approx(2 * CELL0[2] - CELL1[2])
        assert results[1][1:] == results[0][1:]


@pytest.mark.parametrize("sid,length", [("R3a", 6), ("R3b", 6), ("R3c", 6), ("R4", 7)])
@pytest.mark.parametrize("string", range(7))
def test_r3_r4_notefx_and_feedback_position(emus, sid, length, string):
    stock, patched = emus
    va = site_va(sid)
    results = []
    for emu in (stock, patched):
        write_row(emu, GRID)
        esp = STACK + 0x80000
        emu.uc.mem_write(esp, struct.pack("<I", 0x12345678))  # value for R4's pop esi
        emu.run(
            va,
            va + length,
            {
                UC_X86_REG_EAX: GRID,
                UC_X86_REG_ECX: string * 0x4C,
                UC_X86_REG_ESP: esp,
                UC_X86_REG_ESI: 0,
                UC_X86_REG_EFLAGS: 0x246,
            },
        )
        vec = struct.unpack("<3f", emu.read(emu.uc.reg_read(UC_X86_REG_EAX), 12))
        top = emu.fpu_top()
        results.append(
            (
                vec,
                emu.st0(),
                emu.regs(UC_X86_REG_ESI, UC_X86_REG_ESP)[0:2],
                emu.regs(UC_X86_REG_EFLAGS)[0] & FLAGS_MASK,
                top,
            )
        )
    if string < 6:
        assert results[1] == results[0]
    else:
        lane = tuple(2 * a - b for a, b in zip(CELL0, CELL1))
        assert results[1][0] == pytest.approx(lane)
        assert results[1][1] == pytest.approx(lane[0])
        assert results[1][2:] == results[0][2:]


@pytest.mark.parametrize("string", range(7))
def test_r6_lane_offset(emus, string):
    stock, patched = emus
    va = site_va("R6")
    results = []
    for emu in (stock, patched):
        write_row(emu, GRID)
        emu.run(
            va,
            va + 6,
            {
                UC_X86_REG_EAX: GRID - 0x10,
                UC_X86_REG_EDI: 0x10,
                UC_X86_REG_ECX: string * 0x4C,
                UC_X86_REG_EFLAGS: 0x202,
            },
        )
        results.append((emu.st0(), emu.regs(UC_X86_REG_EAX)[0], emu.regs(UC_X86_REG_EFLAGS)[0] & FLAGS_MASK))
    if string < 6:
        assert results[1] == results[0]
    else:
        assert results[1][0] == pytest.approx(2 * CELL0[1] - CELL1[1])
        assert results[1][1:] == results[0][1:]


# ---- colour manager (the X_CM reference; the stock-colour fallback C1-C5 / N11 went in 1.0) -----

MGR_PTR = 0x0135F58C


def test_colour_manager_ref_is_bound(emus):
    _, patched = emus
    assert struct.unpack("<I", patched.read(patched.exports["g_rs7_colormgr"], 4))[0] == MGR_PTR


# ---- [debug] diagnostics switch: R2 (D1) calls its logger only when it is 1 ----


@pytest.mark.parametrize("string", [0, 3, 6])
def test_r2_logger_calls_follow_the_diagnostics_switch(emus, diag, string):
    _, patched = emus
    want = 1 if diag and string in (0, 6) else 0
    with patched.count_calls("R2") as hits:
        run_r2(patched, string)
    assert len(hits) == want


# ---- diagnostics D2, D3, D5: the stubs must be invisible to the game -----------------------


@pytest.mark.parametrize("string", [0, 3, 6, 0xFF])
def test_d2_gem_create_entry_is_transparent(emus, string):
    stock, patched = emus
    va = site_va("D2")
    esp = STACK + 0x80000
    frame = struct.pack(
        "<7I",
        RET_MAGIC,
        0x0BADF00D,
        0x22222222,
        7,
        0xABCDEF00 | string,
        struct.unpack("<I", struct.pack("<f", 12.5))[0],
        2,
    )
    results = []
    for emu in (stock, patched):
        emu.uc.mem_write(esp, frame)
        emu.run(
            va,
            va + 5,
            {
                UC_X86_REG_ESP: esp,
                UC_X86_REG_EAX: 0x569A10B6,
                UC_X86_REG_ECX: 0xC1C1,
                UC_X86_REG_EDX: 0xD0D0,
                UC_X86_REG_EBX: 0xB1B1,
                UC_X86_REG_ESI: 0x5151,
                UC_X86_REG_EDI: 0xD1D1,
                UC_X86_REG_EBP: 0x1111,
                UC_X86_REG_EFLAGS: 0x8D7,
            },
        )
        top = emu.uc.reg_read(UC_X86_REG_ESP)
        results.append(
            (emu.regs(*ALL_REGS), emu.regs(UC_X86_REG_EFLAGS)[0] & FLAGS_MASK, emu.fpu_top(), emu.read(top, 0x28))
        )
    assert results[1] == results[0]


@pytest.mark.parametrize("mode", [0, 2, 4, 5])
@pytest.mark.parametrize("string", [0, 3, 6])
def test_d3_geom_build_mode_switch_is_transparent(emus, string, mode):
    stock, patched = emus
    va = site_va("D3")
    geom, gem = DATA + 0x5000, DATA + 0x5100
    results = []
    for emu in (stock, patched):
        emu.uc.mem_write(gem, bytes(0x0C) + bytes([string, 7, 0, 0]) + struct.pack("<I", 2) + bytes(0x6C))
        emu.uc.mem_write(geom, bytes(8) + struct.pack("<I", gem) + bytes(4) + struct.pack("<II", 1, mode) + bytes(0x30))
        emu.run(
            va,
            va + 6,
            {
                UC_X86_REG_ESP: STACK + 0x80000,
                UC_X86_REG_EDI: geom,
                UC_X86_REG_EAX: 0xA1A1,
                UC_X86_REG_ECX: 0xC1C1,
                UC_X86_REG_EDX: 0xD0D0,
                UC_X86_REG_EBX: 0xB1B1,
                UC_X86_REG_ESI: 0x5151,
                UC_X86_REG_EBP: 0x1111,
                UC_X86_REG_EFLAGS: 0x8D7,
            },
        )
        results.append((emu.regs(*ALL_REGS), emu.regs(UC_X86_REG_EFLAGS)[0] & FLAGS_MASK, emu.fpu_top()))
    assert results[1] == results[0]
    assert results[0][0][0] == mode


@pytest.mark.parametrize("string", [0, 3, 6, 0xFF])
def test_d5_gem_position_join_is_transparent(emus, string):
    stock, patched = emus
    va = site_va("D5")
    gem = DATA + 0x6000
    esp = STACK + 0x80000
    results = []
    for emu in (stock, patched):
        emu.uc.mem_write(
            gem, bytes(0x0C) + bytes([string, 7, 0, 0]) + struct.pack("<I", 2) + struct.pack("<3f", 0.5, 1.5, 2.5)
        )
        emu.uc.mem_write(esp, struct.pack("<10I", *range(0x100, 0x10A)))
        emu.uc.mem_write(esp + 0x1C, struct.pack("<3f", 10.0, 20.0, 30.0))
        emu.run(
            va,
            va + 7,
            {
                UC_X86_REG_ESP: esp,
                UC_X86_REG_EBX: gem,
                UC_X86_REG_EAX: 0xA1A1,
                UC_X86_REG_ECX: 0xC1C1,
                UC_X86_REG_EDX: 0xD0D0,
                UC_X86_REG_ESI: 0x5151,
                UC_X86_REG_EDI: 0xD1D1,
                UC_X86_REG_EBP: 0x1111,
                UC_X86_REG_EFLAGS: 0x8D7,
            },
        )
        top = emu.fpu_top()
        results.append(
            (emu.st0(), emu.regs(*ALL_REGS), emu.regs(UC_X86_REG_EFLAGS)[0] & FLAGS_MASK, top, emu.read(esp, 0x28))
        )
    assert results[1] == results[0]
    assert results[0][0] == 0.5
    assert results[0][1][0] == 0x104  # EAX = [esp+10h]


# ---- N1 / N5 / N21: 7th string line requests and the per-guitar update ---------------------------
# Stock songs never get a string object (S7): no 7-string mode (g_rs7_any7 = 0), or a GuitarStrings
# the load never marked ready, or a string other than 6. On those paths every stub must leave the
# registers, the stack and the flags it does not own exactly as stock. Building S7 needs the live
# model pool and is checked in game (log lines "string7: ...").

S7_ESP = STACK + 0x80000
S7_EBP = STACK + 0x81000
S7_MEM = DATA + 0x9000  # profile / GuitarStrings / guitar / info / pool for these tests
S7_REGS = {
    UC_X86_REG_EAX: 0xA0A0,
    UC_X86_REG_EBX: 0x0B0B,
    UC_X86_REG_ECX: 0x0C0C,
    UC_X86_REG_EDX: 0x0D05,
    UC_X86_REG_ESI: 0x5151,
    UC_X86_REG_EDI: 0xD1D1,
    UC_X86_REG_EBP: S7_EBP,
    UC_X86_REG_ESP: S7_ESP,
}


def set_s7(emu: Emu, on: int, any7: int):
    emu.uc.mem_write(emu.exports["g_rs7_s7_on"], struct.pack("<I", on))
    emu.uc.mem_write(emu.exports["g_rs7_any7"], struct.pack("<I", any7))


@pytest.fixture(params=[(1, 0), (1, 1), (0, 1)], ids=["on-mode-off", "on-mode-on", "switch-off"])
def s7_state(request, emus):
    _, patched = emus
    set_s7(patched, *request.param)
    yield request.param
    set_s7(patched, 1, 0)


def run_s7_site(emus, sid: str, regs: dict[int, int], mem: dict[int, bytes], flags: bool = False):
    """Runs both emulators from the site to site + length; returns (stock, patched) states:
    registers, flags (only where they are live after the site), the 0x20 stack bytes at ESP and
    the bytes of `mem`."""
    va = site_va(sid)
    length = next(s for s in sitetable.load(SITES)[1] if s.id == sid).length
    out = []
    for emu in emus:
        for a, b in mem.items():
            emu.uc.mem_write(a, b)
        emu.run(va, va + length, {**S7_REGS, **regs})
        esp = emu.uc.reg_read(UC_X86_REG_ESP)
        out.append(
            (
                emu.regs(*ALL_REGS),
                emu.regs(UC_X86_REG_EFLAGS)[0] & FLAGS_MASK if flags else None,
                emu.fpu_top(),
                emu.read(esp, 0x20),
                {a: emu.read(a, len(b)) for a, b in mem.items()},
            )
        )
    return out


def stack(*dwords: int) -> bytes:
    return struct.pack(f"<{len(dwords)}I", *dwords)


@pytest.mark.parametrize("count", [4, 6])
def test_n1_string_line_request_count(emus, s7_state, count):
    profile = bytearray(0x40)
    profile[0x38] = count
    stock, patched = run_s7_site(emus, "N1", {UC_X86_REG_EAX: S7_MEM}, {S7_MEM: bytes(profile)})
    on = s7_state[0]
    assert stock[0][0] == count
    if count == 6 and on:  # the one intended difference: a spare 7th instance
        assert patched[0][0] == 7 and patched[0][1:] == stock[0][1:] and patched[2:] == stock[2:]
    else:
        assert patched == stock
    assert stock[3][:8] == stack(1, 0x0C0C)  # push ecx; push 1


@pytest.mark.parametrize("sid,reg,idx", [("N1", UC_X86_REG_EAX, 0), ("N21", UC_X86_REG_ECX, 2)])
@pytest.mark.parametrize("ports", [0, 1])
@pytest.mark.parametrize("count", [4, 6, 7, 8])
def test_n1_n21_request_capacity_with_publish_ports(emus, sid, reg, idx, ports, count):
    """native_publish ports: profile counts 6 and 7 ([neck] clite builds 7) request the 8-slot capacity
    (a pool of 7 left string slot 7 without an entity: detail 15, the guitar never finished loading)."""
    _, patched = emus
    addr = patched.exports["g_rs7_publish_ports"]
    saved = patched.read(addr, 4)
    patched.uc.mem_write(addr, struct.pack("<I", ports))
    try:
        set_s7(patched, 1, 0)
        profile = bytearray(0x40)
        profile[0x38] = count
        regs = {reg: S7_MEM} if sid == "N1" else {UC_X86_REG_ECX: S7_MEM, UC_X86_REG_EAX: 0xA0A0}
        stock, out = run_s7_site(emus, sid, regs, {S7_MEM: bytes(profile)})
        want = 8 if ports and count in (6, 7) else 7 if count == 6 else count
        assert stock[0][idx] == count and out[0][idx] == want
    finally:
        patched.uc.mem_write(addr, saved)


@pytest.mark.parametrize("next_ptr", [0, S7_MEM + 0x600], ids=["je", "jne"])
@pytest.mark.parametrize("gs", [0, S7_MEM + 0x180, S7_MEM + 0x700], ids=["no-strings", "recorded", "unknown"])
def test_n5_update_without_a_7th_string_is_stock(emus, s7_state, gs, next_ptr):
    guitar = bytearray(0x40)
    struct.pack_into("<I", guitar, 0x0C, gs)
    struct.pack_into("<I", guitar, 0x18, next_ptr)
    stock, patched = run_s7_site(
        emus, "N5", {UC_X86_REG_EBX: S7_MEM + 0x500}, {S7_MEM + 0x500: bytes(guitar)}, flags=True
    )
    assert patched == stock
    assert stock[0][0] == next_ptr


# ---- 7th string highlight: N12/N13 (SNG string mask) ----


def run_mask_site(emu: Emu, sid: str, until: int, regs: dict[int, int]):
    emu.run(site_va(sid), until, {**S7_REGS, UC_X86_REG_EAX: 0xA0A0, **regs})
    return emu.regs(*ALL_REGS), emu.fpu_top()


@pytest.mark.parametrize("string", list(range(8)) + [0xFF])
def test_n12_string_mask_forward(emus, s7_state, string):
    """0x55CC71 (`cmp cl,6; jae skip` in PostProcessNotes): strings 0-5 and > 6 as stock; string 6
    goes on to the stock mask write (va + 5) only with [neck] string7_line on."""
    stock, patched = emus
    va, skip = site_va("N12"), site_target("N12")
    regs = {UC_X86_REG_ECX: 0x1200 | string}
    want_stock = va + 5 if string < 6 else skip
    want_patched = va + 5 if string < 6 or (string == 6 and s7_state[0]) else skip
    a = run_mask_site(stock, "N12", want_stock, regs)
    b = run_mask_site(patched, "N12", want_patched, regs)
    assert a == b


SONG = DATA + 0xD000  # fake SNG song: sections vector at +0F4h/+0F8h
SECTIONS = DATA + 0xD200


def write_song(emu: Emu, masks: list[int], level: int):
    sec = bytearray(0x58 * len(masks))
    for i, m in enumerate(masks):
        sec[i * 0x58 + 0x34 + level] = m
    emu.uc.mem_write(SECTIONS, bytes(sec))
    song = bytearray(0x100)
    struct.pack_into("<II", song, 0xF4, SECTIONS, SECTIONS + len(sec))
    emu.uc.mem_write(SONG, bytes(song))


@pytest.mark.parametrize(
    "masks, filled",
    [
        ([0x01, 0x00, 0x05, 0x00], [0x01, 0x00, 0x05, 0x00]),  # stock song: no bit 6
        ([0x00, 0x00, 0x40, 0x00, 0x40], [0x00, 0x40, 0x40, 0x40, 0x40]),  # string 6 lit one section early
        ([0x40, 0x00, 0x00], [0x40, 0x00, 0x00]),
    ],
    ids=["stock", "two", "first"],
)
@pytest.mark.parametrize("string", [0, 3, 5, 6])
def test_n13_string_mask_backfill(emus, s7_state, masks, filled, string):
    """0x55CE3A (bound of the back-fill loop, CL already incremented): CL < 6 loops on (target); after
    string 5 (CL = 6) the stock continuation va + 9, and the patched DLL first back-fills bit 6 for
    level EDI. Registers and FPU depth as stock."""
    stock, patched = emus
    va = site_va("N13")
    level = 3
    until = site_target("N13") if string < 6 else va + 9
    regs = {UC_X86_REG_ECX: 0x1300 | string, UC_X86_REG_ESI: SONG, UC_X86_REG_EDI: level}
    out = []
    for emu in emus:
        write_song(emu, masks, level)
        out.append(
            (
                run_mask_site(emu, "N13", until, regs),
                [emu.read(SECTIONS + i * 0x58 + 0x34 + level, 1)[0] for i in range(len(masks))],
            )
        )
    assert out[1][0] == out[0][0]
    assert out[0][1] == masks
    on = s7_state[0]
    assert out[1][1] == (filled if string == 6 and on else masks)


FRETS_MEM = DATA + 0x20000  # guitar / info / FretGrid for the F1 test (the grid is 0x72A0 bytes)


def set_frets(emu: Emu, on: int):
    emu.uc.mem_write(emu.exports["g_rs7_frets_on"], struct.pack("<I", on))


@pytest.mark.parametrize(
    "on,built",
    [(1, 0), (1, 4), (1, 6), (1, 5), (1, 9), (0, 0), (0, 7), (0, 8)],
    ids=["fret_extend-unknown", "bass", "six", "five", "nine", "off", "off-7", "off-8"],
)
def test_f1_fret_bar_transforms_without_a_7_string_guitar_are_stock(emus, on, built):
    """No guitar was built with 7/8 strings (info +0Bh; every 6-string / bass guitar, or fret_extend off): F1
    runs the stock body of 0x58FB20 as a subroutine and Rs7_FretsAfterApply returns at once. Zero fret bars
    (info +0Dh = 0) keep the stock body away from the game's entity setters; it still runs its prologue, the
    count check and the epilogue, so registers, stack and memory must match stock after the `ret`."""
    va = site_va("F1")
    guitar, info, grid = FRETS_MEM, FRETS_MEM + 0x100, FRETS_MEM + 0x1000
    g = bytearray(0x40)
    struct.pack_into("<I", g, 0x10, grid)
    struct.pack_into("<I", g, 0x24, info)
    inf = bytearray(0x40)
    inf[0x0B] = built
    mem = {
        guitar: bytes(g),
        info: bytes(inf),
        grid: stack(guitar) + bytes(0x7300 - 4),
        S7_ESP: stack(RET_MAGIC, 0x5A5A5A5A),
    }
    _, patched = emus
    set_frets(patched, on)
    out = []
    try:
        for emu in emus:
            for a, b in mem.items():
                emu.uc.mem_write(a, b)
            emu.run(va, RET_MAGIC, {**S7_REGS, UC_X86_REG_EAX: grid, UC_X86_REG_ECX: guitar})
            esp = emu.uc.reg_read(UC_X86_REG_ESP)
            out.append(
                (
                    emu.regs(*ALL_REGS),
                    emu.fpu_top(),
                    emu.read(esp - 0x10, 0x20),
                    {a: emu.read(a, len(b)) for a, b in mem.items() if a != S7_ESP},
                )
            )
    finally:
        set_frets(patched, 1)
    stock, patched_out = out
    assert patched_out[0] == stock[0] and patched_out[1] == stock[1] and patched_out[3] == stock[3]
    assert stock[0][0] == guitar and stock[0][7] == S7_ESP + 4  # EAX = [grid]; ret popped RET_MAGIC


HEAD_MEM = DATA + 0x30000  # guitar / headstock / PegSet for the native PegSet tests
PEG_MAGIC = 0x50375352  # "RS7P" at PegSet +1D0h (core/headstock.h; diagnostic only, review #1)
PEG_OWNERS = 64  # RS7_PEG_OWNERS: the native PegSet ownership table g_rs7_pegsets
PEG_MODES = ["native", "stock", "stale-tail"]  # registered / boot PegSet / rs7 tail bytes but not registered


def set_owners(emu: Emu, *pegsets: int):
    """g_rs7_pegsets = pegsets (the rest free). Native identification is this table only (review #1)."""
    tbl = list(pegsets) + [0] * (PEG_OWNERS - len(pegsets))
    emu.uc.mem_write(emu.exports["g_rs7_pegsets"], struct.pack(f"<{PEG_OWNERS}I", *tbl))


def owners(emu: Emu) -> list[int]:
    return [x for x in struct.unpack(f"<{PEG_OWNERS}I", emu.read(emu.exports["g_rs7_pegsets"], PEG_OWNERS * 4)) if x]


@pytest.fixture
def pegs(emus):
    """An empty ownership table in both emulators, before and after the test (module-scoped emulators)."""
    for e in emus:
        set_owners(e)
    yield
    for e in emus:
        set_owners(e)


def own_for(emus, ps: int, mode: str):
    for e in emus:
        set_owners(e, *([ps] if mode == "native" else []))


def set_head(emu: Emu, on: int):
    emu.uc.mem_write(emu.exports["g_rs7_head_on"], struct.pack("<I", on))


@pytest.fixture(params=[1, 0], ids=["headstock_peg", "switch-off"])
def head_on(request, emus):
    _, patched = emus
    set_head(patched, request.param)
    yield request.param
    set_head(patched, 1)


# ---- the native N-slot PegSet (headstock.h, native_pegset=1) ----


def run_ctor(emu: Emu, ps: int) -> bytes:
    emu.uc.mem_write(ps, b"\xcd" * 0x1D4)
    emu.uc.mem_write(S7_ESP, stack(RET_MAGIC))
    cm = emu.read(0x135F58C, 4)  # other tests point the colour manager at a fake: the ctor needs the real one
    emu.uc.mem_write(0x135F58C, snapshot().read(0x135F58C, 4))
    try:
        emu.run(0x59C190, RET_MAGIC, {UC_X86_REG_ESP: S7_ESP, UC_X86_REG_EDI: ps, UC_X86_REG_EBP: S7_EBP})
    finally:
        emu.uc.mem_write(0x135F58C, cm)
    assert emu.uc.reg_read(UC_X86_REG_EAX) == ps and emu.uc.reg_read(UC_X86_REG_EDI) == ps
    assert emu.fpu_top() == 0
    return emu.read(ps, 0x1D4)


def test_psc1_ctor_is_stock_for_the_six_and_extends_to_eight(emus, pegs):
    """Rnd_PegSet_Ctor with PSC1: header and slots 0-5 exactly as stock, slots 6/7 = slot 0's fresh
    defaults, pegColor[0..5] moved from +0FCh to +14Ch ([6]/[7] = [0]), the rs7 tail and magic."""
    stock, patched = (run_ctor(e, HEAD_MEM) for e in emus)
    assert patched[:0xFC] == stock[:0xFC]

    def slot(m, s):
        return m[0x0C + s * 0x28 : 0x0C + (s + 1) * 0x28]

    for s in (6, 7):
        assert slot(patched, s) == slot(stock, 0)
    for i in range(8):
        src = 0xFC + (i if i < 6 else 0) * 0x10
        assert patched[0x14C + i * 0x10 : 0x15C + i * 0x10] == stock[src : src + 0x10]
    assert patched[0x1CC:0x1D0] == bytes([8, 0, 0, 0])
    assert struct.unpack_from("<I", patched, 0x1D0)[0] == PEG_MAGIC
    assert stock[0x15C:0x1D4] == b"\xcd" * 0x78  # stock writes nothing past its 0x15C bytes
    assert owners(emus[1]) == [HEAD_MEM] and owners(emus[0]) == []  # PSC1 registered it (review #1)


def test_psc1_full_ownership_table_leaves_the_pegset_stock(emus, pegs):
    """No free entry: Rs7_PegSetCtor leaves the stock ctor's image (6 slots, pegColor at +0FCh)."""
    full = [DATA + 0x80000 + i * 0x10 for i in range(PEG_OWNERS)]
    set_owners(emus[1], *full)
    stock, patched = (run_ctor(e, HEAD_MEM) for e in emus)
    assert patched == stock
    assert owners(emus[1]) == full


DEC_FN = CODE + 0x9000  # stand-in for the InterlockedDecrement import: lock dec [arg]; ret 4
FAKE_FN = CODE + 0x9100  # 0x20 bytes per fake object: +0 delete (vtbl+4), +10h release (vtbl+8), both `ret`
FAKE_OBJ = DATA + 0x90000


def fake_pegset(emu: Emu, ps: int, slots: int, native: bool = True):
    """slots x (entity +4, locator +8, owning +0Ch) fake objects with their own vtables; refcount 1.
    native: the rs7 tail (capacity 8 at +1CCh, magic at +1D0h) as Rs7_PegSetCtor writes it."""
    emu.uc.mem_write(DEC_FN, bytes.fromhex("8B 44 24 04 F0 FF 08 8B 00 C2 04 00"))
    emu.uc.mem_write(FAKE_FN, b"\xc3" * 0x20 * 0x20)
    img = bytearray(0x1D4)
    for s in range(slots):
        for k, off in enumerate((0x04, 0x08, 0x0C)):
            n = s * 3 + k
            obj, vt = FAKE_OBJ + n * 0x20, FAKE_OBJ + 0x1000 + n * 0x10
            emu.uc.mem_write(obj, stack(vt, 1))
            emu.uc.mem_write(vt, stack(0, FAKE_FN + n * 0x20, FAKE_FN + n * 0x20 + 0x10))
            struct.pack_into("<I", img, 0x0C + s * 0x28 + off, obj)
    if native:
        img[0x1CC] = 8
        struct.pack_into("<I", img, 0x1D0, PEG_MAGIC)
    emu.uc.mem_write(ps, bytes(img))


def run_dtor(emu: Emu, ps: int) -> list[int]:
    iat = struct.unpack("<I", emu.read(0x59C307, 4))[0]  # mov ebx,[InterlockedDecrement]
    old = emu.read(iat, 4)
    emu.uc.mem_write(iat, stack(DEC_FN))
    hits: list[int] = []
    h = emu.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, u: hits.append(a), begin=FAKE_FN, end=FAKE_FN + 0x400)
    try:
        emu.uc.mem_write(S7_ESP, stack(RET_MAGIC))
        emu.run(0x59C300, RET_MAGIC, {UC_X86_REG_ESP: S7_ESP, UC_X86_REG_EAX: ps, UC_X86_REG_EBP: S7_EBP})
    finally:
        emu.uc.hook_del(h)
        emu.uc.mem_write(iat, old)
    return [a - FAKE_FN for a in hits]


@pytest.mark.parametrize("mode", PEG_MODES)
def test_psd_dtor_releases_eight_slots_in_the_stock_order(emus, pegs, mode):
    """Slot s: the owning +0Ch pointer deleted, the locator deleted, the entity released, s = 7..0
    for a native (registered) PegSet; the stock dtor - and the patched one on a PegSet constructed
    before the patch, even with rs7 tail bytes - does the same for 5..0."""
    stock, patched = emus
    native = mode == "native"
    fake_pegset(stock, HEAD_MEM, 8, mode != "stock")
    fake_pegset(patched, HEAD_MEM, 8, mode != "stock")
    own_for(emus, HEAD_MEM, mode)

    def want(top):
        return [x for s in range(top, -1, -1) for x in ((s * 3 + 2) * 0x20, (s * 3 + 1) * 0x20, s * 3 * 0x20 + 0x10)]

    assert run_dtor(stock, HEAD_MEM) == want(5)
    assert run_dtor(patched, HEAD_MEM) == want(7 if native else 5)


def peg_img(count: int, tail: bool) -> bytes:
    """tail: the rs7 tail bytes (capacity 8, magic) - which do not make a PegSet native (review #1)."""
    img = bytearray(0x1D4)
    struct.pack_into("<H", img, 4, count)
    if tail:
        img[0x1CC] = 8
        struct.pack_into("<I", img, 0x1D0, PEG_MAGIC)
    return bytes(img)


PEG_EXITS = {
    "PSU1": (0x59C3C8, 0x59C41A),
    "PSU2": (0x59C41A, 0x59C3D0),  # (fall-through, branch)
    "PSM1": (0x541BA3, 0x541D2B),
    "PSM2": (0x541D3D, 0x541B90),
}


def run_peg_branch(emu: Emu, sid: str, regs: dict[int, int], mem: dict[int, bytes]):
    """From the site to whichever exit it takes: (exit, registers)."""
    fall, branch = PEG_EXITS[sid]
    for a, b in mem.items():
        emu.uc.mem_write(a, b)
    h = emu.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, u: uc.emu_stop(), begin=branch, end=branch)
    try:
        for r, v in {**S7_REGS, **regs}.items():
            emu.uc.reg_write(r, v)
        emu.uc.emu_start(site_va(sid), fall, count=2000)  # the ownership scan: up to 64 entries
    finally:
        emu.uc.hook_del(h)
    eip = emu.uc.reg_read(UC_X86_REG_EIP)
    assert eip in (fall, branch), f"{sid} stopped at {eip:#x}"
    return eip, emu.regs(*ALL_REGS)


def peg_regs(sid: str, ps: int, i: int) -> tuple[dict[int, int], dict[int, bytes]]:
    holder = HEAD_MEM + 0x400  # PSM2: [[ebp+8]+8] = PegSet
    return {
        "PSU1": ({UC_X86_REG_EDI: ps, UC_X86_REG_ECX: i}, {}),
        "PSU2": ({UC_X86_REG_EDI: ps, UC_X86_REG_EBX: i}, {}),
        "PSM1": ({UC_X86_REG_EAX: ps, UC_X86_REG_ECX: i}, {}),
        "PSM2": ({UC_X86_REG_EBX: i}, {S7_EBP + 8: stack(holder), holder: stack(0, 0, ps)}),
    }[sid]


@pytest.mark.parametrize("sid", ["PSU1", "PSU2", "PSM1", "PSM2"])
@pytest.mark.parametrize("i", [0, 5, 6, 7, 8])
@pytest.mark.parametrize("mode", PEG_MODES)
def test_peg_slot_bounds_eight_only_for_a_native_pegset(emus, pegs, sid, i, mode):
    """Unload give-back loop (PSU1 entry / PSU2 latch) and the string renderer's peg mesh pass
    (PSM1 slot bound / PSM2 pass latch): bound 8 for a native PegSet, the stock bound ([+4] = 6, or
    6 passes) for a PegSet constructed before the patch - stock exit, stock registers."""
    ps = HEAD_MEM
    native = mode == "native"
    own_for(emus, ps, mode)
    regs, extra = peg_regs(sid, ps, i)
    out = [run_peg_branch(emu, sid, regs, {ps: peg_img(6, mode != "stock"), **extra}) for emu in emus]
    fall, branch = PEG_EXITS[sid]
    stock_in = i < 6
    patched_in = i < (8 if native else 6)
    inside = {"PSU1": fall, "PSU2": branch, "PSM1": fall, "PSM2": branch}[sid]  # the exit "still a slot"
    outside = branch if inside == fall else fall
    assert out[0][0] == (inside if stock_in else outside)
    assert out[1][0] == (inside if patched_in else outside)
    assert out[1][1] == out[0][1]


@pytest.mark.parametrize("mode", PEG_MODES)
def test_psu3_taken_flag_clear_count(emus, pegs, mode):
    ps = HEAD_MEM
    native = mode == "native"
    own_for(emus, ps, mode)
    stock, patched = run_s7_site(
        emus, "PSU3", {UC_X86_REG_EDI: ps, UC_X86_REG_EAX: ps + 0x0C}, {ps: peg_img(6, mode != "stock")}
    )
    assert stock[0][2] == 6 and patched[0][2] == (8 if native else 6)
    assert patched[0][:2] == stock[0][:2] and patched[0][3:] == stock[0][3:]


@pytest.mark.parametrize("mode", PEG_MODES)
def test_psa2_sized_free_matches_the_allocation(emus, pegs, mode):
    """The headstock destructor frees a native PegSet with 1D4h (PSA1's allocation), one constructed
    before the patch with the stock 15Ch. A native PegSet leaves the ownership table there (before
    the free: a PegSet later constructed at that address registers afresh); other entries stay."""
    ps, other = HEAD_MEM, HEAD_MEM + 0x800
    native = mode == "native"
    for e in emus:
        set_owners(e, *([other, ps] if native else [other]))
    stock, patched = run_s7_site(emus, "PSA2", {UC_X86_REG_ESI: ps}, {ps: peg_img(6, mode != "stock")})
    assert stock[3][:4] == stack(0x15C) and patched[3][:4] == stack(0x1D4 if native else 0x15C)
    assert patched[0] == stock[0]
    assert owners(emus[1]) == [other]


EDGE_PAGE = 0x5D380000  # one page with nothing mapped (or mappable from the snapshot) after it


@pytest.fixture(scope="module")
def edge_page(emus):
    """Maps EDGE_PAGE once per module and never unmaps it (Unicorn 2 mem_unmap between runs of cached code
    gave spurious fetch faults)."""
    import bisect

    end = EDGE_PAGE + 0x1000
    for e in emus:
        assert all(not (b <= end <= t) and not (b <= EDGE_PAGE <= t) for b, t, _ in e.uc.mem_regions())
        i = bisect.bisect_right(e.mem.bases, end) - 1
        assert i < 0 or e.mem.regions[i]["base"] + e.mem.regions[i]["size"] <= EDGE_PAGE
        e.uc.mem_map(EDGE_PAGE, 0x1000, UC_PROT_ALL)
    return end


@pytest.fixture
def edge_pegset(edge_page, pegs):
    """A stock (boot) PegSet whose 15Ch bytes end exactly at the end of a mapped page with nothing
    readable after it: any read of the old +1CCh/+1D0h tail check faults (review #1)."""
    return edge_page - 0x15C


@pytest.mark.parametrize("sid", ["PSA2", "PSD1", "PSD2", "PSU3", "PSL1"])
def test_peg_stock_pegset_at_a_page_end_is_never_read_past_its_allocation(emus, edge_pegset, sid):
    """Review #1: the native decision reads only the ownership table, so a stock 15Ch PegSet at the
    end of the heap's last readable page runs every site exactly as stock (the old magic check read
    74h bytes past it and faulted here)."""
    ps = edge_pegset
    img = bytearray(0x15C)
    struct.pack_into("<H", img, 4, 6)
    info = HEAD_MEM + 0x400
    inf = bytearray(0x40)
    inf[0x0B] = 6
    reg = {
        "PSA2": UC_X86_REG_ESI,
        "PSD1": UC_X86_REG_EAX,
        "PSD2": UC_X86_REG_EAX,
        "PSU3": UC_X86_REG_EDI,
        "PSL1": UC_X86_REG_EBX,
    }[sid]
    regs = {reg: ps, UC_X86_REG_EDI: info} if sid == "PSL1" else {reg: ps}
    if sid == "PSU3":
        regs[UC_X86_REG_EAX] = ps + 0x0C
    for e in emus:  # the dtor tests ran PSD1/PSD2's continuations: cached blocks would miss run()'s `until`
        e.uc.ctl_flush_tb()
    stock, patched = run_s7_site(emus, sid, regs, {ps: bytes(img), info: bytes(inf)})
    assert patched[0] == stock[0] and patched[3] == stock[3] and patched[4] == stock[4]
    if sid == "PSA2":
        assert patched[3][:4] == stack(0x15C)


@pytest.mark.parametrize("sid", ["PSU1", "PSU2", "PSM1", "PSM2"])
def test_peg_bounds_stock_pegset_at_a_page_end(emus, edge_pegset, sid):
    ps = edge_pegset
    img = bytearray(0x15C)
    struct.pack_into("<H", img, 4, 6)
    regs, extra = peg_regs(sid, ps, 6)
    out = [run_peg_branch(emu, sid, regs, {ps: bytes(img), **extra}) for emu in emus]
    assert out[1] == out[0]


@pytest.mark.parametrize(
    "count,mode,want", [(6, "native", 7), (4, "native", 4), (6, "stock", 6), (6, "stale-tail", 6), (5, "native", 5)]
)
def test_psl1_load_count(emus, pegs, head_on, count, mode, want):
    """A 6-string guitar's native PegSet loads 7 (switch on); bass, a PegSet constructed before the
    patch (with or without rs7 tail bytes) and the switch off load the instrument's count. ECX = 0."""
    info, ps = HEAD_MEM + 0x400, HEAD_MEM
    own_for(emus, ps, mode)
    img = bytearray(peg_img(0, mode != "stock"))
    inf = bytearray(0x40)
    inf[0x0B] = count
    stock, patched = run_s7_site(
        emus, "PSL1", {UC_X86_REG_EDI: info, UC_X86_REG_EBX: ps}, {ps: bytes(img), info: bytes(inf)}
    )
    assert stock[0][0] == count and stock[0][2] == 0
    exp = want if head_on else count
    assert patched[0][0] == exp and patched[0][1:] == stock[0][1:] and patched[2:] == stock[2:]


def test_psr1_reset_bound_is_the_active_peg_count(emus):
    session, guitar, info, head, ps = (HEAD_MEM + o for o in (0x400, 0x500, 0x600, 0x700, 0))
    s = bytearray(0x40)
    struct.pack_into("<I", s, 0x2C, guitar)
    g = bytearray(0x40)
    struct.pack_into("<I", g, 0x18, head)
    struct.pack_into("<I", g, 0x24, info)
    i = bytearray(0x40)
    i[0x0B] = 6
    h = bytearray(0x40)
    struct.pack_into("<I", h, 0x0C, ps)
    p = bytearray(0x10)
    struct.pack_into("<H", p, 4, 7)
    mem = {session: bytes(s), guitar: bytes(g), info: bytes(i), head: bytes(h), ps: bytes(p)}
    stock, patched = run_s7_site(emus, "PSR1", {UC_X86_REG_ESI: session}, mem)
    assert stock[0][0] == 6 and patched[0][0] == 7
    assert patched[0][2] == stock[0][2] == guitar  # ECX = the guitar, as stock


@pytest.mark.parametrize("index", range(8))
def test_psn1_colour_name(emus, index):
    """Index < 6: the stock table entry; 6/7 (past the table): entry 0, a string-colour name."""
    stock, patched = run_s7_site(
        emus,
        "PSN1",
        {UC_X86_REG_EBX: index, UC_X86_REG_EAX: 0x1111, UC_X86_REG_ESI: S7_MEM},
        {S7_MEM: bytes(0x30)},
    )
    table = struct.unpack("<6I", emus[1].read(0x12ED0C0, 24))
    assert patched[0][2] == table[index if index < 6 else 0]
    if index < 6:
        assert patched[:3] == stock[:3]


@pytest.mark.parametrize("player", [0, 1, 2])
@pytest.mark.parametrize("native", [False, True])
def test_psn1_tracks_player_only_in_owned_pegset(emus, pegs, player, native):
    session, guitar, head, ps = (HEAD_MEM + off for off in (0, 0x100, 0x200, 0x300))
    s, g, h, p = bytearray(0x30), bytearray(0x20), bytearray(0x10), bytearray(0x1D4)
    struct.pack_into("<I", s, 8, player)
    struct.pack_into("<I", s, 0x2C, guitar)
    struct.pack_into("<I", g, 0x18, head)
    struct.pack_into("<I", h, 0x0C, ps)
    p[0x1CE] = 0xA5
    if native:
        own_for(emus, ps, "native")
    stock, patched = run_s7_site(
        emus,
        "PSN1",
        {UC_X86_REG_EBX: 5, UC_X86_REG_ESI: session},
        {session: bytes(s), guitar: bytes(g), head: bytes(h), ps: bytes(p)},
    )
    expected = bytearray(p)
    if native and player < 2:
        expected[0x1CE] = player
    assert patched[:4] == stock[:4]  # register/FPU/stack behaviour remains stock; cold oracle checks flags
    assert patched[4][ps] == bytes(expected)
    for addr in (session, guitar, head):
        assert patched[4][addr] == stock[4][addr]


def test_psn2_null_slot_is_skipped(emus):
    """A NULL slot (an index the PegSet does not hold) returns as `ret 4` would, nothing called."""
    _, patched = emus
    patched.uc.mem_write(S7_ESP, stack(RET_MAGIC, 0))
    patched.run(patched.exports["Rs7_Stub_PSN2"], RET_MAGIC, {**S7_REGS, UC_X86_REG_ESP: S7_ESP, UC_X86_REG_EBX: 6})
    assert patched.uc.reg_read(UC_X86_REG_ESP) == S7_ESP + 8


@pytest.mark.parametrize("index", [0, 5])
def test_psn2_stock_index_reaches_the_dispatch_unchanged(emus, index):
    _, patched = emus
    patched.uc.mem_write(S7_ESP, stack(RET_MAGIC, HEAD_MEM + 0x0C))
    patched.run(
        patched.exports["Rs7_Stub_PSN2"],
        0x59BF80,
        {**S7_REGS, UC_X86_REG_ESP: S7_ESP, UC_X86_REG_EBX: index, UC_X86_REG_EAX: 0x1234},
    )
    assert patched.uc.reg_read(UC_X86_REG_ESP) == S7_ESP
    assert patched.regs(UC_X86_REG_EAX, UC_X86_REG_EBX) == (0x1234, index)
    assert patched.read(S7_ESP, 8) == stack(RET_MAGIC, HEAD_MEM + 0x0C)


def run_psl2(emus, img: bytes):
    """Both emulators from the PSL2 call 0x59C60C (ESP -> the pushed PegSet) through the stock
    `mov byte [ebx+8],1; mov eax,[ebp+8]` to 0x59C618, the callee 0x59CBC0 reduced to `ret 4`.
    Returns (stock, patched): EAX, ESP, the PegSet bytes."""
    ps = HEAD_MEM
    out = []
    for emu in emus:
        saved = emu.read(0x59CBC0, 3)
        emu.uc.mem_write(0x59CBC0, bytes.fromhex("C20400"))
        emu.uc.mem_write(ps, img)
        emu.uc.mem_write(S7_EBP + 8, stack(2))
        emu.uc.mem_write(S7_ESP, stack(ps, 0x55AA55AA))
        try:
            emu.run(site_va("PSL2"), 0x59C618, {**S7_REGS, UC_X86_REG_ESP: S7_ESP, UC_X86_REG_EBX: ps})
        finally:
            emu.uc.mem_write(0x59CBC0, saved)
        out.append(
            (
                emu.regs(UC_X86_REG_EAX, UC_X86_REG_ESP, UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI),
                emu.read(ps, len(img)),
            )
        )
    return out


def test_psl2_is_a_call_site_not_a_jmp_over_0x59c611():
    """0x59C587 `jne 0x59C615` enters 0x59C611's second instruction: PSL2 must leave 0x59C611..0x59C617
    alone (the issue #18 boot crash) and redirect only the call at 0x59C60C."""
    site = next(s for s in sitetable.load(SITES)[1] if s.id == "PSL2")
    assert site.kind == "call" and site_va("PSL2") == 0x59C60C


def test_psl2_completes_the_load_like_stock(emus, pegs):
    """A stock-layout PegSet (no magic): Rs7_PegSetLoaded leaves it alone; the call returns with its
    argument popped, +8 = 1, EAX = [ebp+8]."""
    img = bytearray(0x1D4)
    struct.pack_into("<H", img, 4, 6)
    stock, patched = run_psl2(emus, bytes(img))
    assert patched == stock
    assert stock[0][0] == 2 and stock[0][1] == S7_ESP + 4 and stock[1][8] == 1


def test_psl2_native_pegset_starts_with_six_active(emus, pegs):
    """A native PegSet that loaded 7: active count 6, loaded 7 in the tail (slot 6's entity is culled
    through 0x7AE370; NULL here: nothing to cull)."""
    own_for(emus, HEAD_MEM, "native")
    img = bytearray(0x1D4)
    struct.pack_into("<H", img, 4, 7)
    img[0x1CC] = 8
    struct.pack_into("<I", img, 0x1D0, PEG_MAGIC)
    stock, patched = run_psl2(emus, bytes(img))
    assert patched[0] == stock[0]
    m = patched[1]
    assert struct.unpack_from("<H", m, 4)[0] == 6 and m[0x1CD] == 7 and m[8] == 1


def _set_u32(emu: Emu, name: str, value: int):
    emu.uc.mem_write(emu.exports[name], struct.pack("<I", value))


# ---- BD1-BD4: perStringMode (bend direction) guards, string 6 -> string 0's mode (issue #14) ----

BEND_MEM = DATA + 0x48000
PSM = [1, 1, 1, 0, 0, 0]  # perStringMode of the menu snapshot guitar (strings 0-2 rotated)


def bend_setup(emu: Emu):
    info = bytearray(0x40)
    for s, m in enumerate(PSM):
        struct.pack_into("<I", info, 0x20 + 4 * s, m)
    struct.pack_into("<I", info, 0x20 + 4 * 6, 0x7777)  # what an unguarded index 6 would read
    guitar = bytearray(0x40)
    struct.pack_into("<I", guitar, 0x24, BEND_MEM + 0x100)
    session = bytearray(0x400)
    struct.pack_into("<I", session, 0x37C, BEND_MEM + 0x200)
    emu.uc.mem_write(BEND_MEM + 0x100, bytes(info))
    emu.uc.mem_write(BEND_MEM + 0x200, bytes(guitar))
    emu.uc.mem_write(BEND_MEM + 0x300, bytes(session))
    emu.uc.mem_write(S7_EBP - 0x18, struct.pack("<I", 0xDEAD))


# sid -> (string register, extra regs, join address of the body, how to read the chosen mode)
BEND_SITES = {
    "BD2": (UC_X86_REG_ECX, {UC_X86_REG_EAX: BEND_MEM + 0x100}, 0x7A664E, lambda e: e.uc.reg_read(UC_X86_REG_ECX)),
    "BD3": (UC_X86_REG_EBX, {UC_X86_REG_EDI: BEND_MEM + 0x300}, 0x7A6D9C, lambda e: e.uc.reg_read(UC_X86_REG_EDI)),
    "BD4": (
        UC_X86_REG_EBX,
        {UC_X86_REG_EAX: BEND_MEM + 0x200},
        0x7A7672,
        lambda e: struct.unpack("<I", e.read(S7_EBP - 0x18, 4))[0],
    ),
}


def run_bend(emu: Emu, sid: str, string: int, until: int):
    bend_setup(emu)
    if sid == "BD1":
        reg, regs = UC_X86_REG_ECX, {UC_X86_REG_EAX: BEND_MEM + 0x100}
    else:
        reg, regs, _, _ = BEND_SITES[sid]
    emu.run(site_va(sid), until, {**S7_REGS, **regs, reg: 0xAB00 | string})
    return emu.regs(*ALL_REGS)


@pytest.mark.parametrize("string", range(7))
@pytest.mark.parametrize("sid", ["BD2", "BD3", "BD4"])
def test_bd_bend_mode(emus, sid, string):
    _, _, join, mode = BEND_SITES[sid]
    stock_e, patched_e = emus
    patched = run_bend(patched_e, sid, string, join)
    m = mode(patched_e)
    if string < 6:
        stock = run_bend(stock_e, sid, string, join)
        assert patched == stock and m == mode(stock_e) == PSM[string]
    else:
        assert m == PSM[0]  # string 0's direction; stock would take the mode-2 branch


@pytest.mark.parametrize("string", [0, 3, 6, 7])
def test_bd1_bend_notation_branch(emus, string):
    """Mode 0 continues at 0x79FEAA, any other mode (and string > 6) jumps to 0x79FEB7."""
    until = 0x79FEAA if string in (3, 4, 5) else 0x79FEB7
    stock_e, patched_e = emus
    patched = run_bend(patched_e, "BD1", string, until)
    if string != 6:
        assert patched == run_bend(stock_e, "BD1", string, until)


# ---- 7th string bend (issue #16): N18 (SetBend guard), N19 (release guard) ----
# Without a shown S7 every string takes the stock continuation with the stock registers. The S7 path
# (the stock SetBend / release on the 7th string) needs the live string object and is checked in game
# ("string7 bend" log lines, tests/shots/bend).


def test_n18_n19_join_offsets():
    """N18 joins at resume + 10h (`cmp dword [ecx+4],0`), N19 at resume + 7 (`cmp byte [eax+0BDh],0FFh`)."""
    t = game_text()
    a = site_va("N18") + 9 + 0x10
    assert t[a - TEXT_VA : a - TEXT_VA + 4] == bytes.fromhex("83 79 04 00")
    b = site_va("N19") + 5 + 7
    assert t[b - TEXT_VA : b - TEXT_VA + 7] == bytes.fromhex("80 B8 BD 00 00 00 FF")
    assert site_target("N18") == site_target("N19") == 0x7E4095
    c = site_va("N20") + 5 + 7
    assert t[c - TEXT_VA : c - TEXT_VA + 7] == bytes.fromhex("80 B8 BD 00 00 00 FF")
    assert site_target("N20") == 0x7E4443


@pytest.mark.parametrize("count", [4, 6])
@pytest.mark.parametrize("string", [0, 3, 5, 6, 7, 0xFF])
@pytest.mark.parametrize("fret", [5, 0xFF])
def test_n18_bend_without_a_7th_string_is_stock(emus, s7_state, string, count, fret):
    info = bytearray(0x40)
    info[0x0B] = count
    va = site_va("N18")
    until = va + 9 if string < count else site_target("N18")
    out = []
    for emu in emus:
        emu.uc.mem_write(S7_MEM + 0x400, bytes(info))
        emu.uc.mem_write(S7_EBP - 0x64, struct.pack("<f", 0.5))
        emu.run(
            va,
            until,
            {
                **S7_REGS,
                UC_X86_REG_EAX: 0xAB00 | string,
                UC_X86_REG_EDX: 0xCD00 | fret,
                UC_X86_REG_ESI: S7_MEM + 0x400,
                UC_X86_REG_ECX: S7_MEM + 0x180,
            },
        )
        out.append((emu.regs(*ALL_REGS), emu.fpu_top()))
    assert out[1] == out[0]


@pytest.mark.parametrize("sid", ["N19", "N20"])
@pytest.mark.parametrize("count", [4, 6])
@pytest.mark.parametrize("string", [0, 3, 5, 6, 7])
def test_n19_n20_release_without_a_7th_string_is_stock(emus, s7_state, sid, string, count):
    info = bytearray(0x40)
    info[0x0B] = count
    va = site_va(sid)
    until = va + 5 if string < count else site_target(sid)
    out = []
    for emu in emus:
        emu.uc.mem_write(S7_MEM + 0x400, bytes(info))
        emu.run(
            va,
            until,
            {
                **S7_REGS,
                UC_X86_REG_EAX: 0xAB00 | string,
                UC_X86_REG_EDX: S7_MEM + 0x400,
                UC_X86_REG_ECX: S7_MEM + 0x180,
            },
        )
        out.append((emu.regs(*ALL_REGS), emu.fpu_top()))
    assert out[1] == out[0]


# ---- the 7th string's ghost (issue #16): N21 (type-2 request count), N22/N23 (ghost bend guards) ----


@pytest.mark.parametrize("count", [4, 6])
def test_n21_ghost_request_count(emus, s7_state, count):
    profile = bytearray(0x40)
    profile[0x38] = count
    stock, patched = run_s7_site(
        emus, "N21", {UC_X86_REG_ECX: S7_MEM, UC_X86_REG_EAX: 0xA0A0}, {S7_MEM: bytes(profile)}
    )
    on = s7_state[0]
    assert stock[0][2] == count
    if count == 6 and on:  # one spare ghost instance
        assert patched[0][2] == 7 and patched[0][:2] == stock[0][:2] and patched[0][3:] == stock[0][3:]
        assert patched[2:] == stock[2:]
    else:
        assert patched == stock
    assert stock[3][:8] == stack(1, 0xA0A0)  # push eax; push 1


def test_n22_n23_join_offsets():
    t = game_text()
    a = site_va("N22") + 5 + 0xC
    assert t[a - TEXT_VA : a - TEXT_VA + 4] == bytes.fromhex("83 79 04 00")
    b = site_va("N23") + 5 + 7
    assert t[b - TEXT_VA : b - TEXT_VA + 7] == bytes.fromhex("80 B9 BD 00 00 00 FF")
    assert (site_target("N22"), site_target("N23")) == (0x7E3F14, 0x7E4373)


@pytest.mark.parametrize("sid", ["N22", "N23"])
@pytest.mark.parametrize("count", [4, 6])
@pytest.mark.parametrize("string", [0, 3, 5, 6, 7])
def test_n22_n23_ghost_without_a_7th_string_is_stock(emus, s7_state, sid, string, count):
    info = bytearray(0x40)
    info[0x0B] = count
    va = site_va(sid)
    until = va + 5 if string < count else site_target(sid)
    reg = (
        {UC_X86_REG_EAX: 0xAB00 | string, UC_X86_REG_EDX: 0xCD05, UC_X86_REG_ECX: S7_MEM + 0x180}
        if sid == "N22"
        else {UC_X86_REG_ECX: 0xAB00 | string, UC_X86_REG_EDX: S7_MEM + 0x180}
    )
    out = []
    for emu in emus:
        emu.uc.mem_write(S7_MEM + 0x400, bytes(info))
        emu.uc.mem_write(S7_EBP - 0x60, struct.pack("<f", 0.75))
        emu.run(va, until, {**S7_REGS, **reg, UC_X86_REG_ESI: S7_MEM + 0x400})
        out.append((emu.regs(*ALL_REGS), emu.fpu_top()))
    assert out[1] == out[0]


# ---- AL1: entry of the nif memory handler 0x4A8560 (edited headstock model) ----------------


def set_asset(emu: Emu, on: int):
    emu.uc.mem_write(emu.exports["g_rs7_asset_on"], struct.pack("<I", on))


def test_al1_off_replays_the_stock_prologue(emus):
    """[assets] headstock7=0: `push ebp; mov ebp,esp; mov eax,[0x135FBFC]` exactly as stock, back at
    0x4A8568 with the same registers and stack."""
    _, patched = emus
    set_asset(patched, 0)
    args = stack(RET_MAGIC, S7_MEM, S7_MEM + 0x100, 0x200, 0)
    stock, ours = run_s7_site(emus, "AL1", {}, {S7_ESP: args})
    assert ours == stock


def test_al1_on_tail_jumps_into_the_c_wrapper(emus):
    """headstock7=1: the stub leaves the stack untouched and jumps to Rs7_NifHandler (cdecl, same
    arguments); Rs7_NifHandlerStock is the same replayed prologue as the off path."""
    _, patched = emus
    set_asset(patched, 1)
    try:
        esp = S7_ESP
        patched.uc.mem_write(esp, stack(RET_MAGIC, S7_MEM, S7_MEM + 0x100, 0x200, 0))
        va = site_va("AL1")
        patched.run(va, patched.exports["Rs7_NifHandler"], {**S7_REGS, UC_X86_REG_ESP: esp})
        assert patched.uc.reg_read(UC_X86_REG_ESP) == esp
        assert patched.read(esp, 4) == stack(RET_MAGIC)
    finally:
        set_asset(patched, 0)
    stock, _ = emus
    for emu, start in ((stock, site_va("AL1")), (patched, patched.exports["Rs7_NifHandlerStock"])):
        emu.uc.mem_write(S7_ESP, stack(RET_MAGIC, S7_MEM, S7_MEM + 0x100, 0x200, 0))
        emu.run(start, site_va("AL1") + 8, {**S7_REGS})
    assert patched.regs(*ALL_REGS) == stock.regs(*ALL_REGS)


# ---- Virtual guitarist stubs (TICK, JUD1, JUD2, AUD1; [test] vguitar=1 only, core/vguit.h)
VG_SITES = {  # site -> (C entry, registers, flags live after the site)
    "TICK": ("Rs7Vg_OnTick", {}, False),
    "JUD1": ("Rs7Vg_OnJudge", {UC_X86_REG_EBX: S7_MEM, UC_X86_REG_ESI: S7_MEM + 0x400}, False),
    "JUD2": ("Rs7Vg_OnScore", {UC_X86_REG_ECX: 0x1201, UC_X86_REG_EDX: S7_MEM + 0x400}, False),
    "AUD1": ("Rs7Vg_OnInput", {UC_X86_REG_ESI: S7_MEM, UC_X86_REG_EBX: S7_MEM + 0x200}, True),
}


def set_vg(emu: Emu, on: int):
    emu.uc.mem_write(emu.exports["g_rs7_vg_on"], struct.pack("<I", on))


@contextmanager
def vg_calls(emu: Emu, entry: str):
    """The C entry replaced with `ret` (it needs Win32); yields the (esp, 3 stack args) of each call."""
    addr = emu.exports[entry]
    saved = emu.read(addr, 1)
    emu.uc.mem_write(addr, b"\xc3")
    calls: list[tuple[int, tuple[int, int, int]]] = []

    def hook(uc, a, size, user):
        esp = uc.reg_read(UC_X86_REG_ESP)
        calls.append((esp, struct.unpack("<3I", bytes(uc.mem_read(esp + 4, 12)))))

    h = emu.uc.hook_add(UC_HOOK_CODE, hook, begin=addr, end=addr)
    try:
        yield calls
    finally:
        emu.uc.hook_del(h)
        emu.uc.mem_write(addr, saved)


def vg_live(sid: str, emus, ours, stock):
    """TICK / JUD2 are function entries: `and esp,-40h` points ESP below the caller's stack, at bytes
    the function has not written yet (whatever earlier runs left there): dead. Their live stack is the
    pushed EBP and the caller's frame above it; that is checked here and the dead bytes are dropped."""
    if sid not in ("TICK", "JUD2"):
        return ours, stock
    for emu in emus:
        assert emu.read(S7_ESP - 4, 4) == stack(S7_EBP)
        assert emu.read(S7_ESP, 12) == vg_mem()[S7_ESP][:12]
    return ours[:3] + ours[4:], stock[:3] + stock[4:]


def vg_mem():
    return {S7_ESP: stack(RET_MAGIC, S7_MEM + 0x400, 0x41700000, 0), S7_MEM: bytes(0x100), S7_MEM + 0x200: bytes(0x20)}


@pytest.mark.parametrize("sid", list(VG_SITES))
def test_vg_stub_off_replays_stock(emus, sid):
    _, patched = emus
    set_vg(patched, 0)
    entry, regs, flags = VG_SITES[sid]
    with vg_calls(patched, entry) as calls:
        stock, ours = run_s7_site(emus, sid, regs, vg_mem(), flags=flags)
    ours, stock = vg_live(sid, emus, ours, stock)
    assert ours == stock
    assert calls == []


@pytest.mark.parametrize("sid", list(VG_SITES))
def test_vg_stub_on_calls_in_and_is_transparent(emus, sid):
    """vguitar=1: one call with the documented arguments; registers, flags, FPU top, stack and the
    replayed instructions' effects equal stock."""
    _, patched = emus
    set_vg(patched, 1)
    entry, regs, flags = VG_SITES[sid]
    try:
        with vg_calls(patched, entry) as calls:
            stock, ours = run_s7_site(emus, sid, regs, vg_mem(), flags=flags)
    finally:
        set_vg(patched, 0)
    ours, stock = vg_live(sid, emus, ours, stock)
    assert ours == stock
    assert len(calls) == 1
    esp, args = calls[0]
    assert (esp + 4) % 16 in (0, 4, 8, 12)  # any; the stack below holds the 16-byte aligned fxsave area
    want = {
        "TICK": (S7_MEM + 0x400, 0x41700000),  # session, float 15.0
        "JUD1": (S7_MEM, S7_MEM + 0x400),  # note (EBX), session (ESI)
        "JUD2": (1, S7_MEM + 0x400, S7_MEM + 0x400),  # CL, EDX session, [esp+4] note
        "AUD1": (S7_MEM, S7_MEM + 0x200),
    }[sid]  # device (ESI), AkAudioBuffer (EBX)
    assert args[: len(want)] == want


# ---- TJ / TU / TUT / TN: seven-string tuning names (issue #15) ---------------------------------
# The C side (JSON reads, game string calls, GetCurrentThreadId) needs the game and Win32; it is
# covered by test_tuningname.cpp (the name table) and in game. Here: the stock paths.


def set_tn(emu: Emu, on: int):
    _set_u32(emu, "g_rs7_tn_on", on)


@pytest.mark.parametrize("sid", ["TJ", "TU", "TN"])
def test_tuning_name_entries_off_replay_the_stock_prologue(emus, sid):
    """[tuner] names=0: `push ebp; mov ebp,esp; sub esp,N` exactly as stock, same registers and stack."""
    _, patched = emus
    set_tn(patched, 0)
    args = stack(RET_MAGIC, S7_MEM, S7_MEM + 0x100, 0xFFFFFFF2)
    below = bytes(0x200)  # the frame the prologue opens: no garbage of earlier tests in the comparison
    stock, ours = run_s7_site(emus, sid, {UC_X86_REG_EAX: 0x1234}, {S7_ESP - 0x200: below, S7_ESP: args})
    assert ours == stock


def test_tj_on_without_a_tuning_json_is_stock(emus):
    """names=1, JSON argument 0: Rs7_TnJsonPre answers 0 and the stub replays the prologue with every
    register as it was (pushad/popad)."""
    _, patched = emus
    set_tn(patched, 1)
    try:
        args = stack(RET_MAGIC, 0, 0xFFFFFFF2)
        stock, ours = run_s7_site(emus, "TJ", {UC_X86_REG_EAX: 0x1234}, {S7_ESP: args})
        # Registers, FPU and the caller's frame; the new frame's uninitialised locals hold the C call's
        # leftovers (dead stack).
        assert (ours[0], ours[2], ours[4]) == (stock[0], stock[2], stock[4])
    finally:
        set_tn(patched, 0)


def test_tut_off_replays_the_three_instructions(emus):
    """names=0: `mov edx,[edi]; mov eax,[edx]; mov ecx,edi` (the Tuning object's AddRef setup)."""
    _, patched = emus
    set_tn(patched, 0)
    obj = stack(S7_MEM + 0x40) + bytes(0x3C) + stack(0xABCD1234)
    stock, ours = run_s7_site(emus, "TUT", {UC_X86_REG_EDI: S7_MEM}, {S7_MEM: obj})
    assert ours == stock
    assert stock[0][:3] == (0xABCD1234, 0x0B0B, S7_MEM)  # EAX, EBX, ECX


# ---- SE1 / SE2 / SE3: song end log (issue #13, core/songend.h; log only) -------------------------
SE_SITES = {
    "SE1": (1, {UC_X86_REG_ECX: S7_MEM + 0x400}),  # OnEndSongEvent: ECX session
    "SE2": (2, {UC_X86_REG_ESI: S7_MEM + 0x400}),  # leave-state: ESI session
    "SE3": (3, {UC_X86_REG_ESI: S7_MEM + 0x400}),
}  # UpdateState, song +78h NULL: ESI session


def set_endlog(emu: Emu, on: int):
    emu.uc.mem_write(emu.exports["g_rs7_endlog"], struct.pack("<I", on))


def se_mem():
    # [esp] return, [esp+4] event / state; session: vtable -> +0B0h a marker the SE3 replay loads
    return {
        S7_ESP: stack(RET_MAGIC, S7_MEM + 0x300, 3, 0),
        S7_MEM + 0x400: stack(S7_MEM + 0x200) + bytes(0x40),
        S7_MEM + 0x200: bytes(0xB0) + stack(0x00C0FFEE),
    }


def se_live(sid: str, ours, stock):
    """SE2 replays `and esp,-8`: the bytes at the new ESP may lie below the caller's stack (dead)."""
    return (ours[:3] + ours[4:], stock[:3] + stock[4:]) if sid == "SE2" else (ours, stock)


@pytest.mark.parametrize("sid", list(SE_SITES))
def test_se_stub_off_replays_stock(emus, sid):
    _, patched = emus
    set_endlog(patched, 0)
    try:
        with vg_calls(patched, "Rs7End_OnContext") as calls:
            stock, ours = run_s7_site(emus, sid, SE_SITES[sid][1], se_mem())
    finally:
        set_endlog(patched, 1)
    ours, stock = se_live(sid, ours, stock)
    assert ours == stock
    assert calls == []


@pytest.mark.parametrize("sid", list(SE_SITES))
def test_se_stub_on_calls_in_and_is_transparent(emus, sid):
    """song_end_log=1 (default): one call Rs7End_OnContext(site, context); the context points to the
    original game stack; registers, FPU top and memory equal stock."""
    _, patched = emus
    set_endlog(patched, 1)
    site, regs = SE_SITES[sid]
    with vg_calls(patched, "Rs7End_OnContext") as calls:
        stock, ours = run_s7_site(emus, sid, regs, se_mem())
    ours, stock = se_live(sid, ours, stock)
    assert ours == stock
    assert len(calls) == 1
    args = calls[0][1]
    assert args[0] == site
    assert struct.unpack("<I", patched.read(args[1] + 12, 4))[0] == S7_ESP
    if sid == "SE3":
        assert stock[0][0] == 0x00C0FFEE and stock[0][3] == S7_MEM + 0x200  # EAX, EDX of the replay


# ---- XB1: RS7X extension block at the end of SNG_Song_Load (issue #18, core/extblock.h) -----------
def test_xb1_hands_over_song_and_stream_then_replays_the_destructor(emus):
    """One call Rs7Ext_OnSngLoaded(song = [EBP+8], stream = site ESP+14h), then the stock
    `lea ecx,[esp+14h]; call 0x9D8B10` (the stream's destructor; +20h set = buffer not owned, so it
    only rewrites the vtables): registers, flags, FPU top and memory equal stock."""
    _, patched = emus
    stream = bytearray(0x24)
    stream[0x10:0x1C] = stack(S7_MEM + 0x600, 0x40, 0x48)  # buffer, position, size
    stream[0x20] = 1
    mem = {
        S7_ESP: stack(0x11, 0x22, 0x33, 0x44, 0x55) + bytes(stream) + stack(0x66, 0x77),
        S7_EBP + 8: stack(S7_MEM + 0x400),
    }
    with vg_calls(patched, "Rs7Ext_OnSngLoaded") as calls:
        stock, ours = run_s7_site(emus, "XB1", {}, mem, flags=True)
    assert ours == stock
    assert len(calls) == 1 and calls[0][1][:2] == (S7_MEM + 0x400, S7_ESP + 0x14)
    assert stock[4][S7_ESP][0x14:0x18] == stack(0x011DD9A8)  # the destructor ran (base vtable)


# ---- native N-slot chord data (issue #18 stage 2): template / ChordNotes pointer redirects -------

# site: (id register, stock vector begin register, destination register)
NS_TP = {
    "TP1": (UC_X86_REG_EDI, UC_X86_REG_ESI, UC_X86_REG_EAX),
    "TP2": (UC_X86_REG_EDI, UC_X86_REG_EBX, UC_X86_REG_EDI),
    "TP3": (UC_X86_REG_EBX, UC_X86_REG_EDI, UC_X86_REG_EBX),
    "TP4": (UC_X86_REG_EDI, UC_X86_REG_EBX, UC_X86_REG_EDX),
    "TP5": (UC_X86_REG_EDI, UC_X86_REG_ECX, UC_X86_REG_EDI),
    "TP6": (UC_X86_REG_EDI, UC_X86_REG_EBX, UC_X86_REG_EDX),
    "TP7": (UC_X86_REG_ESI, UC_X86_REG_EBX, UC_X86_REG_EBX),
    "TP8": (UC_X86_REG_EDI, UC_X86_REG_EBX, UC_X86_REG_EDX),
    "TP11": (UC_X86_REG_ECX, UC_X86_REG_ESI, UC_X86_REG_EDX),
}
NS_CP = {
    "CP1": (UC_X86_REG_EAX, UC_X86_REG_ESI, UC_X86_REG_EAX),
    "CP2": (UC_X86_REG_ESI, UC_X86_REG_EBX, UC_X86_REG_EBX),
}
NS_BEGIN, NS_SUPER = 0x12340000, 0x22220000


def ns_set(emu: Emu, entries: list[tuple[int, ...]]):
    """g_rs7_ns: (tplBegin, tplCount, t8, cnBegin, cnCount, c8, song, origCn) per entry, rest free."""
    blob = b"".join(struct.pack("<8I", *e) for e in entries)
    emu.uc.mem_write(emu.exports["g_rs7_ns"], blob + b"\0" * (64 * 32 - len(blob)))


def run_ns(emus, sid: str, idx: int):
    reg_idx, reg_base, _ = {**NS_TP, **NS_CP}[sid]
    regs = {UC_X86_REG_EAX: 0xA0A0, reg_base: NS_BEGIN, reg_idx: idx}
    return run_s7_site(emus, sid, regs, {})


@pytest.mark.parametrize("sid", sorted({**NS_TP, **NS_CP}))
def test_ns_redirect_without_a_registered_song_is_stock(emus, sid):
    _, patched = emus
    ns_set(patched, [(0x1000, 50, 0x5000, 0x2000, 50, 0x6000, 1, 50)])  # another song only
    try:
        stock, ours = run_ns(emus, sid, 3)
    finally:
        ns_set(patched, [])
    assert ours == stock


@pytest.mark.parametrize("sid", sorted(NS_TP))
@pytest.mark.parametrize("idx,inside", [(0, True), (3, True), (9, True), (10, False)])
def test_ns_template_redirect_returns_the_t8_record(emus, sid, idx, inside):
    _, patched = emus
    ns_set(patched, [(0x1000, 50, 0x5000, 0, 0, 0, 1, 0), (NS_BEGIN, 10, NS_SUPER, 0, 0, 0, 2, 0)])
    try:
        stock, ours = run_ns(emus, sid, idx)
    finally:
        ns_set(patched, [])
    dst = ALL_REGS.index(NS_TP[sid][2])
    assert stock[0][dst] == NS_BEGIN + idx * 0x48
    want = list(stock[0])
    if inside:
        want[dst] = NS_SUPER + idx * 0x80
    assert list(ours[0]) == want and ours[2:] == stock[2:]


@pytest.mark.parametrize("sid", sorted(NS_CP))
@pytest.mark.parametrize("idx,inside", [(0, True), (4, True), (5, False)])
def test_ns_chordnotes_redirect_returns_the_c8_record(emus, sid, idx, inside):
    _, patched = emus
    ns_set(patched, [(NS_BEGIN, 10, NS_SUPER, NS_BEGIN, 5, NS_SUPER + 0x100000, 2, 5)])
    try:
        stock, ours = run_ns(emus, sid, idx)
    finally:
        ns_set(patched, [])
    dst = ALL_REGS.index(NS_CP[sid][2])
    assert stock[0][dst] == NS_BEGIN + idx * 0x948
    want = list(stock[0])
    if inside:
        want[dst] = NS_SUPER + 0x100000 + idx * 0x15A8
    assert list(ours[0]) == want and ours[2:] == stock[2:]


# ---- native N-slot chord rendering: TM1-TM6 (TestStringMask over C8 mask[8]), DS1 (double-stop count)


def tm_mem(masks8: list[int]) -> dict[int, bytes]:
    """One record readable both ways: stock mask[6] at +0, C8 mask[8] at +948h."""
    return {
        S7_MEM: struct.pack("<6I", *masks8[:6]),
        S7_MEM + 0x948: struct.pack("<8I", *masks8),
        S7_ESP: stack(0x2000),
    }  # the flag the caller pushed


@pytest.mark.parametrize("sid", [f"TM{i}" for i in range(1, 7)])
@pytest.mark.parametrize("string", [0, 3, 5, 6, 7, 8])
def test_tm_test_string_mask_reads_c8(emus, sid, string):
    masks = [0x2000, 0, 0x2004, 0x1000, 0x2000, 0, 0x2004, 0x2000]
    stock, ours = run_s7_site(emus, sid, {UC_X86_REG_ECX: S7_MEM, UC_X86_REG_EAX: 0xA0A00 | string}, tm_mem(masks))
    eax = ALL_REGS.index(UC_X86_REG_EAX)
    want = 1 if string < 8 and masks[string] & 0x2000 else 0
    assert ours[0][eax] & 0xFF == want
    if string < 6:
        assert stock[0][eax] & 0xFF == want
    esp = ALL_REGS.index(UC_X86_REG_ESP)
    assert ours[0][esp] == stock[0][esp] == S7_ESP + 4


@pytest.mark.parametrize("idx", [0, 2])
def test_ds1_double_stop_count_reads_t8_frets(emus, idx):
    _, patched = emus
    ns_set(patched, [(NS_BEGIN, 4, NS_SUPER, 0, 0, 0, 2, 0)])
    try:
        stock, ours = run_s7_site(emus, "DS1", {UC_X86_REG_ESI: NS_BEGIN, UC_X86_REG_ECX: idx}, {})
    finally:
        ns_set(patched, [])
    r = {k: ALL_REGS.index(k) for k in (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX)}
    assert (stock[0][r[UC_X86_REG_EAX]], stock[0][r[UC_X86_REG_ECX]], stock[0][r[UC_X86_REG_EDX]]) == (
        NS_BEGIN + idx * 0x48 + 4,
        6,
        0,
    )
    assert (ours[0][r[UC_X86_REG_EAX]], ours[0][r[UC_X86_REG_ECX]], ours[0][r[UC_X86_REG_EDX]]) == (
        NS_SUPER + idx * 0x80 + 0x48,
        7,
        0,
    )


# ---- native N-slot chord matcher: the port (Rs7Ns_MatchPort) against the stock 0x4E7B30 ---------

MATCH_FN = 0x4E7B30
MT = DATA + 0x30000  # matcher, input, history, detector, frame, targets


def matcher_setup(
    emu: Emu, peaks: list[tuple[int, float]], level: float = -20.0, bass: bool = False, octave_flag: int = 0
):
    m, inp, hist, det, frames = MT, MT + 0x100, MT + 0x200, MT + 0x2000, MT + 0x4000
    emu.uc.mem_write(m, struct.pack("<II", inp, det) + b"\0" * 0x60)
    emu.uc.mem_write(m + 0x54, bytes([octave_flag]))
    emu.uc.mem_write(inp, struct.pack("<II", 0, hist))
    blob = bytearray(0x1000)
    struct.pack_into("<IIIII", blob, 0xDB8, frames, 1, 2, 0, 2)  # base, index 1, capacity 2, -, available 2
    emu.uc.mem_write(hist, bytes(blob))
    d = bytearray(0x1400)
    struct.pack_into("<I", d, 0x11EC, 2 if bass else 0)
    emu.uc.mem_write(det, bytes(d))
    fr = bytearray(4000)
    for f in (0, 1):  # frame 1 = current (history 0), frame 0 = one back
        base = f * 2000
        struct.pack_into("<f", fr, base + 0x700, level)
        struct.pack_into("<I", fr, base + 0xBC, len(peaks))
        for k, (b, s) in enumerate(peaks):
            struct.pack_into("<If", fr, base + 0x1C + 8 * k, b, s)
    emu.uc.mem_write(frames, bytes(fr))


MATCH_ALLOC = 0x8FB9C0  # the game allocator the stock matcher's vector uses
MATCH_FREES = (0x4E7FB9, 0x4E83B5, 0x4E8574)  # its `call eax` frees (memory manager vfunc +8, 3 args)


@contextmanager
def match_heap(emu: Emu):
    """The stock matcher's heap calls without the game's memory manager (TLS, locks): the allocator
    returns a bump buffer, the frees are skipped."""
    nxt = [MT + 0x10000]

    def alloc(uc, addr, size, user):
        n = uc.reg_read(UC_X86_REG_EAX)
        uc.reg_write(UC_X86_REG_EAX, nxt[0])
        nxt[0] += (n + 15) & ~15
        esp = uc.reg_read(UC_X86_REG_ESP)
        uc.reg_write(UC_X86_REG_EIP, struct.unpack("<I", uc.mem_read(esp, 4))[0])
        uc.reg_write(UC_X86_REG_ESP, esp + 4)

    def free(uc, addr, size, user):
        uc.reg_write(UC_X86_REG_ESP, uc.reg_read(UC_X86_REG_ESP) + 12)
        uc.reg_write(UC_X86_REG_EIP, addr + 2)

    hooks = [emu.uc.hook_add(UC_HOOK_CODE, alloc, begin=MATCH_ALLOC, end=MATCH_ALLOC)]
    hooks += [emu.uc.hook_add(UC_HOOK_CODE, free, begin=a, end=a) for a in MATCH_FREES]
    try:
        yield
    finally:
        for h in hooks:
            emu.uc.hook_del(h)


def run_long(emu: Emu, start: int, esp: int):
    emu.uc.reg_write(UC_X86_REG_ESP, esp)
    emu.uc.reg_write(UC_X86_REG_EBP, STACK + 0x81000)
    emu.uc.emu_start(start, RET_MAGIC, count=3_000_000)
    assert emu.uc.reg_read(UC_X86_REG_EIP) == RET_MAGIC, f"stopped at {emu.uc.reg_read(UC_X86_REG_EIP):#x}"
    return emu.uc.reg_read(UC_X86_REG_EAX) & 0xFF


def stock_match(emu: Emu, targets6: list[int], tol: int = 0, hist: int = 0) -> int:
    tg = MT + 0x6000
    emu.uc.mem_write(tg, struct.pack("<6i", *targets6))
    esp = STACK + 0x80000
    emu.uc.mem_write(esp, struct.pack("<IIIII", RET_MAGIC, MT, tg, tol, hist))
    with match_heap(emu):
        return run_long(emu, MATCH_FN, esp)


def port_match(emu: Emu, targets: list[int], tol: int = 0, hist: int = 0) -> int:
    tg = MT + 0x6000
    t8 = (targets + [-1] * 8)[:8]
    emu.uc.mem_write(tg, struct.pack("<8i", *t8))
    esp = STACK + 0x80000
    emu.uc.mem_write(esp, struct.pack("<IIIiII", RET_MAGIC, MT, tg, 8, tol, hist))
    return run_long(emu, emu.exports["Rs7Ns_MatchPort"], esp)


def harmonics(midi: int, strength: float) -> list[tuple[int, float]]:
    # fundamental and the bins of partials 2-5 (+12, +19, +24, +28), decaying
    return [
        (midi + o, strength * w) for o, w in ((0, 1.0), (12, 0.6), (19, 0.4), (24, 0.3), (28, 0.2)) if midi + o < 128
    ]


def random_case(rng):
    n = rng.randint(1, 6)
    targets = [rng.randint(30, 76) for _ in range(n)]
    if rng.random() < 0.3 and n > 1:
        targets[rng.randrange(n)] = targets[0]  # duplicate
    slots = targets + [-1] * (6 - n)
    rng.shuffle(slots)
    peaks: dict[int, float] = {}
    for t in targets:
        if rng.random() < 0.8:  # played
            for b, s in harmonics(t, rng.uniform(0.2, 3.0)):
                peaks[b] = peaks.get(b, 0.0) + s
    for _ in range(rng.randint(0, 6)):  # noise
        b = rng.randint(20, 100)
        peaks[b] = peaks.get(b, 0.0) + rng.uniform(0.05, 1.5)
    plist = list(peaks.items())[:20]
    return slots, plist, rng.choice([0, 0, 1]), rng.choice([0, 1])


@pytest.mark.parametrize("seed", range(40))
def test_matcher_port_equals_stock_for_six_or_fewer_targets(emus, seed):
    import random

    rng = random.Random(seed)
    stock, patched = emus
    for case in range(10):
        slots, peaks, tol, hist = random_case(rng)
        level = rng.choice([-20.0, -20.0, -60.0]) if case else -20.0
        for emu in (stock, patched):
            matcher_setup(emu, peaks, level=level)
        want = stock_match(stock, slots, tol, hist)
        got = port_match(patched, slots, tol, hist)
        assert got == want, (seed, case, slots, peaks, tol, hist)


@pytest.mark.parametrize(
    "chord", [[40, 47, 52, 55, 59, 64, 35], [40, 47, 52, 55, 59, 64, 35, 30]], ids=["7 notes", "8 notes"]
)
@pytest.mark.parametrize("missing,want", [(0, 1), (1, 1), (2, 1), (3, 0)])
def test_matcher_port_wide_chords_may_miss_two(emus, chord, missing, want):
    """[chords] wide_misses=2 (default): a 7/8-note chord passes with up to two notes missing (any
    string), not with three. The missing notes are ones no other chord note's partials cover."""
    _, patched = emus
    gone = [55, 35, 40][:missing]
    peaks = {}
    for t_ in chord:
        if t_ in gone:
            continue
        for b, s in harmonics(t_, 1.0)[:2]:
            peaks[b] = peaks.get(b, 0.0) + s
    matcher_setup(patched, list(peaks.items()))
    assert port_match(patched, chord) == want


def test_matcher_port_wide_misses_setting(emus):
    _, patched = emus
    chord = [40, 47, 52, 55, 59, 64, 35]
    peaks = {}
    for t_ in chord:
        for b, s in harmonics(t_, 1.0)[:2]:
            peaks[b] = peaks.get(b, 0.0) + s
    del peaks[35]  # the low B's fundamental missing (its octave 47 is also the chord's B2)
    var = patched.exports["g_rs7_match_misses"]
    try:
        patched.uc.mem_write(var, struct.pack("<I", 0))  # every note required
        matcher_setup(patched, list(peaks.items()))
        assert port_match(patched, chord) == 0
        patched.uc.mem_write(var, struct.pack("<I", 1))
        assert port_match(patched, chord) == 1
    finally:
        patched.uc.mem_write(var, struct.pack("<I", 2))
    assert port_match(patched, chord[:6]) == 1


@pytest.mark.parametrize("seed", range(12))
def test_md1_up_to_six_targets_run_the_stock_body_on_the_compacted_array(emus, seed):
    """MD1 at 0x4E7B30 with an int32[8] holding at most six targets (holes anywhere) = stock 0x4E7B30
    with those targets compacted to int32[6]."""
    import random

    rng = random.Random(1000 + seed)
    stock, patched = emus
    for case in range(4):
        slots, peaks, tol, hist = random_case(rng)
        eight = slots + [-1, -1]
        rng.shuffle(eight)
        compact = [x for x in eight if x >= 0] + [-1] * (6 - sum(x >= 0 for x in eight))
        for emu in (stock, patched):
            matcher_setup(emu, peaks)
        want = stock_match(stock, compact, tol, hist)
        tg = MT + 0x6000
        patched.uc.mem_write(tg, struct.pack("<8i", *eight))
        esp = STACK + 0x80000
        patched.uc.mem_write(esp, struct.pack("<IIIII", RET_MAGIC, MT, tg, tol, hist))
        with match_heap(patched):
            got = run_long(patched, MATCH_FN, esp)
        assert got == want, (seed, case, eight, tol, hist)
        assert patched.uc.reg_read(UC_X86_REG_ESP) == esp + 4 + 16  # ret 10h


@pytest.mark.parametrize("sid", ["EV1", "EV2", "EV3", "EV4", "EV5"])
def test_ev_thunks_move_the_eval_targets_to_the_int32_8(emus, sid):
    _, patched = emus
    var = patched.exports[f"g_rs7_target_{sid}"]
    saved = patched.read(var, 4)
    patched.uc.mem_write(var, struct.pack("<I", RET_MAGIC))
    try:
        esp = STACK + 0x80000
        patched.uc.mem_write(esp, struct.pack("<II", 0x11111111, S7_MEM + 0x2C))
        patched.run(patched.exports[f"Rs7_Stub_{sid}"], RET_MAGIC, {UC_X86_REG_ESP: esp})
        assert struct.unpack("<II", patched.read(esp, 8)) == (0x11111111, S7_MEM + 0x284)
    finally:
        patched.uc.mem_write(var, saved)


def test_cs1_clears_the_eval_tails(emus):
    ev = bytearray(0xC28)
    ev[0x284:0x2BC] = bytes(range(1, 0x39))
    stock, ours = run_s7_site(
        emus, "CS1", {UC_X86_REG_EAX: S7_MEM, UC_X86_REG_ECX: 0, UC_X86_REG_EDX: 0xFFFFFFFF}, {S7_MEM: bytes(ev)}
    )
    assert ours[0] == stock[0]
    _, patched = emus
    got = patched.read(S7_MEM, 0xC28)
    assert struct.unpack("<ii", got[0x29C:0x2A4]) == (-1, -1)
    assert got[0x2A4:0x2AC] == bytes(8) and got[0x2AC:0x2B4] == b"\xff" * 8 and got[0x2B4:0x2BC] == bytes(8)
    assert got[0x284:0x29C] == bytes(range(1, 0x19))  # the template copy's midi[6] untouched
    assert got[0xC1C:0xC20] == b"\0\0\xff\xff"


@pytest.mark.parametrize("with_cn", [False, True])
def test_bt1_writes_the_eval_tails_and_returns(emus, with_cn):
    _, patched = emus
    t8, c8, ev = MT + 0x8000, MT + 0x9000, MT + 0xC000
    tpl = bytearray(0x80)
    tpl[0x48:0x50] = bytes([0, 2, 2, 0xFF, 0xFF, 0xFF, 0, 0xFF])
    tpl[0x50:0x58] = bytes([0xFF] * 6 + [1, 0xFF])
    struct.pack_into("<8i", tpl, 0x58, 40, 47, 52, -1, -1, -1, 35, -1)
    cn = bytearray(0x15A8)
    struct.pack_into("<8I", cn, 0x948, *([0] * 6 + [0x2004, 0]))
    cn[0x968:0x978] = b"\xff" * 16
    cn[0x96E] = 5
    patched.uc.mem_write(t8, bytes(tpl))
    patched.uc.mem_write(c8, bytes(cn))
    patched.uc.mem_write(ev, bytes(0xC28))
    ns_set(patched, [(NS_BEGIN, 1, t8, NS_BEGIN + 0x100, 1, c8, 2, 1)])
    ebp = STACK + 0x81000
    # [ebp] saved EBP, [ebp+4] return, [ebp+8] eval, [ebp+14h] template, [ebp+18h] ChordNotes; below: EDI ESI EBX
    patched.uc.mem_write(ebp, struct.pack("<7I", 0xEB0EB0, RET_MAGIC, ev, 0, 0, t8, c8 if with_cn else 0))
    esp = ebp - 12
    patched.uc.mem_write(esp, struct.pack("<III", 0xD1D1, 0x5151, 0xB0B0))
    try:
        patched.run(
            patched.exports["Rs7_Stub_BT1"],
            RET_MAGIC,
            {UC_X86_REG_ESP: esp, UC_X86_REG_EBP: ebp, UC_X86_REG_EBX: ev, UC_X86_REG_EAX: ev},
        )
    finally:
        ns_set(patched, [])
    assert patched.regs(UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP) == (
        ev,
        0xB0B0,
        0x5151,
        0xD1D1,
        0xEB0EB0,
    )
    assert patched.uc.reg_read(UC_X86_REG_ESP) == ebp + 8 + 0x14
    got = patched.read(ev, 0xC28)
    assert struct.unpack("<ii", got[0x29C:0x2A4]) == (35, -1)
    assert got[0x2AC:0x2B0] == bytes([0, 0xFF, 1, 0xFF])
    if with_cn:
        assert struct.unpack("<II", got[0x2A4:0x2AC]) == (0x2004, 0) and got[0x2B0:0x2B4] == bytes(
            [5, 0xFF, 0xFF, 0xFF]
        )
    else:
        assert got[0x2A4:0x2AC] == bytes(8) and got[0x2B0:0x2B4] == b"\xff" * 4


def test_lu3_sets_targets_6_and_7(emus):
    mem = {S7_EBP - 8: stack(0x12345678, 0x9ABCDEF0), S7_EBP + 8: stack(0x4321)}
    stock, ours = run_s7_site(emus, "LU3", {UC_X86_REG_ECX: 0x77, UC_X86_REG_EAX: 0xA0A0}, mem)
    assert ours[0] == stock[0]
    _, patched = emus
    assert struct.unpack("<ii", patched.read(S7_EBP - 8, 8)) == (-1, -1)
    assert struct.unpack("<I", patched.read(S7_EBP - 0x10, 4))[0] == 0x77


# ---- native N-slot chord tracking: the child queries AG1-AG3 against stock 0x875780/0x875A10/0x875C90

AGG = {"AG1": 0x875780, "AG2": 0x875A10, "AG3": 0x875C90}
TRK = DATA + 0x60000  # tracker: evals at +8, 0xC28 each


def agg_setup(emu: Emu, rng, whole: int, s67=(0xFF, 0xFF)):
    evals = bytearray(8 + 12 * 0xC28)
    parent = 8 + 10 * 0xC28
    evals[parent + 0xC1C] = 1
    evals[parent + 0xC1D] = whole
    for k in (0x54, 0x55, 0x56, 0x64, 0x74, 0x75, 0x76, 0x84):
        evals[parent + k] = rng.choice([0, 1, 1, 3])
    for s in range(6):
        c = rng.choice([-1, -1, s, s + 4])
        struct.pack_into("<i", evals, parent + 0xC04 + 4 * s, c)
    evals[parent + 0xC1E], evals[parent + 0xC1F] = s67
    for c in range(10):
        e = 8 + c * 0xC28
        for k in (0x54, 0x55, 0x56, 0x74, 0x75, 0x76):
            evals[e + k] = rng.choice([0, 1, 1, 2, 3])
        evals[e + 0x64] = rng.choice([0, 0, 1])
        evals[e + 0x84] = rng.choice([0, 0, 1])
    emu.uc.mem_write(TRK, bytes(evals))
    emu.uc.mem_write(TRK + 0x10000, bytes([rng.randint(0, 7)]))  # string for the string query
    return TRK + parent


def run_agg(emu: Emu, fn: int, eval_: int, string_query: bool) -> int:
    esp = STACK + 0x80000
    emu.uc.mem_write(esp, struct.pack("<I", RET_MAGIC))
    emu.run(
        fn,
        RET_MAGIC,
        {
            UC_X86_REG_ESP: esp,
            UC_X86_REG_ESI: eval_,
            UC_X86_REG_EDI: TRK,
            UC_X86_REG_EDX: TRK + 0x10000 if string_query else 0,
            UC_X86_REG_EAX: 0x12345600,
        },
    )
    return emu.uc.reg_read(UC_X86_REG_EAX)


@pytest.mark.parametrize("sid", sorted(AGG))
@pytest.mark.parametrize("seed", range(25))
def test_ag_child_queries_equal_stock_for_strings_0_to_5(emus, sid, seed):
    import random

    rng = random.Random(seed * 7 + int(sid[-1]))
    stock, patched = emus
    for case in range(6):
        whole = 1 if case == 0 else 0
        string_query = sid != "AG1" and case % 2 == 1
        for emu in (stock, patched):
            st = rng.getstate()
            ev = agg_setup(emu, rng, whole)
        rng.setstate(st)
        ev = agg_setup(stock, rng, whole)
        if string_query and stock.read(TRK + 0x10000, 1)[0] >= 6:
            continue  # stock answers 0 for strings 6/7 by its guard; ours reads their children
        want = run_agg(stock, AGG[sid], ev, string_query)
        got = run_agg(patched, AGG[sid], ev, string_query)
        assert got == want, (sid, seed, case)


def test_ag_children_of_strings_6_and_7_take_part(emus):
    import random

    rng = random.Random(5)
    _, patched = emus
    ev = agg_setup(patched, rng, 0, s67=(2, 0xFF))
    for s in range(6):
        patched.uc.mem_write(ev + 0xC04 + 4 * s, struct.pack("<i", -1))
    child = TRK + 8 + 2 * 0xC28
    patched.uc.mem_write(child + 0x64, b"\x01")
    patched.uc.mem_write(child + 0x84, b"\x00")
    patched.uc.mem_write(child + 0x56, b"\x00")
    assert run_agg(patched, AGG["AG1"], ev, False) & 0xFF == 0  # string 6's child failed
    patched.uc.mem_write(child + 0x56, b"\x01")
    assert run_agg(patched, AGG["AG1"], ev, False) & 0xFF == 1


# ---- loop-latch predicates PD2-PD5: stock for strings 0-5, strings 6/7 from the eval tails ------

PRED = {0x877D20: (UC_X86_REG_EDX, 0x800), 0x877D60: (UC_X86_REG_ECX, 0x401810), 0x877DD0: (UC_X86_REG_EDX, 0x10)}
EVP = DATA + 0x70000


def pred_eval(emu: Emu, rng, tails: bool):
    ev = bytearray(0xC28)
    for s in range(6):
        struct.pack_into("<i", ev, 0x284 + 4 * s, rng.choice([-1, 40 + s]))
        struct.pack_into("<I", ev, 0x2BC + 4 * s, rng.choice([0, 0x800, 0x10, 0x1000, 0x400000, 0x810]))
        ev[0x278 + s] = rng.choice([0xFF, 0, 3, 5])
        ev[0xBEC + s] = rng.choice([0xFF, 7, 2])
    if tails:
        struct.pack_into("<ii", ev, 0x29C, rng.choice([-1, 35]), -1)
        struct.pack_into("<II", ev, 0x2A4, rng.choice([0, 0x800, 0x10]), 0)
        ev[0x2AC:0x2B4] = bytes([rng.choice([0xFF, 2]), 0xFF, 0xFF, 0xFF, rng.choice([0xFF, 6]), 0xFF, 0xFF, 0xFF])
    else:
        struct.pack_into("<ii", ev, 0x29C, -1, -1)
        ev[0x2A4:0x2AC] = bytes(8)
        ev[0x2AC:0x2B4] = b"\xff" * 8
    emu.uc.mem_write(EVP, bytes(ev))
    return ev


def run_pred(emu: Emu, fn: int, reg: int) -> int:
    esp = STACK + 0x80000
    emu.uc.mem_write(esp, struct.pack("<I", RET_MAGIC))
    emu.run(
        fn,
        RET_MAGIC,
        {UC_X86_REG_ESP: esp, reg: EVP, UC_X86_REG_EAX: 0x55555500, UC_X86_REG_EBX: 0x0B0B, UC_X86_REG_ESI: 0x5151},
    )
    return emu.uc.reg_read(UC_X86_REG_EAX)


@pytest.mark.parametrize("fn", sorted(PRED) + [0x877E10, 0x877CF0])
@pytest.mark.parametrize("seed", range(20))
def test_pd_predicates_without_string_6_are_stock(emus, fn, seed):
    import random

    stock, patched = emus
    reg = PRED.get(fn, (UC_X86_REG_ECX, 0))[0]
    rng = random.Random(seed)
    st = rng.getstate()
    pred_eval(stock, rng, False)
    rng.setstate(st)
    pred_eval(patched, rng, False)
    assert run_pred(patched, fn, reg) == run_pred(stock, fn, reg)


def test_pd_string_6_takes_part(emus):
    _, patched = emus
    import random

    ev = bytearray(0xC28)
    struct.pack_into("<8i", ev, 0x284, 40, -1, -1, -1, -1, -1, 35, -1)
    struct.pack_into("<I", ev, 0x2BC, 0x800)
    struct.pack_into("<II", ev, 0x2A4, 0, 0)
    ev[0x278:0x27E] = bytes([0xFF] * 6)
    ev[0x2AC:0x2B4] = b"\xff" * 8
    patched.uc.mem_write(EVP, bytes(ev))
    assert run_pred(patched, 0x877D20, UC_X86_REG_EDX) & 0xFF == 0  # string 6 sounds without a slide
    patched.uc.mem_write(EVP + 0x2A4, struct.pack("<I", 0x800))
    assert run_pred(patched, 0x877D20, UC_X86_REG_EDX) & 0xFF == 1
    patched.uc.mem_write(EVP + 0x284, struct.pack("<i", -1))
    assert run_pred(patched, 0x877CF0, UC_X86_REG_ECX) == 35  # first sounding MIDI: string 6's


# ---- technique fixes for string 6 (docs/ARCHITECTURE.md): I1 inverse pitch ----
# Rs7_Base6 reads the side store (covered by the C++ unit tests); here
# they are replaced by `mov eax,[cell]; ret` so the stubs' own logic runs against the stock code.


@contextmanager
def fake_c(emu: Emu, name: str, cell: int, value: int):
    addr = emu._stub_calls(emu.exports[f"Rs7_Stub_{name}"])[0]  # the stub's one C callee
    saved = emu.read(addr, 6)
    emu.uc.mem_write(cell, struct.pack("<I", value & 0xFFFFFFFF))
    emu.uc.mem_write(addr, bytes([0xA1]) + struct.pack("<I", cell) + bytes([0xC3]))
    try:
        yield
    finally:
        emu.uc.mem_write(addr, saved)


def call_midi_to_fret(emu: Emu, midi: int, string: int, tuning: list[int] | None, instrument: int):
    esi = 0
    if tuning is not None:
        esi = DATA + 0x200
        emu.uc.mem_write(esi, struct.pack(f"<{len(tuning)}h", *tuning))
    esp = STACK + 0x80000
    emu.uc.mem_write(esp, struct.pack("<II", RET_MAGIC, instrument))
    emu.run(
        0x874050,
        RET_MAGIC,
        {
            UC_X86_REG_ESP: esp,
            UC_X86_REG_ECX: midi,
            UC_X86_REG_EDX: string,
            UC_X86_REG_ESI: esi,
            UC_X86_REG_EBP: STACK + 0x90000,
        },
    )
    return emu.uc.reg_read(UC_X86_REG_EAX) & 0xFFFFFFFF, emu.uc.reg_read(UC_X86_REG_ESP)


def test_i1_site_is_in_play_midi_to_fret():
    assert site_va("I1") == 0x874056 and site_target("I1") == 0x874079


@pytest.mark.parametrize("instrument", [0, 1, 2])
@pytest.mark.parametrize("string", range(6))
@pytest.mark.parametrize("tuning", [None, [0] * 6, [-2, 0, 1, 0, 0, 0]])
def test_i1_strings_0_to_5_and_bass_are_stock(emus, instrument, string, tuning):
    stock, patched = emus
    with fake_c(patched, "I1", DATA + 0x300, 35):
        assert call_midi_to_fret(patched, 52, string, tuning, instrument) == call_midi_to_fret(
            stock, 52, string, tuning, instrument
        )


@pytest.mark.parametrize("tuning", [None, [0] * 7])
def test_i1_string_6_of_a_bass_is_stock(emus, tuning):
    stock, patched = emus
    with fake_c(patched, "I1", DATA + 0x300, 35):
        assert call_midi_to_fret(patched, 40, 6, tuning, 2) == call_midi_to_fret(stock, 40, 6, tuning, 2)


@pytest.mark.parametrize("instrument", [0, 1])
def test_i1_string_6_not_in_seven_string_mode_is_stock(emus, instrument):
    stock, patched = emus
    with fake_c(patched, "I1", DATA + 0x300, 0x80000000):
        for tuning in (None, [0] * 7):
            assert call_midi_to_fret(patched, 40, 6, tuning, instrument) == call_midi_to_fret(
                stock, 40, 6, tuning, instrument
            )


@pytest.mark.parametrize("t6", [0, -1, -3, 2])
@pytest.mark.parametrize("fret", [0, 3, 7, 12])
def test_i1_string_6_is_the_inverse_of_the_forward_pitch(emus, t6, fret):
    _, patched = emus
    with fake_c(patched, "I1", DATA + 0x300, 35 + t6):
        got, esp = call_midi_to_fret(patched, 35 + t6 + fret, 6, [0] * 6 + [t6], 0)
    assert got == fret
    assert esp == STACK + 0x80000 + 4  # plain ret, the instrument argument stays for the caller


def test_i1_string_6_without_tuning_pointer_is_stock(emus):
    stock, patched = emus
    with fake_c(patched, "I1", DATA + 0x300, 35):
        assert call_midi_to_fret(patched, 40, 6, None, 0) == call_midi_to_fret(stock, 40, 6, None, 0)
