import subprocess
from test_nslot_lock import ROOT, compile_harness


def test_grid_build_fault_uses_shared_deadline(tmp_path):
    exe = compile_harness(ROOT / "tools/tests/native/grid_fault.cpp", tmp_path)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr


def test_grid_build_timeout_marks_a_seven_built_neck_failed(tmp_path):
    exe = compile_harness(ROOT / "tools/tests/native/neck_failed.cpp", tmp_path)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
