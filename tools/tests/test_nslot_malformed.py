"""Fuzz-derived vector counts and inaccessible chart records, without game state."""

from native_harness import ROOT, compile_harness, run_harness


def test_nslot_expanded_vector_byte_counts_do_not_wrap(tmp_path):
    exe = compile_harness(ROOT / "tools/tests/native/nslot_malformed.cpp", tmp_path)
    result = run_harness(exe)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "PASS" in result.stdout, result.stdout


def test_nslot_unreadable_chart_record_does_not_publish_partial_array(tmp_path):
    exe = compile_harness(ROOT / "tools/tests/native/nslot_malformed.cpp", tmp_path)
    result = run_harness(exe, "truncated-record")
    assert result.returncode == 0, result.stdout + result.stderr
    assert "PASS" in result.stdout, result.stdout
