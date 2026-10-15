"""Fuzz-derived vector counts and inaccessible chart records, without game state."""

import subprocess
from test_nslot_lock import ROOT, compile_harness


def test_nslot_expanded_vector_byte_counts_do_not_wrap(tmp_path):
    exe = compile_harness(ROOT / "tools/tests/native/nslot_malformed.cpp", tmp_path)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr


def test_nslot_unreadable_chart_record_does_not_publish_partial_array(tmp_path):
    exe = compile_harness(ROOT / "tools/tests/native/nslot_malformed.cpp", tmp_path)
    result = subprocess.run([str(exe), "truncated-record"], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
