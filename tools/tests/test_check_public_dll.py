"""tools/check_public_dll.py on synthetic bytes (no real DLL)."""

from __future__ import annotations

import check_public_dll

MARKERS = ("secret-marker", "other-marker")


def test_clean_bytes_pass():
    assert check_public_dll.find_markers(b"MZ\x00hello world\x00", MARKERS) == []


def test_ascii_marker_found():
    assert check_public_dll.find_markers(b"xx secret-marker yy", MARKERS) == ["secret-marker"]


def test_utf16_marker_found():
    data = b"\x00\x01" + "other-marker".encode("utf-16-le") + b"\x00"
    assert check_public_dll.find_markers(data, MARKERS) == ["other-marker"]


def test_main_fails_on_marker_and_passes_clean(tmp_path, monkeypatch, capsys):
    monkeypatch.setattr(check_public_dll, "load_markers", lambda: MARKERS)
    bad, good = tmp_path / "bad.dll", tmp_path / "good.dll"
    bad.write_bytes(b"..secret-marker..")
    good.write_bytes(b"..nothing..")
    assert check_public_dll.main(["x", str(bad)]) == 1
    assert "secret-marker" in capsys.readouterr().out
    assert check_public_dll.main(["x", str(good)]) == 0


def test_main_skips_without_marker_file(tmp_path, monkeypatch, capsys):
    monkeypatch.setattr(check_public_dll, "load_markers", lambda: None)
    assert check_public_dll.main(["x", str(tmp_path / "missing.dll")]) == 0
    assert "SKIPPED" in capsys.readouterr().out
