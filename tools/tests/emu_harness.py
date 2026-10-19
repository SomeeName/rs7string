"""Shared Unicorn harness of the emulator tests: run the rs7string stubs in Unicorn on the game snapshot and compare them with stock code.

Two emulators share the same setup; only the patched one has the code sites (+5 data/code
references) rewritten exactly as the DLL does (jmp/rel32/call into the DLL's stubs, resume/target
variables set, ref variables bound to the dword at their site). S1 and S4
call C code that uses the side store and are covered by the C++ unit tests (test_mode.cpp)
instead; S3's C function is pure and runs in the emulator. The diagnostic
logger calls (S2, and D1-D5 in R2, R1, D2, D3, D5) are neutralised (see VOID_C_CALLS, VOID_C_EXPORTS); the C
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

import capstone
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

import game_files
import sitecheck
import sitetable
from emu_common import DLL, EXE, REPO, ROOT, SITES, load_dll, require_game_exe

SNAPSHOT = game_files.SNAPSHOT
sys.path.insert(0, str(game_files.SCRIPTS))

needs_emu = pytest.mark.skipif(  # every emulator test module: `pytestmark = needs_emu`
    not DLL.exists() or not (SNAPSHOT / "memory.bin").exists(), reason="needs the built DLL and the memory snapshot"
)

STACK = 0x5D000000  # free in the snapshot (checked when the base address was chosen)
DATA = 0x5D100000
CODE = 0x5D200000  # helper code: fstp dword [DATA_OUT]
DATA_OUT = 0x5D1F0000
RET_MAGIC = 0x5D2F0000
TEB = 0x5D300000  # fake TEB page (fs base), see Emu.__init__
FLAGS_MASK = 0x8D5  # CF PF AF ZF SF OF

# Stubs whose C calls are diagnostics only (S2 -> Rs7_NoteString6; D2, D3,
# D5 -> Rs7_Diag*: void cdecl loggers; R2's D1 logger is in VOID_C_EXPORTS). The loggers need Win32 imports the emulator does not
# provide, so their entries are replaced with `ret`; the stubs' own pushad / pushfd / push / call /
# stack restore / popfd / popad still run.
VOID_C_CALLS = ("S2", "D2", "D3", "D5")
MID_CALLBACKS = {
    "D2": "Rs7_Mid_D2",
    "D3": "Rs7_Mid_D3",
    "SE1": "Rs7_Mid_SE1",
    "SE2": "Rs7_Mid_SE2",
    "SE3": "Rs7_Mid_SE3",
    # cold sites converted from naked stubs (features/*/hooks*.cpp, test_midhook_cold.py)
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
    "PGH1": "Rs7_Mid_PGH1",  # 3+3 peg glow: skip meshes of the hidden peg part (features/headstock/assetload_pegs.cpp)
    "GLW": "Rs7_Mid_GLW",  # RSMods pure red glow (features/colours/hooks.cpp); a no-op while g_rs7_glow_fix = 0
    # stubs of the former hooks/stubs.cpp converted in refactor step 7 (test_midhook_cold.py)
    "BD1": "Rs7_Mid_BD1",  # bend direction guards (features/string7/hooks_benddir.cpp), light
    "BD2": "Rs7_Mid_BD2",
    "BD3": "Rs7_Mid_BD3",
    "BD4": "Rs7_Mid_BD4",
    "D5": "Rs7_Mid_D5",  # gem position log (features/diag/hooks.cpp)
    "B1": "Rs7_Mid_B1",  # chord frame marks (features/chords/hooks_frames.cpp)
    "N12": "Rs7_Mid_N12",  # string 6 in the SNG section mask (features/string7/hooks_mid.cpp)
    "N13": "Rs7_Mid_N13",
    "PM0": "Rs7_Mid_PM0",  # native pitch tails (features/pitch/hooks_mid.cpp)
    "PR0": "Rs7_Mid_PR0",
    "PM1": "Rs7_Mid_PM1",
    # batch 3 (test_midhook_cold.py, test_midhook_nslot.py)
    "TF1": "Rs7_Mid_TF1",  # tuner camera reference string (features/tuner/hooks_camera.cpp), light
    "V2": "Rs7_Mid_V2",  # slide end cell stride (features/fretgrid/hooks_slide.cpp), light
    # native N-slot chords (features/nslot/hooks_records.cpp, hooks_eval.cpp, hooks_latches.cpp)
    "TP1": "Rs7_Mid_TP1",
    "TP2": "Rs7_Mid_TP2",
    "TP3": "Rs7_Mid_TP3",
    "TP4": "Rs7_Mid_TP4",
    "TP5": "Rs7_Mid_TP5",
    "TP6": "Rs7_Mid_TP6",
    "TP7": "Rs7_Mid_TP7",
    "TP8": "Rs7_Mid_TP8",
    "TP11": "Rs7_Mid_TP11",
    "TP12": "Rs7_Mid_TP12",
    "DS1": "Rs7_Mid_DS1",
    "CP1": "Rs7_Mid_CP1",
    "CP2": "Rs7_Mid_CP2",
    "LU3": "Rs7_Mid_LU3",
    "WC2": "Rs7_Mid_WC2",
    "CS1": "Rs7_Mid_CS1",
    "CC3": "Rs7_Mid_CC3",
    "CH3": "Rs7_Mid_CH3",
    "PL1": "Rs7_Mid_PL1",
    "UP1": "Rs7_Mid_UP1",
    "UP2": "Rs7_Mid_UP2",
    "UP3": "Rs7_Mid_UP3",
    "PD2": "Rs7_Mid_PD2",
    "PD3": "Rs7_Mid_PD3",
    "PD4": "Rs7_Mid_PD4",
    "PD5": "Rs7_Mid_PD5",
    # batch 2b (features/string7/hooks_mid.cpp, features/pitch/hooks_mid.cpp)
    "N5": "Rs7_Mid_N5",
    "N18": "Rs7_Mid_N18",
    "N19": "Rs7_Mid_N19",
    "N20": "Rs7_Mid_N20",
    "N22": "Rs7_Mid_N22",
    "N23": "Rs7_Mid_N23",
    "I1": "Rs7_Mid_I1",
    "SF1": "Rs7_Mid_SF1",  # call site (Before placement): built with Rs7Mid_BuildCall
    # batch 4, call sites (features/nslot/hooks_calls.cpp, features/pitch/hooks_calls.cpp, features/headstock/hooks_mid.cpp)
    **{f"TM{i}": "Rs7_Mid_TM" for i in range(1, 7)},  # one callback, light
    **{f"EV{i}": "Rs7_Mid_EV" for i in range(1, 6)},  # one callback, light
    "S4": "Rs7_Mid_S4",
    "PC2": "Rs7_Mid_PC2",
    "PC3": "Rs7_Mid_PC3",
    "PSL2": "Rs7_Mid_PSL2",  # After placement
    **{sid: f"Rs7_Mid_{sid}" for sid in ("PP1", "F1", "IN1", "PSN2", "TJ", "TU", "TN")},  # Wrap placement (CALL_WRAP)
    "AM1": "Rs7_Mid_AM1Before",  # Wrap placement, no after callback
    "MD1": "Rs7_Mid_MD1Before",
    "GIA1": "Rs7_Mid_GIA1",
    **{
        sid: f"Rs7_Mid_{sid}" for sid in ("GSA1", "GSC1", "GRM3", "BT1", "AG1", "AG2", "AG3", "PE0")
    },  # call / jmp sites
    # asm reduction part 2 (features/pitch/hooks_calls.cpp, features/publish/hooks_pitch.cpp), light
    "PC1": "Rs7_Mid_PC1",
    "PC4": "Rs7_Mid_PC4",
    **{f"PUBZ{i}": "Rs7_Mid_PUBZ" for i in range(7)},  # one callback: call sites (0, 4-6) and jmp sites (1-3)
    # asm reduction part 3: the plain-PSARC unit's jmp_atomic8 sites (features/psarc/hooks_mid.cpp), full
    "PP_TOC": "Rs7_Mid_PP_TOC",
    "PP_SNGF": "Rs7_Mid_PP_SNGF",
    # x87 result (Context::PushSt0) and jmp_atomic8 sites (features/string7/hooks_lanes.cpp, features/pitch/hooks_mid.cpp)
    "R3a": "Rs7_Mid_R3a",
    "R3b": "Rs7_Mid_R3b",
    "R3c": "Rs7_Mid_R3c",
    "R4": "Rs7_Mid_R4",
    "S3": "Rs7_Mid_S3",
    "PT1": "Rs7_Mid_PT1",
}
# Call sites registered with Rs7Hook::midCall = Rs7Mid::Call::After (keep in sync with the registration tables).
CALL_AFTER: set[str] = {"PSL2"}
# Sites registered with Rs7_WrapRow (Rs7Mid::Call::Wrap; keep in sync with the registration tables): sid -> (stack
# argument dwords, bytes the callee pops, scratch bytes, before callback, after callback), callbacks as export names or
# None. MID_CALLBACKS lists the after callback, or the before callback of a site without one.
CALL_WRAP: dict[str, tuple[int, int, int, str | None, str | None]] = {
    "PP1": (2, 8, 0, None, "Rs7_Mid_PP1"),
    "F1": (0, 0, 0, None, "Rs7_Mid_F1"),  # a function entry
    "IN1": (1, 4, 0, None, "Rs7_Mid_IN1"),
    "PSN2": (1, 4, 0, "Rs7_Mid_PSN2Before", "Rs7_Mid_PSN2"),
    "AM1": (4, 16, 32, "Rs7_Mid_AM1Before", None),
    "MD1": (4, 16, 32, "Rs7_Mid_MD1Before", None),  # a function entry
    "GIA1": (2, 0, 0, "Rs7_Mid_GIA1Before", "Rs7_Mid_GIA1"),
    "GSC1": (1, 4, 0, None, "Rs7_Mid_GSC1"),
    **{sid: (n, 0, 0, f"Rs7_Mid_{sid}Before", f"Rs7_Mid_{sid}") for sid, n in (("TJ", 2), ("TU", 0), ("TN", 2))},
}
# Sites registered with Rs7Hook::midMode = Rs7Mid::Mode::Light; keep in sync with the registration tables. Their
# entries are built with Rs7Mid_BuildLight, and test_midhook_emu.py checks their callbacks for x87/MMX/SSE code.
LIGHT_CALLBACKS: set[
    str
] = {  # BD*, TF1, V2, N19/N20/N23, N-slot but TP11, DS1 (loader thread), PL1 (FP helper); TM*, EV*
    "AG1",
    "AG2",
    "AG3",
    "PE0",
    "BD1",
    "BD2",
    "BD3",
    "BD4",
    "TF1",
    "V2",
    "TP1",
    "TP2",
    "TP3",
    "TP4",
    "TP5",
    "TP6",
    "TP7",
    "TP8",
    "TP12",
    "CP1",
    "CP2",
    "LU3",
    "WC2",
    "CS1",
    "CC3",
    "CH3",
    "UP1",
    "UP2",
    "UP3",
    "PD2",
    "PD3",
    "PD4",
    "PD5",
    "N19",
    "N20",
    "N23",
    *(f"TM{i}" for i in range(1, 7)),
    *(f"EV{i}" for i in range(1, 6)),
    "PC1",
    "PC4",
    *(f"PUBZ{i}" for i in range(7)),
}
# Refactor step 7 batch 5: feature stubs converted to mid-hooks (test_midhook_features.py). Light ones in BATCH5_LIGHT.
BATCH5_MID = (
    *("CLBM", "CLBA", "CLGU", "CLGE", "CLVB"),  # features/chords/hooks_cl.cpp
    *("NG1", "NG9", "FGRB"),  # features/nextgrid/hooks_mid.cpp, features/fretgrid/hooks_build.cpp
    *("DGB", "DGM", "DGS", "TP13"),  # features/diagrams/hooks_mid.cpp
    *("TU8H", "TU8C", "TU8A", "TU8INIT", "TU8FREE", "AT"),  # features/tuner/hooks_entries.cpp (+ the TS8* entries)
    *("TS8SEL", "TS8STA", "TS8STB", "TS8M2E", "TS8MD2", "TS8MD0", "TS8MD1", "TS8MD3", "TS8FIN", "TS8FOU", "TS8PLS"),
    *("TS8PEK", "TS8LET", "TS8LPL", "TS8LRL", "TS8PKS", "TS8PKR", "TS8PKP", "TS8PKL", "TS8LOF", "TS8HNT"),
    *("PUBCL", "PUBTO", "PUBWU", "PUBNL", "PUBAN", "PUBAC"),  # features/publish/hooks_entries.cpp
    *("PUBGS", "PUBGE1", "PUBGE2", "PUBGC", "PUBGO", "PUBGB", "PUBGA", "PUBGF", "PUBEDGE"),  # hooks_gridsites.cpp
    *("PSU1", "PSU2", "PSM1", "LT1", "AL1"),  # features/headstock/hooks_mid.cpp
    *("TICK", "JUD1", "JUD2", "AUD1"),  # features/vguit/hooks_mid.cpp
    *("GSFR", "GSBD", "GSUL", "GSLD", "GSFO", "GSFI", "GSXF", "GSWD", "GSUP", "GSRS", "GSPK", "GSVB"),
    *("GSIC", "GSDM", "GSGL", "GSHC", "GRM1", "GRM2"),  # features/strings/hooks_entries.cpp
)
BATCH5_LIGHT = {
    *("CLBM", "CLBA", "CLGU", "CLGE", "CLVB", "NG1"),
    *("PUBCL", "PUBTO", "PUBWU", "PUBNL", "PUBAN", "PUBAC", "PUBGS", "PUBGE1", "PUBGE2", "PUBGC", "PUBGO", "PUBEDGE"),
}
MID_CALLBACKS.update({sid: f"Rs7_Mid_{sid}" for sid in BATCH5_MID})
LIGHT_CALLBACKS |= BATCH5_LIGHT
# The sse pass (test_midhook_sse_sites.py): W (full), R2, R6 and PUBGP (site R1) in sse mode.
MID_CALLBACKS.update({"W": "Rs7_Mid_W", "R2": "Rs7_Mid_R2", "R6": "Rs7_Mid_R6", "R1": "Rs7_Mid_PUBGP"})
# Sites registered with Rs7Hook::midMode = Rs7Mid::Mode::Sse; keep in sync with the registration tables. Built with
# Rs7Mid_BuildSse (mode 2); test_midhook_emu.py checks their callbacks for x87/MMX code (SSE is allowed).
SSE_CALLBACKS: set[str] = {"R2", "R6", "R1"}
assert not LIGHT_CALLBACKS & SSE_CALLBACKS


def mid_mode(sid: str) -> int:
    """The builders' mode argument of a site: 0 full, 1 light, 2 sse (Rs7Mid::Mode)."""
    return 2 if sid in SSE_CALLBACKS else int(sid in LIGHT_CALLBACKS)


