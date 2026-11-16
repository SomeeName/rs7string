"""tools/check_analyze.py: reading /analyze result files and the shrink-only baseline."""

import json

import check_analyze
import pytest

_XML = """<?xml version="1.0" encoding="UTF-8"?>
<DEFECTS>{}</DEFECTS>"""
_DEFECT = """<DEFECT><SFA><FILEPATH>{dir}\\</FILEPATH><FILENAME>{name}</FILENAME><LINE>{line}</LINE></SFA>
<DEFECTCODE>{code}</DEFECTCODE><FUNCTION>Fn</FUNCTION></DEFECT>"""


def _defect(rel, line, code):
    path = check_analyze.REPO / rel
    return _DEFECT.format(dir=path.parent, name=path.name, line=line, code=code)


def _results(tmp_path, tests_files: dict[str, str], dll=True):
    """A results folder: the tests project's files (name -> XML body) and, if dll, one clean DLL result."""
    tests = tmp_path / "rs7string_tests" / "tests"
    tests.mkdir(parents=True)
    for name, body in tests_files.items():
        (tests / f"{name}.nativecodeanalysis.xml").write_text(body, encoding="utf-8")

    if dll:
        (tmp_path / "rs7string").mkdir()
        (tmp_path / "rs7string" / "config.nativecodeanalysis.xml").write_text(_XML.format(""), encoding="utf-8")

    for project in check_analyze.PROJECTS:
        folder = tmp_path / project
        if folder.exists():
            reports = sorted(folder.rglob("*.nativecodeanalysis.xml"))
            (folder / "analysis-reports.txt").write_text("\n".join(str(p) for p in reports), encoding="utf-8")

    return tmp_path


def _entry(count, note="false positive"):
    return {"count": count, "note": note}


def test_findings_dedupe_headers_and_skip_third_party_and_stale(tmp_path):
    header = _defect("tests/d3d_fixture.h", 10, 6387)
    vendored = _defect("third_party/imgui/imgui.cpp", 5, 6011)
    files = {"test_patch": _XML.format(header + vendored), "test_gpu": _XML.format(header)}
    files["test_gone"] = _XML.format(header)

    assert check_analyze.findings(_results(tmp_path, files)) == {"tests/d3d_fixture.h::Fn::C6387": 1}


def test_missing_project_results_fail_instead_of_reporting_everything_fixed(tmp_path):
    results = _results(tmp_path, {"test_patch": _XML.format("")}, dll=False)

    with pytest.raises(check_analyze.AnalyzeError, match="no analysis results for rs7string;"):
        check_analyze.findings(results)


def test_malformed_xml_names_the_file(tmp_path):
    results = _results(tmp_path, {"test_patch": "<DEFECTS><DEFECT>"})

    with pytest.raises(check_analyze.AnalyzeError, match=r"test_patch\.nativecodeanalysis\.xml: not valid XML"):
        check_analyze.findings(results)


def test_defect_without_location_names_the_file(tmp_path):
    results = _results(tmp_path, {"test_patch": _XML.format("<DEFECT><DEFECTCODE>6011</DEFECTCODE></DEFECT>")})

    with pytest.raises(check_analyze.AnalyzeError, match=r"test_patch\.nativecodeanalysis\.xml: a DEFECT without"):
        check_analyze.findings(results)


def test_missing_translation_unit_report_fails(tmp_path):
    results = _results(tmp_path, {"test_patch": _XML.format(""), "test_gpu": _XML.format("")})
    (results / "rs7string_tests/tests/test_gpu.nativecodeanalysis.xml").unlink()
    with pytest.raises(check_analyze.AnalyzeError, match="missing analysis report.*test_gpu"):
        check_analyze.findings(results)


def test_missing_report_manifest_fails(tmp_path):
    results = _results(tmp_path, {"test_patch": _XML.format("")})
    (results / "rs7string/analysis-reports.txt").unlink()
    with pytest.raises(check_analyze.AnalyzeError, match="analysis-reports.txt"):
        check_analyze.findings(results)


def test_update_does_not_shrink_baseline_when_report_is_missing(tmp_path, monkeypatch):
    results = _results(tmp_path / "results", {"test_patch": _XML.format(""), "test_gpu": _XML.format("")})
    (results / "rs7string_tests/tests/test_gpu.nativecodeanalysis.xml").unlink()
    baseline = _baseline_file(tmp_path, monkeypatch, 3)
    original = baseline.read_bytes()
    monkeypatch.setattr(check_analyze, "RESULTS", results)
    monkeypatch.setattr("sys.argv", ["check_analyze", "--update"])
    assert check_analyze.main() == 1
    assert baseline.read_bytes() == original


