"""The player converter (tools/rsxr_convert.py): which packages it converts, where the old ones go, what it leaves."""

import psarc_plain
import pytest
import rsxr_convert as rc
import test_native_chart as tn


def package(path, lead_strings=7, flags=0, **change):
    six = {f"string{i}": 0 for i in range(6)}
    files = {
        "songs/bin/generic/a_lead.sng": tn.wrap(tn.fixture(0, lead_strings)),
        "songs/bin/generic/a_bass.sng": tn.wrap(tn.fixture(1, 4)),
        "songs/bin/generic/a_vocals.sng": tn.wrap(b"vocals"),
    }
    files.update(tn.package_manifests(six if lead_strings == 6 else None, six))
    files.update(change)
    psarc_plain.write(path, files, list(files), flags=flags)
    return path.read_bytes()


def run(*arguments):
    return rc.main([*map(str, arguments), "--no-pause"])


def test_folder_is_converted_and_old_files_are_set_aside(tmp_path, capsys):
    dlc = tmp_path / "dlc"
    (dlc / "sub").mkdir(parents=True)
    old = package(dlc / "sub" / "song_p.psarc")
    six = package(dlc / "six_p.psarc", lead_strings=6)
    official = package(dlc / "official_p.psarc", flags=4)
    (dlc / "mac_m.psarc").write_bytes(b"mac")

    assert run(dlc) == 0
    text = capsys.readouterr().out
    assert "1 converted, 2 left alone, 0 failed." in text
    assert "mac_m" not in text

    new = dlc / "sub" / "song_rsxr_p.psarc"
    files, meta = psarc_plain.read(new)
    assert meta["magic"] == b"PSXR"
    assert tn.nc.unpack_sng(files["songs/bin/generic/a_lead.sng"]).startswith(b"SNXR")
    assert not (dlc / "sub" / "song_p.psarc").exists()
    assert (dlc / "sub" / "old-format" / "song_p.disabled.psarc").read_bytes() == old
    assert (dlc / "six_p.psarc").read_bytes() == six and (dlc / "official_p.psarc").read_bytes() == official
    assert sorted(p.name for p in dlc.rglob("*") if p.is_file()) == [
        "mac_m.psarc",
        "official_p.psarc",
        "six_p.psarc",
        "song_p.disabled.psarc",
        "song_rsxr_p.psarc",
    ]

    assert run(dlc) == 0  # a second run finds nothing to do and does not look into old-format
    assert "0 converted, 3 left alone, 0 failed." in capsys.readouterr().out
    assert (dlc / "sub" / "old-format" / "song_p.disabled.psarc").read_bytes() == old


def test_single_files_and_a_taken_old_name(tmp_path, capsys):
    package(tmp_path / "song_p.psarc")
    (tmp_path / "old-format").mkdir()
    (tmp_path / "old-format" / "song_p.disabled.psarc").write_bytes(b"earlier")
    (tmp_path / "notes.txt").write_bytes(b"x")

    assert run(tmp_path / "song_p.psarc", tmp_path / "notes.txt", tmp_path / "song_p.psarc") == 0
    text = capsys.readouterr().out
    assert "1 converted, 1 left alone, 0 failed." in text  # a file named twice is converted once
    assert (tmp_path / "old-format" / "song_p.disabled.psarc").read_bytes() == b"earlier"
    assert (tmp_path / "old-format" / "song (2)_p.disabled.psarc").exists()
    assert run(tmp_path / "song_rsxr_p.psarc") == 0
    assert "already in the new format" in capsys.readouterr().out


@pytest.mark.parametrize("subfolder", ["", "sub", "old-format", "old-format/sub"])
def test_folder_leaves_set_aside_files_alone(tmp_path, capsys, subfolder):
    folder = tmp_path / subfolder
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / "song_p.disabled.psarc"
    old = package(path)

    assert run(tmp_path) == 0
    text = capsys.readouterr().out
    assert "0 converted, 0 left alone, 0 failed." in text
    assert path.name not in text
    assert path.read_bytes() == old
    assert [p for p in tmp_path.rglob("*") if p.is_file()] == [path]


