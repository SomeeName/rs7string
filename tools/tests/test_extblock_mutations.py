"""Deterministic truncation/bit-mutation coverage of the real RS7X parser."""

import subprocess
from test_nslot_lock import ROOT, compile_harness


def test_extblock_mutations_truncations_and_null_input(tmp_path):
    exe = compile_harness(ROOT / "tools/tests/native/extblock_mutations.cpp", tmp_path)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
