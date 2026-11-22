"""Plumbing shared by the Unicorn tests: repo paths, the built DLL image, the unpacked exe sections, CRT stubs."""

from __future__ import annotations

import struct
import re
from functools import lru_cache
from pathlib import Path

import pefile
import pytest
from unicorn import UC_PROT_ALL
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP

import game_files
import gen_fncopy

REPO = Path(__file__).resolve().parents[2]  # this checkout (a worktree too)
ROOT = game_files.ROOT  # original/, rskeys/ (tools/game_files.py; RSXR_GAME_FILES)
DLL = REPO / "bin" / "Release" / "dinput8.dll"
SITES = REPO / "sites.json"
EXE = gen_fncopy.EXE  # original/Rocksmith2014.unpacked.exe

needs_exe = pytest.mark.skipif(not EXE.exists(), reason="needs original/Rocksmith2014.unpacked.exe")


def require_game_exe() -> None:
    """Skip (the test, or the module while pytest imports it) when the user's unpacked game exe is absent, e.g. on CI
    or with RSXR_GAME_FILES pointing at an empty folder (tools/game_files.py)."""
    if not EXE.exists():
        pytest.skip(f"needs {EXE} (tools/game_files.py)", allow_module_level=True)


needs_exe_and_dll = pytest.mark.skipif(
    not EXE.exists() or not DLL.exists(), reason="needs original/Rocksmith2014.unpacked.exe and the built DLL"
)

CRT_MEMSET = 0xF5B0C0  # the CRT memset (VMProtect-wrapped in the game image)
SECURITY_CHECK_COOKIE = 0xF5AB08


def call_after() -> set[str]:
    """Read After placement from registration; reject an unrecognised spelling instead of silently using Before."""
    out = set()
    pattern = r'Rsxr_Mid\(\s*"(\w+)"[^)]*\)((?:\s*\.\w+\([^)]*\))*)'
    for path in sorted((REPO / "src" / "features").glob("*/registration.cpp")):
        source = re.sub(r"//[^\n]*|/\*.*?\*/", "", path.read_text(encoding="utf-8"), flags=re.S)
        rows = [sid for sid, modifiers in re.findall(pattern, source) if ".After()" in modifiers]
        assert len(rows) == source.count(".After()"), f"unrecognised After registration in {path}"
        out.update(rows)
    return out


def run_checked(uc, start: int, until: int, *, count: int = 50000):
    """A bounded run succeeds only when it reaches the requested return or stop address."""
    uc.emu_start(start, until, count=count)
    eip = uc.reg_read(UC_X86_REG_EIP)
    assert eip == until, f"stopped at {eip:#x}, expected {until:#x} (instruction limit {count})"


@lru_cache(maxsize=1)
def dll_image():
    """(base, mapped size, [(address, bytes)] of the emulator image, {export name: address}) of the built DLL."""
    pe = pefile.PE(str(DLL))
    base = pe.OPTIONAL_HEADER.ImageBase
    blobs = [(base, pe.header)] + [(base + s.VirtualAddress, s.get_data()) for s in pe.sections]
    exports = {e.name.decode(): base + e.address for e in pe.DIRECTORY_ENTRY_EXPORT.symbols if e.name}
    # Emulation runs one thread: uncontended SRW acquire/release calls need only their stdcall cleanup.
    # Keep the real spacing registry code running; its concurrency is covered by the native tests.
    srw_locks = {
        b"AcquireSRWLockExclusive",
        b"ReleaseSRWLockExclusive",
        b"AcquireSRWLockShared",
        b"ReleaseSRWLockShared",
    }
    for desc in pe.DIRECTORY_ENTRY_IMPORT:
        if desc.dll.lower() == b"kernel32.dll":
            for imp in desc.imports:
                if imp.name in srw_locks:
                    # Use this import's hint/name storage for a local `ret 4` stand-in and bind its IAT slot.
                    thunk = base + pe.get_dword_at_rva(imp.address - base)
                    blobs.extend([(thunk, b"\xc2\x04\x00"), (imp.address, struct.pack("<I", thunk))])
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