JMP_BUILDERS = ("Rs7Mid_Build", "Rs7Mid_BuildLight", "Rs7Mid_BuildSse")  # by mode
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
    "X_WTHR": "g_rs7_wrong_const",
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
VOID_C_EXPORTS = ("Rs7_NeckLog", "Rs7_S7LogMask", "Rs7_HeadLog", "Rs7_DiagNoteHead")
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


TEXT_VA, TEXT_SIZE = 0x401000, 0xCB503C


@lru_cache(maxsize=1)
def snapshot():
    """The 700 MB memory snapshot, loaded once for all tests. Without it (CI, RS7_GAME_FILES), a skip."""
    if not (SNAPSHOT / "memory.bin").exists():
        pytest.skip(f"needs the memory snapshot {SNAPSHOT} (tools/game_files.py)", allow_module_level=True)

    from resolve_imports import Memory

    return Memory()


@lru_cache(maxsize=1)
def game_text() -> bytes:
    """Unpacked .text (file offset == RVA in Rocksmith2014.unpacked.exe). Without the exe, a skip: of the test, or of
    the whole module when it resolves sites while pytest imports it (the shared guard, emu_common.require_game_exe)."""
    require_game_exe()
    return EXE.read_bytes()[TEXT_VA - 0x400000 : TEXT_VA - 0x400000 + TEXT_SIZE]


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


