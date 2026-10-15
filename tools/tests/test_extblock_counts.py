"""Malformed RS7X counts exercised against the real Win32 parser."""

import subprocess
from test_nslot_lock import ROOT, compile_harness


def test_extblock_rejects_wrapping_counts_before_allocation(tmp_path):
    exe = compile_harness(ROOT / "tools/tests/native/extblock_counts.cpp", tmp_path)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