def test_direct_set_aside_file_is_reported_as_left_alone(tmp_path, capsys):
    path = tmp_path / "song_p.disabled.psarc"
    path.write_bytes(b"set aside")

    assert run(path) == 0
    text = capsys.readouterr().out
    assert "0 converted, 1 left alone, 0 failed." in text
    assert "set aside earlier (old format)" in text
    assert path.read_bytes() == b"set aside"


@pytest.mark.parametrize("scan_old_folder", [False, True])
def test_folder_repairs_earlier_names_once(tmp_path, capsys, scan_old_folder):
    folder = tmp_path / "sub" / "old-format"
    folder.mkdir(parents=True)
    (folder / "song_p.psarc.old").write_bytes(b"old song")
    (folder / "other_p.psarc.old").write_bytes(b"other song")
    (folder / "song_p.disabled.psarc").write_bytes(b"earlier")
    (folder / "song (2)_p.disabled.psarc").write_bytes(b"earlier too")
    (folder / "untouched_p.psarc").write_bytes(b"leave in old-format")
    (tmp_path / "outside_p.psarc.old").write_bytes(b"leave outside old-format")
    scan = folder if scan_old_folder else tmp_path

    assert run(scan, scan) == 0
    text = capsys.readouterr().out
    assert "0 converted, 0 left alone, 0 failed." in text
    assert "2 old files renamed so the game no longer loads them" in text
    assert not (folder / "song_p.psarc.old").exists()
    assert not (folder / "other_p.psarc.old").exists()
    assert (folder / "song (3)_p.disabled.psarc").read_bytes() == b"old song"
    assert (folder / "other_p.disabled.psarc").read_bytes() == b"other song"
    assert (folder / "song_p.disabled.psarc").read_bytes() == b"earlier"
    assert (folder / "song (2)_p.disabled.psarc").read_bytes() == b"earlier too"
    assert (folder / "untouched_p.psarc").read_bytes() == b"leave in old-format"
    assert (tmp_path / "outside_p.psarc.old").read_bytes() == b"leave outside old-format"
    before = {p.relative_to(tmp_path): p.read_bytes() for p in tmp_path.rglob("*") if p.is_file()}

    assert run(scan) == 0
    text = capsys.readouterr().out
    assert "0 converted, 0 left alone, 0 failed." in text
    assert "old files renamed" not in text
    assert {p.relative_to(tmp_path): p.read_bytes() for p in tmp_path.rglob("*") if p.is_file()} == before


def test_existing_target_is_kept(tmp_path, capsys):
    old = package(tmp_path / "song_p.psarc")
    mine = package(tmp_path / "song_rsxr_p.psarc", lead_strings=6)

    assert run(tmp_path) == 0
    assert "song_rsxr_p.psarc is already there" in capsys.readouterr().out
    assert (tmp_path / "song_rsxr_p.psarc").read_bytes() == mine
    assert (tmp_path / "song_p.psarc").read_bytes() == old


@pytest.mark.parametrize("broken", ["chart", "file", "missing"])
def test_failures_leave_everything_in_place(tmp_path, capsys, broken):
    path = tmp_path / "song_p.psarc"
    if broken == "chart":
        old = package(path, **{"songs/bin/generic/a_lead.sng": tn.wrap(b"SNXR")})
    elif broken == "file":
        path.write_bytes(old := b"not a package at all, but long enough for a header")

    assert run(path) == 1
    assert "0 converted, 0 left alone, 1 failed." in capsys.readouterr().out
    if broken != "missing":
        assert path.read_bytes() == old
        assert [p.name for p in tmp_path.iterdir()] == ["song_p.psarc"]


def test_without_arguments_it_explains_itself(capsys):
    assert rc.main(["--no-pause"]) == 1
    assert "drag files or a folder" in capsys.readouterr().out