def wrap_builder_args(exports, sid: str, va: int, callee: int | None, source: int, out: int, mode: int) -> tuple:
    """Rs7Mid_BuildWrap's arguments for Wrap site `sid` (CALL_WRAP) at `out`: a call site's callee, or (callee None) a
    function entry whose prologue bytes are at `source` (length from sites.json)."""
    nargs, pops, locals_, before, after = CALL_WRAP[sid]
    length = 5 if callee is not None else next(s for s in sitetable.load(SITES)[1] if s.id == sid).length
    callbacks = tuple(exports[name] if name else 0 for name in (before, after))
    return (
        out,
        256,
        out,
        callee or 0,
        0 if callee is not None else source,
        va,
        length,
        *callbacks,
        nargs,
        pops,
        locals_,
        int(mode),
    )


def build_mid_entry(uc, exports, site, va: int, target: int, out: int, esp: int, ret: int) -> int:
    """Build the mid-hook entry of `site` (MID_CALLBACKS) at `out` with the DLL's own emitter, as Rs7_Startup does
    (mode / call placement from mid_mode / CALL_AFTER); `esp` / `ret` are a free stack and a stop address."""
    callback = exports[MID_CALLBACKS[site.id]]
    if site.id in CALL_WRAP:
        callee = target if site.kind == "call" else None
        args = (ret, *wrap_builder_args(exports, site.id, va, callee, va, out, mid_mode(site.id)))
        builder = "Rs7Mid_BuildWrap"
    elif site.kind == "call":
        args = (ret, out, 256, va, target, out, callback, mid_mode(site.id), int(site.id in CALL_AFTER))
        builder = "Rs7Mid_BuildCall"
    else:
        # a jmp_atomic8 site replays its whole `expect` span (startup's PrepareMid), the 5-byte jmp ends inside it
        args = (ret, out, 256, va, va, len(site.expect), out, callback)
        builder = JMP_BUILDERS[mid_mode(site.id)]
    uc.mem_write(esp, struct.pack(f"<{len(args)}I", *args))
    uc.reg_write(UC_X86_REG_ESP, esp)
    uc.emu_start(exports[builder], ret, count=5000)
    assert uc.reg_read(UC_X86_REG_EIP) == ret and uc.reg_read(UC_X86_REG_EAX) > 0, f"mid-hook rejected {site.id}"
    return out


