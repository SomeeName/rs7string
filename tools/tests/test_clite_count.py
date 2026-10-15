"""fix/clite3: static checks behind docs/ARCHITECTURE.md.

- Door (b): every return address in core/clite.h kRetModeEntries is a `call 0x5B6030` (Play_Tuning_SetPlayerTargetTuning)
  in the game, and every caller of 0x5B6030 is classified (song select, mode entry, or deliberately left out).
- Door (a): the byte shapes the audit's verdicts rest on (InstrumentInfo per-string flag fill 0x58AC20 stays inside the
  native 0x48-byte InstrumentInfo; the Session Mode harmony readers index [6][26]-sized rows with info+0Bh).
"""

from __future__ import annotations

import re
import struct
import sys
from functools import lru_cache
from pathlib import Path

import capstone
import pefile
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import gen_fncopy  # noqa: E402

SRC = Path(__file__).resolve().parents[2] / "src" / "core"
TARGET_TUNING = 0x5B6030
SONG_SELECT = 0x697A24
# Callers of 0x5B6030 that must NOT rebuild (they can run around a 7-string song): the tuner's in-session re-stores
# and the song options path. Return addresses.
LEFT_OUT = {
    0x7400B3: "tuner bass-emulation switch 0x73FFA0",
    0x742B24: "tuner event 0x742990",
    0x742C09: "tuner event 0x742990",
    0x69A7AF: "song options 0x69A2E0",
    0x74236F: "tuner cancel 0x73FC80 -> 0x742270 (fix/escrebuild: was misread as a Guitarcade tuner entry)",
}


# fix/menutuner: the tuning-list screen's stores rebuild to the picked tuning (core/clite.h kRetTunerMenu).
TUNER_MENU = {
    0x745E46: "TuningOptionsMenu select 0x745D90 (player 0)",
    0x745EC3: "TuningOptionsMenu select 0x745D90 (MP)",
}


@lru_cache(maxsize=1)
def _pe():
    if not gen_fncopy.EXE.exists():
        pytest.skip(f"{gen_fncopy.EXE} missing")
    return pefile.PE(str(gen_fncopy.EXE), fast_load=True)


def _bytes(va: int, n: int) -> bytes:
    return _pe().get_data(va - 0x400000, n)


def _mode_entries() -> list[int]:
    text = (SRC / "clite.h").read_text(encoding="utf-8")
    block = re.search(r"kRetModeEntries\[\]\s*=\s*\{(.*?)\};", text, re.S).group(1)
    block = re.sub(r"//[^\n]*", "", block)
    return [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]+", block)]


def _call_target(ret: int) -> int | None:
    b = _bytes(ret - 5, 5)
    if b[0] != 0xE8:
        return None
    return (ret + struct.unpack("<i", b[1:])[0]) & 0xFFFFFFFF


def _tuner_menu() -> list[int]:
    text = (SRC / "clite.h").read_text(encoding="utf-8")
    block = re.search(r"kRetTunerMenu\[\]\s*=\s*\{(.*?)\};", text, re.S).group(1)
    return [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]+", block)]


def test_tuner_menu_stores_are_the_tuning_options_menu():
    # fix/menutuner: both stores sit in 0x745D90, the TuningOptionsMenu select handler (vtable 0x11C2418 slot 0xD8,
    # installed by the factory 0x5D48F0 that 0x5D198E registers under "TuningOptionsMenu"), before it opens "Tuner".
    rets = _tuner_menu()
    assert sorted(rets) == sorted(TUNER_MENU)
    for r in rets:
        assert _call_target(r) == TARGET_TUNING, hex(r)
        assert 0x745D90 <= r < 0x746035
    assert not set(rets) & (set(_mode_entries()) | set(LEFT_OUT) | {SONG_SELECT})
    assert struct.unpack("<I", _bytes(0x11C2418 + 0xD8, 4))[0] == 0x745D90
    assert struct.unpack("<I", _bytes(0x5D4912, 4))[0] == 0x11C2418  # factory installs the vtable
    assert _bytes(0x5D198E, 5) == bytes.fromhex("68 F0 48 5D 00")  # push factory 0x5D48F0
    assert _bytes(0x5D19A2, 5) == bytes.fromhex("68 EC A0 1B 01")  # push "TuningOptionsMenu"
    assert _bytes(0x11BA0EC, 18) == b"TuningOptionsMenu" + bytes(1)


def test_mode_entries_are_target_tuning_calls():
    entries = _mode_entries()
    assert len(entries) == 8
    for r in entries:
        assert _call_target(r) == TARGET_TUNING, hex(r)
    assert SONG_SELECT not in entries
    assert not set(entries) & set(LEFT_OUT)


def test_every_target_tuning_caller_is_classified():
    pe = _pe()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    sec = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
    data = sec.get_data()[: sec.Misc_VirtualSize]
    base = 0x400000 + sec.VirtualAddress
    rets = set()
    for m in re.finditer(rb"\xE8", data):
        off = m.start()
        if off + 5 > len(data):
            break
        va = base + off
        if (va + 5 + struct.unpack("<i", data[off + 1 : off + 5])[0]) & 0xFFFFFFFF != TARGET_TUNING:
            continue
        ins = next(md.disasm(data[off : off + 5], va), None)  # a real call, not bytes inside another instruction
        if ins is not None and ins.mnemonic == "call":
            rets.add(va + 5)
    known = set(_mode_entries()) | set(LEFT_OUT) | set(_tuner_menu()) | {SONG_SELECT}
    assert rets, "no call to 0x5B6030 found"
    assert rets <= known, sorted(hex(r) for r in rets - known)