def test_main_reports_missing_results_in_one_line(tmp_path, monkeypatch, capsys):
    monkeypatch.setattr(check_analyze, "RESULTS", tmp_path)
    monkeypatch.setattr("sys.argv", ["check_analyze.py"])

    assert check_analyze.main() == 1
    assert capsys.readouterr().out.strip().splitlines() == [
        "check_analyze: no analysis results for rs7string; run build.ps1 (it runs the analysis build)"
    ]


def test_baseline_only_shrinks_and_needs_notes():
    base = {"a.cpp::F::C6001": _entry(2), "gone.cpp::G::C6011": _entry(1), "b.cpp::B::C6385": _entry(1, "")}
    now = {"a.cpp::F::C6001": 3, "new.cpp::H::C6385": 1, "b.cpp::B::C6385": 1}
    errors = "\n".join(check_analyze.compare(now, base))

    assert "a.cpp::F::C6001: 3 finding(s), grew from 2" in errors
    assert "new.cpp::H::C6385: 1 new finding(s)" in errors
    assert "gone.cpp::G::C6011: fixed" in errors
    assert "b.cpp::B::C6385: baseline entry without a note" in errors
    assert check_analyze.compare({"a.cpp::F::C6001": 1}, {"a.cpp::F::C6001": _entry(2)}) == []


def test_write_baseline_refuses_growth_unless_allowed():
    base = {"a.cpp::F::C6001": _entry(2, "why a"), "gone.cpp::G::C6011": _entry(1)}

    kept, grown = check_analyze.rewrite({"a.cpp::F::C6001": 3, "new.cpp::H::C6385": 1}, base, allow_grow=False)
    assert kept == base
    assert grown == ["a.cpp::F::C6001", "new.cpp::H::C6385"]

    shrunk, grown = check_analyze.rewrite({"a.cpp::F::C6001": 1}, base, allow_grow=False)
    assert grown == []
    assert shrunk == {"a.cpp::F::C6001": _entry(1, "why a")}

    grew, grown = check_analyze.rewrite({"new.cpp::H::C6385": 1}, base, allow_grow=True)
    assert grown == []
    assert grew == {"new.cpp::H::C6385": _entry(1, "")}


def _baseline_file(tmp_path, monkeypatch, count):
    path = tmp_path / "baseline.json"
    path.write_text(json.dumps({"a::F::C1": {"count": count, "note": "x"}}), encoding="utf-8")
    monkeypatch.setattr(check_analyze, "BASELINE", path)
    return path


def test_slack_notes_counts_below_the_baseline():
    base = {"a::F::C1": {"count": 3, "note": "x"}, "b::F::C2": {"count": 1, "note": "x"}}
    now = {"a::F::C1": 2, "b::F::C2": 1}

    assert check_analyze.slack(now, base) == ["baseline can shrink: a::F::C1 3 -> 2"]
    assert check_analyze.compare(now, base) == []


def test_update_only_shrinks(tmp_path, monkeypatch):
    path = _baseline_file(tmp_path, monkeypatch, 3)
    monkeypatch.setattr(check_analyze, "findings", lambda: {"a::F::C1": 2})
    monkeypatch.setattr("sys.argv", ["check_analyze", "--update"])

    assert check_analyze.main() == 0
    assert json.loads(path.read_text(encoding="utf-8"))["a::F::C1"] == {"count": 2, "note": "x"}

    monkeypatch.setattr(check_analyze, "findings", lambda: {"a::F::C1": 5})
    assert check_analyze.main() == 1


def test_strict_fails_on_slack_default_passes(tmp_path, monkeypatch, capsys):
    _baseline_file(tmp_path, monkeypatch, 3)
    monkeypatch.setattr(check_analyze, "findings", lambda: {"a::F::C1": 2})

    monkeypatch.setattr("sys.argv", ["check_analyze"])
    assert check_analyze.main() == 0
    assert "baseline can shrink: a::F::C1 3 -> 2" in capsys.readouterr().out

    monkeypatch.setattr("sys.argv", ["check_analyze", "--strict"])
    assert check_analyze.main() == 1
