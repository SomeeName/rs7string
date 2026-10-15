"""tools/check_exports.py on the built DLL (exports, image base, no d3d9.dll import)."""

from __future__ import annotations

from pathlib import Path

import pytest

import check_exports

DLL = Path(__file__).resolve().parents[2] / "bin" / "Release" / "dinput8.dll"


@pytest.mark.skipif(not DLL.exists(), reason="needs the built DLL")
def test_built_dll_passes():
    assert check_exports.problems(DLL) == []


@pytest.mark.skipif(not DLL.exists(), reason="needs the built DLL")
def test_built_dll_imports_only_expected_windows_libraries():
    import pefile

    # user32: the [window] options (src/core/window.cpp); the game has it loaded anyway.
    # bcrypt: verify owner movie/tag hashes; no bundled crypto or encryption keys.
    assert check_exports.imports(pefile.PE(str(DLL))) == {"kernel32.dll", "user32.dll", "bcrypt.dll"}


def test_d3d9_is_forbidden():
    assert "d3d9.dll" in check_exports.FORBIDDEN_IMPORTS
    assert {"g_rs7_v6", "g_rs7_colormgr"} <= set(check_exports.REQUIRED_DATA)
