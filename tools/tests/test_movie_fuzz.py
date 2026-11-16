"""Truncated, boundary-value and fixed-seed random movies / R7DA recipes against the real diagram movie swap."""

from native_harness import ROOT, compile_harness, run_harness


def test_movie_swap_malformed_inputs_fail_closed_without_overreads(tmp_path):
    exe = compile_harness(ROOT / "tools/tests/native/movie_fuzz.cpp", tmp_path)
    result = run_harness(exe)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "PASS" in result.stdout
