"""Tone IDs survive native conversion; F is an event, not the game's no-event sentinel."""

import json
from copy import deepcopy

import native_chart as nc
import pytest
import psarc_plain
import tone_demo
from test_native_chart import core


def test_native_tone_timeline_round_trip():
    body, _ = core(0, 7, native=True)
    native = b"SNXR\x01\x00\x07\x00" + body + bytes(256)
    changes = [(float(i + 1), i % 6) for i in range(20)]
    native = nc.with_tone_changes(native, changes)
    assert nc.read_tone_changes(native) == changes
    stock = nc.to_stock(native)
    assert nc.read_tone_changes(stock) == changes
    assert nc.read_tone_changes(nc.to_native(stock, 0)) == changes


@pytest.mark.parametrize("changes", [[(1.0, 26)], [(1.0, -1)], [(2.0, 0), (1.0, 4)], [(float("nan"), 0)]])
def test_reject_invalid_tone_events(changes):
    body, _ = core(0, 7, native=True)
    with pytest.raises(ValueError):
        nc.with_tone_changes(b"SNXR\x01\x00\x07\x00" + body + bytes(256), changes)


def test_extra_names_and_definitions_survive_manifest_conversion():
    attrs = {
        "ArrangementName": "Lead",
        "ExtendedRange": {"Version": 1},
        "Tuning": {f"string{i}": 0 for i in range(7)},
        "Tone_Base": "clean",
        **{f"Tone_{chr(65 + i)}": f"tone{i}" for i in range(26)},
        "Tones": [{"Key": f"tone{i}"} for i in range(26)],
    }
    raw = json.dumps({"Entries": {"test": {"Attributes": attrs}}}).encode()
    back = nc.manifest_to_native(nc.manifest_to_stock(raw))
    actual = json.loads(back)["Entries"]["test"]["Attributes"]
    assert nc.tone_names(actual) == [f"tone{i}" for i in range(26)]
    assert actual["Tones"] == attrs["Tones"]


def test_demo_writes_six_distinct_names_and_twenty_events(tmp_path):
    body, _ = core(0, 7, native=True)
    keys = [f"original{i}" for i in range(6)]
    attrs = {
        "ArrangementName": "Lead",
        "ExtendedRange": {"Version": 1},
        "Tuning": {f"string{i}": 0 for i in range(7)},
        "SongAsset": "urn:application:musicgame-song:demo_lead",
        "Tones": [{"Key": key, "OriginalTestData": i} for i, key in enumerate(keys)],
    }
    manifest = json.dumps({"Entries": {"TEST_ID": {"Attributes": attrs}}}).encode()
    files = {
        "manifests/songs/demo_lead.json": manifest,
        "manifests/songs/demo.hsan": manifest,
        "songs/bin/generic/demo_lead.sng": nc.pack_sng(b"SNXR\x01\x00\x07\x00" + body + bytes(256)),
    }
    source, destination = tmp_path / "source.psarc", tmp_path / "demo_rsxr_p.psarc"
    psarc_plain.write(source, files, list(files), magic=b"PSXR")
    original = source.read_bytes()
    tone_demo.generate(source, destination, "test_id", keys)
    actual, _ = psarc_plain.read(destination)
    result = json.loads(actual["manifests/songs/demo_lead.json"])["Entries"]["TEST_ID"]["Attributes"]
    assert nc.tone_names(result) == keys + [""] * 20
    assert result["Tone_Base"] == keys[0]
    assert result["Tones"] == attrs["Tones"]
    assert nc.read_tone_changes(nc.unpack_sng(actual["songs/bin/generic/demo_lead.sng"])) == [
        (5.0 + 3 * i, i % 6) for i in range(20)
    ]
    assert source.read_bytes() == original


def variant_package(tmp_path, tones):
    """Synthetic gear names and control keys; no definitions from a game package."""
    body, _ = core(0, 7, native=True)
    attrs = {
        "ArrangementName": "Lead",
        "ExtendedRange": {"Version": 1},
        "Tuning": {f"string{i}": 0 for i in range(7)},
        "SongAsset": "urn:application:musicgame-song:demo_lead",
        "Tones": tones,
    }
    header = deepcopy(attrs)
    del header["Tones"]
    files = {
        "manifests/songs/demo_lead.json": json.dumps({"Entries": {"TEST_ID": {"Attributes": attrs}}}).encode(),
        "manifests/songs/demo.hsan": json.dumps({"Entries": {"TEST_ID": {"Attributes": header}}}).encode(),
        "songs/bin/generic/demo_lead.sng": nc.pack_sng(b"SNXR\x01\x00\x07\x00" + body + bytes(256)),
        "audio/synthetic.wem": b"synthetic audio placeholder",
    }
    source = tmp_path / "source.psarc"
    psarc_plain.write(source, files, list(files), magic=b"PSXR")
    return source, files


@pytest.fixture
def variant_tone():
    return {
        "Key": "source",
        "Name": "Synthetic source",
        "NameSeparator": " - ",
        "Volume": "-20.000",
        "IsCustom": True,
        "ToneDescriptors": ["synthetic"],
        "GearList": {
            "Amp": {
                "Key": "SyntheticAmp",
                "Type": "Amps",
                "KnobValues": {
                    "SyntheticAmp_Gain": 32,
                    "SyntheticAmp_Bass": 45,
                    "SyntheticAmp_Treble": 67,
                    "SyntheticAmp_Mid": 54,
                },
            },
            "Cabinet": {"Key": "SyntheticCab", "Type": "Cabinets", "KnobValues": {}},
        },
    }


