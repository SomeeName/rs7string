"""Manifest tuning edits select arrangements and preserve unrelated text."""

import json

import manifest_key
import psarc_plain
import pytest


def entry(arrangement, tuning=None):
    return {"Attributes": {"Tuning": tuning or {f"string{i}": 0 for i in range(6)}, "ArrangementName": arrangement}}


@pytest.mark.parametrize("newline", ["\n", "\r\n"])
def test_arr_selector_applies_to_manifests_and_hsan(tmp_path, newline):
    entries = {name: entry(name) for name in ("Lead", "Rhythm", "Bass", "Vocals")}
    text = json.dumps({"Entries": entries}, indent=2).replace("\n", newline)
    names = ["manifests/songs/song.hsan", "manifests/songs/song_lead.json", "manifests/songs/song_rhythm.json"]
    files = {
        names[0]: text.encode(),
        names[1]: json.dumps({"Entries": {"Lead": entries["Lead"]}}).encode(),
        names[2]: json.dumps({"Entries": {"Rhythm": entries["Rhythm"]}}).encode(),
    }
    source, dest = tmp_path / "in.psarc", tmp_path / "out.psarc"
    psarc_plain.write(source, files, names)
    manifest_key.main([str(source), str(dest), "--arr", "lead", "--t6", "-2"])
    result, _ = psarc_plain.read(dest)
    hsan = json.loads(result[names[0]])["Entries"]
    assert hsan["Lead"]["Attributes"]["Tuning"]["Rs7String6"] == -2
    for name in ("Rhythm", "Bass", "Vocals"):
        assert hsan[name] == entries[name]
    assert result[names[2]] == files[names[2]]
    assert json.loads(result[names[1]])["Entries"]["Lead"]["Attributes"]["Tuning"]["Rs7String6"] == -2
    # Removing only the inserted line restores the original byte layout.
    added = f',{newline}          "Rs7String6": -2'
    assert result[names[0]].decode().replace(added, "") == text


def test_partially_updated_hsan_keeps_existing_offsets_and_fills_missing():
    lead = entry("Lead", {**{f"string{i}": 0 for i in range(6)}, "Rs7String6": -5})
    original = {
        "Entries": {"lead": lead, "rhythm": entry("Rhythm"), "bass": entry("Bass"), "vocals": entry("Vocals")},
        "other": {"string5": 12},
    }
    text = json.dumps(original, indent=2)
    new, count = manifest_key.add_key(text, -2)
    result = json.loads(new)
    assert count == 1
    assert result["Entries"]["lead"] == lead
    assert result["Entries"]["rhythm"]["Attributes"]["Tuning"]["Rs7String6"] == -2
    assert result["Entries"]["bass"] == original["Entries"]["bass"]
    assert result["Entries"]["vocals"] == original["Entries"]["vocals"]
    assert result["other"] == original["other"]
    assert manifest_key.add_key(new, 9) == (new, 0)


def test_key_elsewhere_does_not_skip_tuning():
    text = json.dumps({"Rs7String6": "metadata", **entry("Lead")})
    new, count = manifest_key.add_key(text, 3)
    assert count == 1
    assert json.loads(new)["Attributes"]["Tuning"]["Rs7String6"] == 3
