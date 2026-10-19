"""Shared pytest setup: the tools/ import path and the emulator fixtures (see emu_harness.py)."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import game_files
from emu_harness import Emu, set_s7
from nextgrid_harness import Machine


def _game_files_status() -> list[str]:
    found = lambda p: "found" if p.exists() else "MISSING (its tests skip)"
    source = "RS7_GAME_FILES" if game_files.FROM_ENV else "default"
    return [
        f"rs7 game files: {game_files.ROOT} ({source})",
        f"  unpacked exe: {found(game_files.UNPACKED_EXE)}; snapshot: {found(game_files.SNAPSHOT / 'memory.bin')}",
    ]


def pytest_report_header(config):
    """Where the game files are looked up and what was found: a leftover RS7_GAME_FILES cannot skip tests silently."""
    return _game_files_status()


def pytest_terminal_summary(terminalreporter):
    """The same lines at the end, which -q (no header) still prints."""
    for line in _game_files_status():
        terminalreporter.write_line(line)


@pytest.fixture(scope="module")
def emus():
    """(stock, patched) emulators of the game snapshot; module scope, each module builds its own pair."""
    return Emu(patched=False), Emu(patched=True)


@pytest.fixture(params=[1, 0], ids=["diag", "nodiag"])
def diag(request, emus):
    """rs7string.ini [debug] diagnostics: 1 = R2/R1 call the D1/D4 loggers, 0 = they do not."""
    _, patched = emus
    patched.set_diag(request.param)
    yield request.param
    patched.set_diag(1)


@pytest.fixture(params=[(1, 0), (1, 1), (0, 1)], ids=["on-mode-off", "on-mode-on", "switch-off"])
def s7_state(request, emus):
    """(g_rs7_s7_on, g_rs7_any7): switch on without / with 7-string mode, and the switch off."""
    _, patched = emus
    set_s7(patched, *request.param)
    yield request.param
    set_s7(patched, 1, 0)


@pytest.fixture(scope="module")
def machines():
    """{"stock": Machine, "native": Machine} of test_nextgrid*.py."""
    return {m: Machine(m) for m in ("stock", "native")}