@pytest.mark.parametrize("count", [6, 26])
@pytest.mark.parametrize("drive_control", ["Gain", "Loudness1"])
def test_demo_variants_fill_slots_cycle_and_preserve_native_package(
    tmp_path, variant_tone, capsys, count, drive_control
):
    source_knobs = variant_tone["GearList"]["Amp"]["KnobValues"]
    source_knobs[f"SyntheticAmp_{drive_control}"] = source_knobs.pop("SyntheticAmp_Gain")
    source, original_files = variant_package(tmp_path, [variant_tone])
    original = source.read_bytes()
    destination = tmp_path / "demo_rsxr_p.psarc"
    tone_demo.generate(source, destination, "test_id", variants=count, start=2.0, interval=1.5)
    files, meta = psarc_plain.read(destination)
    assert meta["magic"] == b"PSXR"
    assert nc._check_native_package(files)
    attrs = json.loads(files["manifests/songs/demo_lead.json"])["Entries"]["TEST_ID"]["Attributes"]
    header = json.loads(files["manifests/songs/demo.hsan"])["Entries"]["TEST_ID"]["Attributes"]
    keys = [f"RsxrDemo{chr(65 + i)}" for i in range(count)]
    assert nc.tone_names(attrs) == nc.tone_names(header) == keys + [""] * (26 - count)
    assert attrs["Tone_Base"] == header["Tone_Base"] == keys[0]
    definitions = {tone["Key"]: tone for tone in attrs["Tones"]}
    assert len(definitions) == len(attrs["Tones"]) == count + 1
    assert definitions["source"] == variant_tone
    changes = nc.read_tone_changes(nc.unpack_sng(files["songs/bin/generic/demo_lead.sng"]))
    assert changes == [(2.0 + i * 1.5, i % count) for i in range(count + 1)]
    assert [ident for _, ident in changes[:-1]] == list(range(count))
    settings = set()
    table = capsys.readouterr().out
    for i, key in enumerate(keys):
        expected = deepcopy(variant_tone)
        expected.update(Key=key, Name=f"Demo {chr(65 + i)}")
        knobs = expected["GearList"]["Amp"]["KnobValues"]
        knobs[f"SyntheticAmp_{drive_control}"] = (10, 50, 90)[i % 3]
        knobs["SyntheticAmp_Bass"] = (0, 50, 100)[(i // 3) % 3]
        knobs["SyntheticAmp_Treble"] = (0, 50, 100)[i // 9]
        assert definitions[key] == expected
        settings.add(tuple(knobs.values()))
        assert f"{chr(65 + i)} -> {key} ->" in table
        for name in (f"SyntheticAmp_{drive_control}", "SyntheticAmp_Bass", "SyntheticAmp_Treble"):
            assert f"{name}={knobs[name]}" in table
    assert len(settings) == count
    assert files["audio/synthetic.wem"] == original_files["audio/synthetic.wem"]
    assert source.read_bytes() == original


@pytest.mark.parametrize("variants", [5, 27, 6.5])
def test_demo_rejects_invalid_variant_count(tmp_path, variant_tone, variants):
    source, _ = variant_package(tmp_path, [variant_tone])
    destination = tmp_path / "demo_rsxr_p.psarc"
    with pytest.raises(ValueError, match="6.*26"):
        tone_demo.generate(source, destination, "test_id", variants=variants)
    assert not destination.exists()


@pytest.mark.parametrize("control", ["Gain", "Bass", "Treble"])
def test_demo_rejects_variants_without_suitable_existing_controls(tmp_path, variant_tone, control):
    del variant_tone["GearList"]["Amp"]["KnobValues"][f"SyntheticAmp_{control}"]
    source, _ = variant_package(tmp_path, [variant_tone])
    destination = tmp_path / "demo_rsxr_p.psarc"
    with pytest.raises(ValueError, match="amp.*controls"):
        tone_demo.generate(source, destination, "test_id", variants=26)
    assert not destination.exists()


def test_demo_rejects_variant_key_collision(tmp_path, variant_tone):
    variant_tone["Key"] = "rsxrdemoa"
    source, _ = variant_package(tmp_path, [variant_tone])
    with pytest.raises(ValueError, match="already exists"):
        tone_demo.generate(source, tmp_path / "demo_rsxr_p.psarc", "test_id", variants=26)


def test_demo_rejects_keys_and_variants_together(tmp_path):
    with pytest.raises(ValueError, match="either"):
        tone_demo.generate(tmp_path / "source.psarc", tmp_path / "demo.psarc", "test_id", ["x"] * 6, variants=26)


def test_demo_variant_cli(tmp_path, variant_tone):
    source, _ = variant_package(tmp_path, [variant_tone])
    destination = tmp_path / "demo_rsxr_p.psarc"
    tone_demo.main(
        [
            str(source),
            str(destination),
            "--arrangement",
            "test_id",
            "--variants",
            "26",
            "--start",
            "0",
            "--interval",
            "2",
        ]
    )
    files, _ = psarc_plain.read(destination)
    assert nc.read_tone_changes(nc.unpack_sng(files["songs/bin/generic/demo_lead.sng"])) == [
        (float(2 * i), i % 26) for i in range(27)
    ]


@pytest.mark.parametrize(
    "options", [["--variants", "5"], ["--variants", "27"], ["--variants", "26", "--keys", *"abcdef"]]
)
def test_demo_cli_rejects_invalid_modes(options):
    with pytest.raises(SystemExit) as error:
        tone_demo.main(["source.psarc", "demo.psarc", "--arrangement", "test_id", *options])
    assert error.value.code == 2
