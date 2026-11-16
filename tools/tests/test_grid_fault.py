from native_harness import ROOT, compile_harness, run_harness


def test_grid_build_fault_uses_shared_deadline(tmp_path):
    exe = compile_harness(ROOT / "tools/tests/native/grid_fault.cpp", tmp_path)
    result = run_harness(exe)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "PASS" in result.stdout, result.stdout


def test_grid_build_timeout_marks_a_seven_built_neck_failed(tmp_path):
    exe = compile_harness(ROOT / "tools/tests/native/neck_failed.cpp", tmp_path)
    result = run_harness(exe)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "PASS" in result.stdout, result.stdout
