"""Decrypt-or-copy entry tested with protected memory and real stub source."""

from native_harness import ROOT, compile_harness, run_harness


def _compile_with_stub(name, tmp_path):
    source = (ROOT / "src/features/psarc/hooks.cpp").read_text()
    start = source.index("using RsxrSngDecrypt =")
    (tmp_path / "sngstub.inc").write_text(source[start:])
    harness = tmp_path / name
    harness.write_text((ROOT / "tools/tests/native" / name).read_text())
    return compile_harness(harness, tmp_path)


def test_plain_sng_rejects_short_input_before_reading_header(tmp_path):
    exe = _compile_with_stub("plainpsarc_short.cpp", tmp_path)
    result = run_harness(exe)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "PASS" in result.stdout, result.stdout


def test_plain_sng_copy_is_exact_at_both_buffer_ends(tmp_path):
    exe = _compile_with_stub("plainpsarc_bounds.cpp", tmp_path)
    result = run_harness(exe)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "PASS" in result.stdout