def _callins() -> dict[str, tuple[str, int, int]]:
    """Every call-in entry the DLL defines (`Rs7Mid::CallIn g_rs7_callin_<name>{&callback, Mode::..., argBytes}` in
    src/): name -> (callback, mode 0 full / 1 light / 2 sse, argBytes), read from the C++ so the tests cannot drift from it."""
    import re

    out = {}
    pattern = re.compile(r"Rs7Mid::CallIn g_rs7_callin_(\w+)\{&(\w+), Rs7Mid::Mode::(Full|Light|Sse)(?:, (\d+))?\};")
    for path in sorted((REPO / "src").rglob("*.cpp")):
        for name, callback, mode, arg_bytes in pattern.findall(path.read_text(encoding="utf-8")):
            assert name not in out, f"g_rs7_callin_{name} defined twice"
            out[name] = (callback, ("Full", "Light", "Sse").index(mode), int(arg_bytes or 0))
    return out


CALLINS = _callins()


def build_callin_entry(uc, exports, callback: str, mode: int, arg_bytes: int, out: int, esp: int, ret: int) -> int:
    """The call-in entry of `callback` at `out` with the DLL's Rs7Mid_BuildCallIn; `esp` / `ret`: a free stack and a
    stop address."""
    uc.mem_write(esp, struct.pack("<7I", ret, out, 0x40, out, exports[callback], mode, arg_bytes))
    uc.reg_write(UC_X86_REG_ESP, esp)
    uc.emu_start(exports["Rs7Mid_BuildCallIn"], ret, count=5000)
    assert uc.reg_read(UC_X86_REG_EIP) == ret and uc.reg_read(UC_X86_REG_EAX) == (18 if arg_bytes else 16), callback
    return out


