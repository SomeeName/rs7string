"""tools/check_analyze.py: reading /analyze result files and the shrink-only baseline."""

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
