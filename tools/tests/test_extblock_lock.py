"""Run: uv run pytest tools/tests/test_extblock_lock.py -q (MSVC Win32)."""

import subprocess

from test_nslot_lock import ROOT, compile_harness


def test_extblock_allocation_failure_does_not_hold_writer_lock(tmp_path):
    exe = compile_harness(ROOT / "tools/tests/native/extblock_lock.cpp", tmp_path)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
