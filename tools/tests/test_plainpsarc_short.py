"""Decrypt-or-copy entry tested with protected memory and real stub source."""

import subprocess
from test_nslot_lock import ROOT, compile_harness


def test_plain_sng_rejects_short_input_before_reading_header(tmp_path):
    source = (ROOT / "src/hooks/stubs_psarc.cpp").read_text()
    start = source.index("using Rs7SngDecrypt =")
    (tmp_path / "sngstub.inc").write_text(source[start:])
    harness = tmp_path / "plainpsarc_short.cpp"
    harness.write_text((ROOT / "tools/tests/native/plainpsarc_short.cpp").read_text())
    exe = compile_harness(harness, tmp_path)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