def callin_entry(uc, exports, sym: str, out: int, esp: int, ret: int) -> int:
    """build_callin_entry for a call-in object (g_rs7_callin_<name>, CALLINS), as CallIn::Entry() builds it."""
    return build_callin_entry(uc, exports, *CALLINS[sym.removeprefix("g_rs7_callin_")], out, esp, ret)


def site_mid_entry(uc, exports, sid: str, out: int, esp: int, ret: int) -> int:
    """build_mid_entry for site `sid` of sites.json: what the game's jump at the site goes to (tests that enter a hook
    directly, in place of the naked stub they entered before the conversion). The site's bytes must be mapped."""
    _, sites = sitetable.load(SITES)
    site = next(x for x in sites if x.id == sid)
    ok, msg, va, target = sitecheck.check_site(game_text(), TEXT_VA, site)
    assert ok, f"{sid}: {msg}"
    return build_mid_entry(uc, exports, site, va, target, out, esp, ret)


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
        self.exports = load_dll(self.uc)
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
            if s.group == "fast":  # [test] fast=0 (default, features/testdrive/testdrive.h): unwritten
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
            if s.id.startswith("GK"):  # [neck] clite=0 (default, features/clite/clite.h): GK* unwritten
                continue
            if s.id.startswith("TV"):  # [test] force_* off (default, features/testdrive/testdrive.h): TV* unwritten
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
                stub = self._build_mid(s, va, target)
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

    def _build_mid(self, site, va, target):
        """Run the actual DLL emitter before patching the real game site."""
        out = CODE + 0x80000 + len(self.mid_entries) * 0x100
        return build_mid_entry(self.uc, self.exports, site, va, target, out, STACK + 0x70000, RET_MAGIC)

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

    def hook_entry(self, sid: str) -> int:
        """Where the game's patched jump / call of `sid` goes: its mid-hook entry or its naked stub."""
        return self.mid_entries.get(sid) or self.exports[f"Rs7_Stub_{sid}"]

    def run_to_callee(self, start: int, callee: int, regs: dict[int, int]):
        """Run until the hooked code continues into `callee`. A code hook stops there (Unicorn bakes an `until` address
        into blocks it translated earlier, and a callee usually was), with the callee's cached block dropped."""
        hook = self.uc.hook_add(UC_HOOK_CODE, lambda uc, address, size, user: uc.emu_stop(), begin=callee, end=callee)
        self.uc.ctl_remove_cache(callee, callee + 1)  # a block translated before the hook would not call it
        try:
            for r, v in regs.items():
                self.uc.reg_write(r, v)
            self.uc.emu_start(start, 0xFFFFFFF0, count=5000)
        finally:
            self.uc.hook_del(hook)
        assert self.uc.reg_read(UC_X86_REG_EIP) == callee, f"stopped at {self.uc.reg_read(UC_X86_REG_EIP):#x}"

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


# ---- helpers shared by the site tests (run_s7_site and friends) ------------------------------
# Stock songs never get a string object (S7): on those paths every stub must leave the registers,
# the stack and the flags it does not own exactly as stock.

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


def set_u32(emu: Emu, name: str, value: int):
    emu.uc.mem_write(emu.exports[name], struct.pack("<I", value))


@contextmanager
def entry_calls(emu: Emu, entry: str):
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


# ---- native N-slot chord data: the registered-song table g_rs7_ns --------------------------------

NS_BEGIN, NS_SUPER = 0x12340000, 0x22220000


def ns_set(emu: Emu, entries: list[tuple[int, ...]]):
    """g_rs7_ns: (tplBegin, tplCount, t8, cnBegin, cnCount, c8, song, origCn) per entry, rest free."""
    blob = b"".join(struct.pack("<8I", *e) for e in entries)
    emu.uc.mem_write(emu.exports["g_rs7_ns"], blob + b"\0" * (64 * 32 - len(blob)))
