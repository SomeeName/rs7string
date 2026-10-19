"""Fault injection against the exact disk reader; operates only on a temporary file."""

import subprocess
from test_nslot_lock import ROOT, compile_harness


def test_asset_reader_closes_handle_after_allocation_failure(tmp_path):
    source = (ROOT / "src/features/headstock/assetload_tex.cpp").read_text()
    start = source.index("bool ReadFileBytes(")
    end = source.index("void* CallDdsHandler(", start)
    (tmp_path / "reader.inc").write_text(source[start:end])
    harness = tmp_path / "asset_reader_failure.cpp"
    harness.write_text((ROOT / "tools/tests/native/asset_reader_failure.cpp").read_text())
    exe = compile_harness(harness, tmp_path)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
