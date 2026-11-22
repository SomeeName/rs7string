"""RsxrStartup_LogSites' per-site reason, run on the real source sliced out of src/startup_sites.cpp.

startup_sites.cpp is not in the unit-test project (it pulls in every feature); the harness compiles only its
Skipped / DeferredFg helpers and the LogSites body, with Writes and RsxrLog stubbed.
Catches: a [debug] skip_sites entry logged as "patched" or "deferred", the skip text for a site whose switch is
off anyway, a wrong off-reason precedence, and Skipped() matching an id that is only a prefix of a listed one.
"""

from native_harness import ROOT, compile_harness, run_harness


def _slice(source, start, end=None):
    i = source.index(start)
    return source[i : source.index(end, i)] if end else source[i:]


def test_log_sites_reasons_and_skip_matching(tmp_path):
    source = (ROOT / "src/startup_sites.cpp").read_text()
    (tmp_path / "logsites_helpers.inc").write_text(_slice(source, "// [debug] skip_sites lists", "}  // namespace"))
    (tmp_path / "logsites_body.inc").write_text(_slice(source, "void RsxrStartup_LogSites("))
    harness = tmp_path / "startup_logsites.cpp"
    harness.write_text((ROOT / "tools/tests/native/startup_logsites.cpp").read_text())
    exe = compile_harness(harness, tmp_path)
    result = run_harness(exe)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "PASS" in result.stdout, result.stdout
