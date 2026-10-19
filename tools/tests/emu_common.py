"""Plumbing shared by the Unicorn tests: repo paths, the built DLL image, the unpacked exe sections, CRT stubs."""

from __future__ import annotations

import struct
from functools import lru_cache
from pathlib import Path

import pefile
import pytest
from unicorn import UC_PROT_ALL
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP

import game_files
import gen_fncopy

REPO = Path(__file__).resolve().parents[2]  # this checkout (a worktree too)
ROOT = game_files.ROOT  # original/, rskeys/ (tools/game_files.py; RS7_GAME_FILES)
DLL = REPO / "bin" / "Release" / "dinput8.dll"
SITES = REPO / "sites.json"
EXE = gen_fncopy.EXE  # original/Rocksmith2014.unpacked.exe

needs_exe = pytest.mark.skipif(not EXE.exists(), reason="needs original/Rocksmith2014.unpacked.exe")


def require_game_exe() -> None:
    """Skip (the test, or the module while pytest imports it) when the user's unpacked game exe is absent, e.g. on CI
    or with RS7_GAME_FILES pointing at an empty folder (tools/game_files.py)."""
    if not EXE.exists():
        pytest.skip(f"needs {EXE} (tools/game_files.py)", allow_module_level=True)


needs_dll = pytest.mark.skipif(not DLL.exists(), reason="needs the built DLL")
needs_exe_and_dll = pytest.mark.skipif(
    not EXE.exists() or not DLL.exists(), reason="needs original/Rocksmith2014.unpacked.exe and the built DLL"
)

CRT_MEMSET = 0xF5B0C0  # the CRT memset (VMProtect-wrapped in the game image)
SECURITY_CHECK_COOKIE = 0xF5AB08


@lru_cache(maxsize=1)
def dll_image():
    """(base, mapped size, [(address, bytes)] of header + sections, {export name: address}) of the built DLL."""
    pe = pefile.PE(str(DLL))
    base = pe.OPTIONAL_HEADER.ImageBase
    blobs = [(base, pe.header)] + [(base + s.VirtualAddress, s.get_data()) for s in pe.sections]
    exports = {e.name.decode(): base + e.address for e in pe.DIRECTORY_ENTRY_EXPORT.symbols if e.name}
    return base, (pe.OPTIONAL_HEADER.SizeOfImage + 0xFFF) & ~0xFFF, blobs, exports


def load_dll(uc) -> dict[str, int]:
    """Map the built DLL at its image base into `uc`; returns its exports (name -> address)."""
    base, size, blobs, exports = dll_image()
    uc.mem_map(base, size, UC_PROT_ALL)
    for address, data in blobs:
        uc.mem_write(address, data)
    return dict(exports)


@lru_cache(maxsize=1)
def exe_sections():
    """[(address, bytes)] of .text, .rdata and .data of the unpacked game exe."""
    pe = pefile.PE(str(EXE), fast_load=True)
    return [
        (pe.OPTIONAL_HEADER.ImageBase + s.VirtualAddress, bytes(s.get_data()[: s.Misc_VirtualSize]))
        for s in pe.sections
        if s.Name.rstrip(b"\0") in (b".text", b".rdata", b".data")
    ]


def crt_memset(uc, address, size, data):
    """UC_HOOK_CODE callback for CRT_MEMSET: cdecl memset(dst, val, n) done natively, then return."""
    sp = uc.reg_read(UC_X86_REG_ESP)
    ret, dst, val, n = struct.unpack("<4I", bytes(uc.mem_read(sp, 16)))
    uc.mem_write(dst, bytes([val & 0xFF]) * n)
    uc.reg_write(UC_X86_REG_EAX, dst)
    uc.reg_write(UC_X86_REG_ESP, sp + 4)
    uc.reg_write(UC_X86_REG_EIP, ret)


def stub_security_cookie(uc):
    """__security_check_cookie replaced with `ret`."""
    uc.mem_write(SECURITY_CHECK_COOKIE, b"\xc3")
