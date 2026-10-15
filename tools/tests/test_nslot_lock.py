"""Native fault injection against the real nslot source; never launches the game."""

import pathlib
import subprocess
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
VCVARS = pathlib.Path(r"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars32.bat")


def compile_harness(source, tmp_path):
    if sys.platform != "win32" or not VCVARS.exists():
        pytest.skip("MSVC Win32 is required for the native SRW lock regression")
    exe = tmp_path / "lock.exe"
    command = f'call "{VCVARS}" >nul && cl /nologo /std:c++17 /EHsc /MT /Od /DWIN32_LEAN_AND_MEAN /DNOMINMAX /I"{ROOT / "src"}" "{source}" /Fe:"{exe}" /Fo:"{tmp_path / "lock.obj"}"'
    script = tmp_path / "build.cmd"
    script.write_text("@echo off\n" + command + "\n")
    result = subprocess.run(
        ["cmd", "/d", "/c", str(script)], capture_output=True, text=True, errors="replace", timeout=60
    )
    assert result.returncode == 0, result.stdout + result.stderr
    return exe


def test_nslot_allocation_failure_does_not_hold_writer_lock(tmp_path):
    exe = compile_harness(ROOT / "tools/tests/native/nslot_lock.cpp", tmp_path)
    failures = []
    for args in ([], ["postprocess"]):
        result = subprocess.run([str(exe), *args], capture_output=True, text=True, timeout=10)
        if result.returncode:
            failures.append(f"{args}: {result.stdout}{result.stderr}")
    assert not failures, "\n".join(failures)