def test_0x742270_is_only_the_tuner_cancel_handler():
    # fix/escrebuild: 0x742270 (store at 0x74236A, return 0x74236F) restores the profile's tuning; its only caller is
    # 0x73FF4F in 0x73FC80, the tuner's cancel handler (input event 5Bh in 0x73ED40, UI_TunerMenu_Tick 0x73E5B5).
    assert _call_target(0x74236F) == TARGET_TUNING
    assert _call_target(0x73FF54) == 0x742270
    assert _call_target(0x73EDBC) == 0x73FC80  # 0x73ED40 case 5Bh
    assert _call_target(0x73E5BA) == 0x73FC80  # UI_TunerMenu_Tick
    assert 0x74236F not in _mode_entries()
    pe = _pe()
    sec = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
    data = sec.get_data()[: sec.Misc_VirtualSize]
    base = 0x400000 + sec.VirtualAddress
    callers = [
        base + m.start()
        for m in re.finditer(bytes([0xE8]), data)
        if m.start() + 5 <= len(data)
        and (base + m.start() + 5 + struct.unpack("<i", data[m.start() + 1 : m.start() + 5])[0]) & 0xFFFFFFFF
        == 0x742270
    ]
    assert callers == [0x73FF4F], [hex(c) for c in callers]


def test_tuner_manager_shape():
    # The live-tuner guard (clite.h kTunerMgrGlobal / kTunerMgrSlots / kTunerGuitar).
    assert _bytes(0x5B57ED, 6) == bytes.fromhex("89 3D D4 F5 35 01")  # ~manager: mov [0x135F5D4],edi (0)
    assert _bytes(0x5B5869, 7) == bytes.fromhex("C7 45 F8 04 00 00 00")  # UI_TunerManager_Update: 4 tuners
    assert _bytes(0x5B5870, 2) == bytes.fromhex("8B 1E")  # ebx = [esi] (esi walks mgr+10h)
    assert _bytes(0x5B5862, 4) == bytes.fromhex("C6 41 20 01")  # mgr+20h: the update flag (after the slots)
    assert _bytes(0x5B0663, 3) == bytes.fromhex("8B 4E 2C")  # 0x5B0660: ecx = tuner+2Ch (Rnd_Guitar*)
    h = (SRC / "clite.h").read_text(encoding="utf-8")
    assert "kTunerMgrGlobal = 0x135F5D4" in h and "kTunerMgrSlots = 0x10" in h and "kTunerGuitar = 0x2C" in h


def test_instrumentinfo_flag_fill_stays_inside_the_native_info():
    # 0x58AC20: for i < info+0Bh: mov [ecx+esi*4+20h], 0/1 - slot 6 at +38h, slot 7 at +3Ch.
    assert _bytes(0x58AC26, 3) == bytes.fromhex("8A 51 0B")  # mov dl,[ecx+0Bh]
    assert _bytes(0x58AC85, 4) == bytes.fromhex("89 54 B1 20")  # mov [ecx+esi*4+20h],edx
    assert _bytes(0x58AC8B, 3) == bytes.fromhex("3A 41 0B")  # cmp al,[ecx+0Bh]
    h = (SRC / "gsnative.h").read_text(encoding="utf-8")
    size = int(re.search(r" kInfoSize = (0x[0-9A-F]+)", h).group(1), 16)
    active = int(re.search(r"kInfoActive = (0x[0-9A-F]+)", h).group(1), 16)
    stock = int(re.search(r"kStockInfoSize = (0x[0-9A-F]+)", h).group(1), 16)
    assert stock == 0x38  # stock: slot 6 would be past the object
    assert 0x20 + 4 * 8 <= active <= size  # native: slots 6/7 end before the rs7 tail (+40h)


def test_session_harmony_rows_are_six_strings_wide():
    # 0x4431F0: row = fret*1CCh + string*4Ch over the [26] x [6] cells (6 * 4Ch = 1C8h <= 1CCh), string = info+0Bh - 1.
    assert _bytes(0x443207, 3) == bytes.fromhex("8A 49 0B")  # mov cl,[ecx+0Bh]
    assert _bytes(0x443224, 6) == bytes.fromhex("69 C0 CC 01 00 00")  # imul eax,eax,1CCh
    assert _bytes(0x44322A, 3) == bytes.fromhex("6B D2 4C")  # imul edx,edx,4Ch
    # 0x443470: the string loop runs to info+0Bh over GuitarStrings main[s] (+0Ch -> [s*4+4]).
    assert _bytes(0x44347C, 3) == bytes.fromhex("38 50 0B")
    assert _bytes(0x4434B9, 3) == bytes.fromhex("3A 50 0B")


def test_gkm_frame_holds_the_player_for_the_profile_loader_call():
    """Review F4: GKM (0x595ECB) reads the player from [EBP+0Ch] of Rnd_Guitar_RebuildIfProfileChanged 0x595EA0, which the
    function itself loads into EDI after the loader call (0x595EE0 `mov edi,[ebp+0Ch]`); the loader's vtable call
    (0x595ED0..0x595ED7) comes after GKM and before that load, and EBP is the standard frame."""
    assert _bytes(0x595EA0, 3) == b"\x55\x8b\xec"  # push ebp; mov ebp,esp
    assert _bytes(0x595EE0, 3) == b"\x8b\x7d\x0c"  # mov edi,[ebp+0Ch]
    assert _bytes(0x595ECB, 5) == b"\x8b\x4b\x0c\x8b\x01"  # GKM: mov ecx,[ebx+0Ch]; mov eax,[ecx]
    assert _bytes(0x595ED7, 1) == b"\xff" and _bytes(0x595ED8, 1) == b"\xd0"  # call eax (loader vtable[1])
