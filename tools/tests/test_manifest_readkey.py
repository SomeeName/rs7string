"""Exercise the exact ReadKey function with synthetic Win32 JSON vtables."""

import subprocess
from test_nslot_lock import ROOT, compile_harness


def test_manifest_numeric_reader_boundaries_and_bad_nodes(tmp_path):
    source = (ROOT / "src/features/tuner/hooks.cpp").read_text()
    start = source.index("using JsonFind =")
    end = source.index("// ---- end of the manifest key reader", start)
    (tmp_path / "readkey.inc").write_text(source[start:end])
    harness = tmp_path / "manifest_readkey.cpp"
    harness.write_text((ROOT / "tools/tests/native/manifest_readkey.cpp").read_text())
    exe = compile_harness(harness, tmp_path)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
