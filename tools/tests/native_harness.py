"""Build and run the native C++ harnesses in tools/tests/native with MSVC (Win32); never launches the game."""

import functools
import os
import pathlib
import shutil
import subprocess
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def _find_vcvars() -> pathlib.Path:
    """vcvars32.bat of the newest Visual Studio with the C++ tools, found with vswhere as build.ps1 does."""
    root = pathlib.Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)"))
    vswhere = root / "Microsoft Visual Studio/Installer/vswhere.exe"
    if sys.platform == "win32" and vswhere.exists():
        found = subprocess.run(
            [str(vswhere), "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
             "-property", "installationPath"],
            capture_output=True, text=True, errors="replace", timeout=60,
        ).stdout.splitlines()  # fmt: skip
        if found:
            return pathlib.Path(found[0].strip()) / "VC/Auxiliary/Build/vcvars32.bat"
    return pathlib.Path("vcvars32.bat")  # not found: the native harness tests skip


VCVARS = _find_vcvars()
# Generous limits: they only catch a hang. vcvars and cl are slow when the machine is busy, and a tight limit
# made these tests fail under load.
COMPILE_TIMEOUT = 300
RUN_TIMEOUT = 60


@functools.cache
def _msvc_env() -> dict[str, str]:
    """The environment vcvars32.bat sets up, captured once per test session (vcvars is the slow part)."""
    result = subprocess.run(
        f'cmd /d /c call "{VCVARS}" >nul && set',  # one string: list form would escape the quotes for cmd
        capture_output=True,
        text=True,
        errors="replace",
        timeout=COMPILE_TIMEOUT,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    lines = (line.split("=", 1) for line in result.stdout.splitlines() if "=" in line and not line.startswith("="))
    env = {name.upper(): value for name, value in lines}  # cmd keeps the stored case ("Path"); Windows ignores it
    assert shutil.which("cl", path=env.get("PATH")), "vcvars32.bat did not put cl.exe on PATH"
    return env


def compile_harness(source, tmp_path):
    """Compile one harness source into tmp_path/lock.exe (skips without MSVC Win32)."""
    if sys.platform != "win32" or not VCVARS.exists():
        pytest.skip("MSVC Win32 is required for the native harnesses")
    env = _msvc_env()
    exe = tmp_path / "lock.exe"
    command = [
        shutil.which("cl", path=env["PATH"]),
        "/nologo", "/std:c++17", "/EHsc", "/MT", "/Od", "/DWIN32_LEAN_AND_MEAN", "/DNOMINMAX",
        f"/I{ROOT / 'src'}", str(source), f"/Fe:{exe}", f"/Fo:{tmp_path / 'lock.obj'}",
    ]  # fmt: skip
    result = subprocess.run(
        command, cwd=tmp_path, env=env, capture_output=True, text=True, errors="replace", timeout=COMPILE_TIMEOUT
    )
    assert result.returncode == 0, result.stdout + result.stderr
    return exe


def run_harness(exe, *args):
    """Run a compiled harness; returns the CompletedProcess (text output)."""
    return subprocess.run([str(exe), *args], capture_output=True, text=True, timeout=RUN_TIMEOUT)


def preprocess_sources(sources, tmp_path):
    """Expand the real registration macros and generated includes without compiling or linking game hooks."""
    if sys.platform != "win32" or not VCVARS.exists():
        pytest.skip("MSVC Win32 is required for the native harnesses")
    env = _msvc_env()
    command = [
        shutil.which("cl", path=env["PATH"]),
        "/nologo", "/EP", "/std:c++17", "/DWIN32_LEAN_AND_MEAN", "/DNOMINMAX",
        f"/I{ROOT / 'src'}", *(str(source) for source in sources),
    ]  # fmt: skip
    result = subprocess.run(
        command, cwd=tmp_path, env=env, capture_output=True, text=True, errors="replace", timeout=COMPILE_TIMEOUT
    )
    assert result.returncode == 0, result.stderr
    return result.stdout
