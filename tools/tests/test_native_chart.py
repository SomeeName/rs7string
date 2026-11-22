"""Synthetic, independently assembled SNXR layouts and optional local package round trips."""

import json
import os
import struct
import zlib
from pathlib import Path

import native_chart as nc
import psarc_plain
import pytest


def pack(fmt, *args):
    return struct.pack("<" + fmt, *args)


def array(records):
    return pack("I", len(records)) + b"".join(records)


SLOTS = {
    (0, 6): (0, 1, 2, 3, 4, 5),
    (0, 7): (6, 0, 1, 2, 3, 4, 5),
    (0, 8): (7, 6, 0, 1, 2, 3, 4, 5),
    (1, 4): (0, 1, 2, 3),
    (1, 5): (4, 0, 1, 2, 3),
    (1, 6): (4, 0, 1, 2, 3, 5),
}
MIDI = ((40, 45, 50, 55, 59, 64, 35, 30), (28, 33, 38, 43, 23, 48))
TUNING = (-2, -1, 0, 1, 2, 3, -3, -4)


def technique(slot, record):
    bends = pack("ffI", 1.25 + record, 0.5 + slot, 0) + pack("ffI", 2.5 + record, 1.0 + slot, 0)
    return pack("IbbhB", 0x401800 + slot, slot + 2, slot + 3, 120 + slot, 2) + bends


def chord_notes(slots, record, active):
    masks, bends, slides, unpitched, vibratos = [], [], [], [], []
    for slot in slots:
        if slot in active:
            tech = technique(slot, record)
            masks.append(tech[:4])
            bends.append(tech[9:] + bytes(360) + pack("I", 2))
            slides.append(tech[4:5])
            unpitched.append(tech[5:6])
            vibratos.append(tech[6:8])
        else:
            masks.append(bytes(4))
            bends.append(bytes(388))
            slides.append(b"\xff")
            unpitched.append(b"\xff")
            vibratos.append(bytes(2))
    return b"".join(masks + bends + slides + unpitched + vibratos)


def template(slots, record, instrument, active):
    frets = [slot + record + 1 if slot in active else -1 for slot in slots]
    fingers = [(slot + record) % 5 if slot in active else -1 for slot in slots]
    notes = [MIDI[instrument][slot] + TUNING[slot] + fret if fret >= 0 else -1 for slot, fret in zip(slots, frets)]
    return (
        pack("I", 0x12340000 + record)
        + pack(f"{len(slots)}b", *frets)
        + pack(f"{len(slots)}b", *fingers)
        + pack(f"{len(slots)}i", *notes)
        + f"chord{record}".encode().ljust(32, b"\0")
    )


def core(instrument, strings, native=False):
    """Assemble all stock sections, including the conditional symbol tables and variable records."""
    active = SLOTS[instrument, strings]
    columns = active if native else tuple(range(6))
    tuning_slots = active if native else tuple(range(strings if instrument == 0 else 6))
    tuning = [TUNING[s] if s in active else 0 for s in tuning_slots]
    pieces, offsets = [], {}

    def add(name, data):
        offsets[name] = sum(map(len, pieces))
        pieces.append(data)

    add("beats", array([pack("fhhII", 1.0, 2, 3, 4, 5)]))
    add("phrases", array([bytes(range(44))]))
    add("templates", array([template(columns, r, instrument, active) for r in range(2)]))
    add("chord_notes", array([chord_notes(columns, r, active) for r in range(2)]))
    add("vocals", array([bytes(60)]))
    for name, size in (
        ("symbol_headers", 32),
        ("symbol_textures", 144),
        ("symbols", 44),
        ("iterations", 24),
        ("phrase_extra", 16),
    ):
        add(name, array([bytes((i % 251 for i in range(size)))]))
    add("linked", array([pack("I", 3) + array([pack("I", 5), pack("I", 8)])]))
    for name, size in (("actions", 260), ("events", 260), ("tones", 8), ("dna", 8)):
        add(name, array([bytes((i % 251 for i in range(size)))]))
    masks = [sum(1 << (active.index(s) if native else s) for s in active if (s + d) % 3 != 0) for d in range(36)]
    add("sections", array([b"section".ljust(32, b"\0") + pack("IffII", 2, 1.0, 10.0, 0, 0) + bytes(masks)]))
    levels = []
    for difficulty in range(2):
        notes = []
        for s in (*active, -1):
            prefix = bytearray((i % 127 for i in range(63)))
            prefix[16] = (active.index(s) if native else s) if s >= 0 else 255
            notes.append(bytes(prefix) + array([pack("ffI", 2.0, 0.5, 0)]))
        levels.append(
            pack("I", difficulty)
            + array([bytes(28)])
            + array([bytes(12)])
            + array([bytes(20)])
            + array([bytes(20)])
            + array(notes)
            + array([pack("f", 2.5)])
            + array([pack("I", 7)])
            + array([pack("I", 9)])
        )
    add("levels", array(levels))
    add(
        "metadata",
        bytes(range(79)) + pack("I", len(tuning)) + pack(f"{len(tuning)}h", *tuning) + pack("ffI", 2.5, 2.5, 1),
    )
    return b"".join(pieces), offsets


def section(ident, payload, flags=0):
    return pack("4sHHI", ident, 1, flags, len(payload)) + payload


def extension(instrument, strings):
    if strings == (6 if instrument == 0 else 4):
        return b""
    extra = tuple(range(6, strings)) if instrument == 0 else ()
    parts = [section(b"STRS", bytes((instrument, strings, len(extra), 0, *extra)), 1)]
    if extra:
        parts.append(
            section(
                b"CHDT", pack("I", 2) + b"".join(pack("bb", s + r + 1, (s + r) % 5) for r in range(2) for s in extra), 1
            )
        )
        parts.append(section(b"CHNT", pack("I", 2) + b"".join(technique(s, r) for r in range(2) for s in extra)))
    payload = b"".join(parts)
    return pack("4sHHII", b"RS7X", 1, 16, 16 + len(payload), len(parts)) + payload


def fixture(instrument, strings):
    stock, _ = core(instrument, strings)
    return stock + extension(instrument, strings)


def ext_sections(data):
    if not data or not any(data):
        return {}
    magic, version, pos, size, count = struct.unpack_from("<4sHHII", data)
    assert (magic, version, size) == (b"RS7X", 1, len(data))
    result = {}
    for _ in range(count):
        ident, version, flags, length = struct.unpack_from("<4sHHI", data, pos)
        result[ident] = (version, flags, data[pos + 12 : pos + 12 + length])
        pos += 12 + length
    assert pos == len(data)
    return result


@pytest.mark.parametrize("instrument,strings", SLOTS)
def test_layout_and_round_trip(instrument, strings):
    original = fixture(instrument, strings)
    native = (
        nc.to_native(original, 1 - instrument, 8)
        if extension(instrument, strings)
        else nc.to_native(original, instrument)
    )
    slots = SLOTS[instrument, strings]
    assert struct.unpack_from("<4sBBBB", native) == (b"SNXR", 1, instrument, strings, 0)
    expected, offsets = core(instrument, strings, True)
    # Includes each N-wide template/technique field, every note, masks, and tuning in low-to-high order.
    assert native[8 : 8 + len(expected)] == expected
    padding = native[8 + len(expected) :]
    minimum = 256 + max(0, 6 - strings) * (6 * 2 + 396 * 2 + 2)
    assert len(padding) >= minimum and not any(padding)
    converted = nc.to_stock(native)
    stock, _ = core(instrument, strings)
    assert len(converted) == len(native)
    assert converted[: len(stock)] == stock
    sections = ext_sections(converted[len(stock) :])
    before = ext_sections(original[len(stock) :])
    assert {k: v for k, v in sections.items() if k != b"PAD0"} == before
    if before:
        assert list(sections)[-1] == b"PAD0"
        assert sections[b"PAD0"][:2] == (1, 0)
        assert not any(sections[b"PAD0"][2])
    assert nc.to_native(converted, instrument) == native


@pytest.mark.parametrize(
    "offset,value,message",
    [
        (0, b"FAIL", "magic"),
        (4, b"\x02", "version"),
        (5, b"\x02", "instrument"),
        (6, b"\x09", "string"),
        (7, b"\x01", "reserved"),
    ],
)
def test_bad_header(offset, value, message):
    native = bytearray(nc.to_native(fixture(0, 7), 0))
    native[offset : offset + len(value)] = value
    with pytest.raises(ValueError, match=message):
        nc.to_stock(bytes(native))


@pytest.mark.parametrize("instrument,strings", SLOTS)
def test_truncation_and_missing_padding(instrument, strings):
    stock, _ = core(instrument, strings)
    native = nc.to_native(fixture(instrument, strings), instrument)
    native_core, _ = core(instrument, strings, True)
    end = 8 + len(native_core)
    for length in sorted(set(range(0, end, 29)) | set(range(0, 24)) | {end - 1}):
        with pytest.raises(ValueError):
            nc.to_stock(native[:length])
    for length in sorted(set(range(0, len(stock), 31)) | {len(stock) - 1}):
        with pytest.raises(ValueError):
            nc.to_native(stock[:length], instrument)
    required = len(stock) + (len(extension(instrument, strings)) + 12 if extension(instrument, strings) else 0)
    if required > end:
        with pytest.raises(ValueError, match=r"missing \d+ byte"):
            nc.to_stock(native[:end])
        with pytest.raises(ValueError, match="missing 1 byte"):
            nc.to_stock(native[: required - 1])
    else:
        assert len(nc.to_stock(native[:end])) == end
    damaged = bytearray(native)
    damaged[-1] = 1
    with pytest.raises(ValueError, match="padding"):
        nc.to_stock(bytes(damaged))


def test_no_block_defaults_and_strings_argument():
    stock, _ = core(0, 6)
    with pytest.raises(ValueError, match="string"):
        nc.to_native(stock, 0, 7)
    with pytest.raises(ValueError, match="instrument"):
        nc.to_native(stock, 2)
    stock, _ = core(1, 4)
    assert nc.to_native(stock, 1, 4)[6] == 4


def wrap(body, flags=1):
    return pack("II", 0x4A, flags) + bytes(16) + pack("I", len(body)) + zlib.compress(body) + bytes(56)


def package_manifests(lead=None, bass=None):
    """The manifests of the test package: a seven-string lead, a five-string bass, vocals."""
    six = {f"string{i}": 0 for i in range(6)}
    tunings = {
        "lead": {**six, "Rs7String6": -2} if lead is None else lead,
        "bass": {**six, "string4": -2, "Rs7String4": -2} if bass is None else bass,
        "vocals": None,
    }
    result = {}
    # Names deliberately disagree: classification must use the linked manifest.
    for suffix, tuning in tunings.items():
        attributes = {"ArrangementName": suffix.capitalize(), "SongAsset": f"urn:application:musicgame-song:a_{suffix}"}
        if tuning is not None:
            attributes["Tuning"] = tuning
        result[f"manifests/songs/a_{suffix}.json"] = json.dumps(
            {"Entries": {suffix: {"Attributes": attributes}}}
        ).encode()
    return result


@pytest.mark.parametrize(
    "change",
    [
        {"lead": {f"string{i}": 0 for i in range(6)}},  # the chart has seven strings, the manifest six
        {"bass": {f"string{i}": 0 for i in range(6)}},
        {"manifests/songs/a_lead.json": None},  # no Tuning at all
        {"songs/bin/generic/a_bass.sng": b"SNXR"},  # says native, is nothing
    ],
)
def test_package_parts_must_agree(tmp_path, change):
    files = {
        "songs/bin/generic/a_lead.sng": wrap(fixture(0, 7)),
        "songs/bin/generic/a_bass.sng": wrap(fixture(1, 5)),
        "songs/bin/generic/a_vocals.sng": wrap(b"vocals"),
    }
    files.update(package_manifests(change.get("lead"), change.get("bass")))
    for name, value in change.items():
        if name == "manifests/songs/a_lead.json":
            attributes = {"ArrangementName": "Lead", "SongAsset": "urn:application:musicgame-song:a_lead"}
            files[name] = json.dumps({"Entries": {"lead": {"Attributes": attributes}}}).encode()
        elif "/" in name:
            files[name] = wrap(value)
    source = tmp_path / "source.psarc"
    psarc_plain.write(source, files, list(files))
    with pytest.raises(SystemExit):
        nc.main(["to-native", str(source), str(tmp_path / "native.psarc")])
    assert not (tmp_path / "native.psarc").exists()


@pytest.mark.parametrize("lead_strings,magic", [(7, b"PSXR"), (6, b"PSAR")])
def test_package_is_native_per_arrangement(tmp_path, lead_strings, magic):
    """A four-string bass next to a seven-string lead stays stock; a package without a native chart stays PSAR."""
    six = {f"string{i}": 0 for i in range(6)}
    files = {
        "songs/bin/generic/a_lead.sng": wrap(fixture(0, lead_strings)),
        "songs/bin/generic/a_bass.sng": wrap(fixture(1, 4)),
        "songs/bin/generic/a_vocals.sng": wrap(b"vocals"),
    }
    files.update(package_manifests(six if lead_strings == 6 else None, six))
    source, native, back = (tmp_path / f"{s}.psarc" for s in ("source", "native", "back"))
    psarc_plain.write(source, files, list(files))
    nc.main(["to-native", str(source), str(native)])
    actual, meta = psarc_plain.read(native)
    assert meta["magic"] == magic
    for name, value in files.items():
        if lead_strings == 7 and name.endswith("a_lead.sng"):
            assert nc.unpack_sng(actual[name]).startswith(b"SNXR")
        elif lead_strings == 7 and name.endswith("a_lead.json"):
            assert b"ExtendedRange" in actual[name]
        else:
            assert actual[name] == value
    nc.main(["to-stock", str(native), str(back)])
    actual, meta = psarc_plain.read(back)
    assert meta["magic"] == b"PSAR"
    assert actual["songs/bin/generic/a_bass.sng"] == files["songs/bin/generic/a_bass.sng"]


def test_package_and_body_cli(tmp_path):
    files = {
        "songs/bin/generic/a_lead.sng": wrap(fixture(0, 7)),
        "songs/bin/generic/a_bass.sng": wrap(fixture(1, 5)),
        "songs/bin/generic/a_vocals.sng": wrap(b"vocals"),
        "songs/arr/a_showlights.xml": b"showlights",
        "other": bytes(range(256)),
    }
    files.update(package_manifests())
    source, native, converted = (tmp_path / f"{s}.psarc" for s in ("source", "native", "converted"))
    psarc_plain.write(source, files, list(files))
    nc.main(["to-native", str(source), str(native)])
    actual, meta = psarc_plain.read(native)
    assert meta["names"] == list(files) and meta["magic"] == b"PSXR"
    for name, value in files.items():
        if name.endswith(("a_lead.sng", "a_bass.sng")):
            assert nc.unpack_sng(actual[name]).startswith(b"SNXR")
        elif name.endswith(("a_lead.json", "a_bass.json")):
            assert b"ExtendedRange" in actual[name]
        else:
            assert actual[name] == value
    again = tmp_path / "again.psarc"
    nc.main(["to-native", str(native), str(again)])  # already native: checked and kept
    assert psarc_plain.read(again)[0] == actual
    nc.main(["to-stock", str(native), str(converted)])
    actual, meta = psarc_plain.read(converted)
    assert meta["magic"] == b"PSAR"
    for name, value in files.items():
        if name.startswith("manifests/"):  # re-serialised: equal as JSON
            assert json.loads(actual[name]) == json.loads(value)
    for suffix, instrument, strings in (("lead", 0, 7), ("bass", 1, 5)):
        stock, _ = core(instrument, strings)
        assert nc.unpack_sng(actual[f"songs/bin/generic/a_{suffix}.sng"])[: len(stock)] == stock
    inp, out, back = (tmp_path / f"{s}.bin" for s in ("in", "out", "back"))
    inp.write_bytes(fixture(0, 7))
    nc.main(["body-to-native", str(inp), str(out), "--instrument", "0"])
    nc.main(["body-to-stock", str(out), str(back)])
    assert back.read_bytes() == nc.to_stock(out.read_bytes())


@pytest.mark.parametrize(
    "data,message",
    [
        (b"", "truncated"),
        (wrap(b"x", 3), "encrypted"),
        (b"BAD!" + wrap(b"x")[4:], "magic"),
        (wrap(b"abc")[:-57], "truncated"),
        (wrap(b"abc")[:24] + pack("I", 10) + wrap(b"abc")[28:], "size"),
    ],
)
def test_wrapper_errors(data, message):
    with pytest.raises(ValueError, match=message):
        nc.unpack_sng(data)


def test_encrypted_package_refused(tmp_path, capsys):
    source, dest = tmp_path / "in.psarc", tmp_path / "out.psarc"
    psarc_plain.write(source, {"x": b"x"}, ["x"], flags=4)
    with pytest.raises(SystemExit) as error:
        nc.main(["to-native", str(source), str(dest)])
    assert error.value.code == 1
    assert "encrypted" in capsys.readouterr().err
    assert not dest.exists()


def test_local_packages_round_trip(capsys, record_property):
    roots = [Path(r) for r in os.environ.get("RSXR_CHART_DIRS", "").split(os.pathsep) if r]
    if not roots or not all(root.is_dir() for root in roots):
        pytest.skip("set RSXR_CHART_DIRS to folders with plain chart packages")
    paths = sorted({p.resolve() for root in roots for p in root.rglob("*.psarc")})
    packages = bodies = encrypted = legacy = 0
    for path in paths:
        if os.name == "nt":
            path = Path("\\\\?\\" + str(path))
        header = path.open("rb")
        with header:
            raw = header.read(32)
        if len(raw) >= 32 and int.from_bytes(raw[28:32], "big") & 4:
            encrypted += 1
            continue
        files, meta = psarc_plain.read(path)
        arrangements = nc.instrument_arrangements(files)
        if any(struct.unpack_from("<I", files[name], 4)[0] != 1 for name in arrangements):
            encrypted += 1
            continue
        count = 0
        for name, instrument in arrangements.items():
            original = nc.unpack_sng(files[name])
            if original.startswith(b"SNXR"):
                continue
            try:
                native = nc.to_native(original, instrument)
            except ValueError as error:
                # Charts from before RS7X: extra tuning entries without a block. Not convertible.
                if "tuning count" not in str(error):
                    raise
                legacy += 1
                continue
            restored = nc.to_stock(native)
            end = nc._walk(original, 6).end
            assert restored[:end] == original[:end], "stock section round trip differs"
            old_ext = ext_sections(original[end:])
            new_ext = ext_sections(restored[end:])
            for ident in (b"STRS", b"CHDT", b"CHNT"):
                assert old_ext.get(ident) == new_ext.get(ident), "RS7X round trip differs"
            count += 1
        packages += bool(count)
        bodies += count
    record_property("packages", packages)
    record_property("sng_bodies", bodies)
    with capsys.disabled():
        print(
            f"\nReal data: {packages} packages, {bodies} SNG bodies round-tripped; {encrypted} encrypted packages and {legacy} pre-RS7X bodies skipped"
        )


def manifest(arrangement, tuning, **extra):
    attributes = {"ArrangementName": arrangement, "SongName": "x", "Tuning": tuning, "Tone_Base": "t", **extra}
    return json.dumps(
        {"Entries": {"ID": {"Attributes": attributes}}, "InsertRoot": "Static.Songs.Entries"}, indent=2
    ).encode()


def tuning_of(data):
    attributes = json.loads(data)["Entries"]["ID"]["Attributes"]
    return attributes.get("ExtendedRange"), attributes["Tuning"], list(attributes)


SIX = {f"string{i}": i + 1 for i in range(6)}


@pytest.mark.parametrize(
    "arrangement,stock,instrument,native",
    [
        ("Rhythm", {**SIX, "Rs7String6": -7}, "guitar", [-7, 1, 2, 3, 4, 5, 6]),
        ("Combo", {**SIX, "Rs7String6": -7, "Rs7String7": -8}, "guitar", [-8, -7, 1, 2, 3, 4, 5, 6]),
        ("Bass", {**SIX, "string4": -5, "string5": 0, "Rs7String4": -5}, "bass", [-5, 1, 2, 3, 4]),
        ("Bass", {**SIX, "string4": -5, "string5": 9, "Rs7String4": -5, "Rs7String5": 9}, "bass", [-5, 1, 2, 3, 4, 9]),
    ],
)
def test_manifest_tuning_both_ways(arrangement, stock, instrument, native):
    source = manifest(arrangement, stock)
    converted = nc.manifest_to_native(source)
    marker, tuning, keys = tuning_of(converted)
    assert marker == {"Version": 1} and (instrument == "bass") == (arrangement == "Bass")
    assert tuning == {f"string{i}": v for i, v in enumerate(native)}
    assert keys.index("ExtendedRange") + 1 == keys.index("Tuning")
    assert nc.manifest_to_native(converted) == converted
    back = nc.manifest_to_stock(converted)
    assert back == source and nc.manifest_to_stock(back) is back


@pytest.mark.parametrize("arrangement,stock", [("Lead", SIX), ("Bass", {**SIX, "string4": 0, "string5": 0})])
def test_manifest_stock_arrangements_stay_stock(arrangement, stock):
    source = manifest(arrangement, stock)  # six-string guitar, four-string bass
    assert nc.manifest_to_native(source) is source and nc.manifest_to_stock(source) is source


def test_manifest_bass_by_path_and_untouched_files():
    five = {**SIX, "string4": -5, "Rs7String4": -5}
    for name, path in (("Low", 1), ("Bass", 0)):  # the name and the path must agree
        properties = {"pathBass": path, "pathLead": 1 - path}
        if name == "Low":
            with pytest.raises(ValueError, match="ArrangementName"):
                nc.manifest_to_native(manifest(name, five, ArrangementProperties=properties))
            source = manifest(name, SIX, ArrangementProperties=properties)
            assert nc.manifest_to_native(source) is source  # a stock arrangement is not judged
        else:
            marker, tuning, _ = tuning_of(nc.manifest_to_native(manifest(name, five, ArrangementProperties=properties)))
            assert marker == {"Version": 1} and len(tuning) == 5
    vocals = json.dumps({"Entries": {"ID": {"Attributes": {"ArrangementName": "Vocals"}}}}).encode()
    assert nc.manifest_to_native(vocals) is vocals
    crlf = manifest("Lead", {**SIX, "Rs7String6": 0}).replace(b"\n", b"\r\n")
    native = nc.manifest_to_native(crlf)
    assert b"\r\n" in native and nc.manifest_to_stock(native) == crlf


@pytest.mark.parametrize(
    "arrangement,tuning,message",
    [
        ("Lead", {**SIX, "Rs7String7": 0}, "Rs7String7 without Rs7String6"),
        ("Bass", {**SIX, "Rs7String5": 0}, "Rs7String5 without Rs7String4"),
        ("Lead", {"string0": 0, "Rs7String6": 0}, "string0..string5"),
        ("Lead", {**SIX, "string0": 25, "Rs7String6": 0}, "whole number"),
        ("Lead", {**SIX, "string3": 0.5, "Rs7String6": 0}, "whole number"),
        ("Lead", {**SIX, "string3": True, "Rs7String6": 0}, "whole number"),
        ("Lead", {**SIX, "Rs7String6": "low"}, "whole number"),
        ("Bass", {**SIX, "string4": 0, "Rs7String4": -5}, "differ from string4"),
    ],
)
def test_manifest_to_native_errors(arrangement, tuning, message):
    with pytest.raises(ValueError, match=message):
        nc.manifest_to_native(manifest(arrangement, tuning))


@pytest.mark.parametrize(
    "arrangement,marker,tuning",
    [
        ("Lead", {"Version": 2}, SIX),
        ("Lead", {"Version": "1"}, SIX),
        ("Lead", 1, SIX),
        ("Lead", {"Version": 1}, {"string0": 0}),
        ("Lead", {"Version": 1}, {f"string{i}": 0 for i in range(5)}),
        ("Bass", {"Version": 1}, {f"string{i}": 0 for i in range(7)}),
        ("Lead", {"Version": 1}, {**SIX, "string7": 0}),
        ("Lead", {"Version": 1}, {**SIX, "string0": -25}),
        ("Lead", {"Version": 1}, {**SIX, "string0": "0"}),
    ],
)
def test_manifest_to_stock_errors(arrangement, marker, tuning):
    with pytest.raises(ValueError):
        nc.manifest_to_stock(manifest(arrangement, tuning, ExtendedRange=marker))


def test_package_gets_the_native_magic_and_manifests(tmp_path):
    files = {
        "songs/bin/generic/a_lead.sng": wrap(fixture(0, 8)),
        "manifests/songs/a_lead.json": manifest(
            "Lead", {**SIX, "Rs7String6": 0, "Rs7String7": -1}, SongAsset="urn:application:musicgame-song:a_lead"
        ),
        "manifests/songs/songs.hsan": manifest("Lead", {**SIX, "Rs7String6": 0, "Rs7String7": -1}),
        "appid.appid": b"221680",
    }
    source, native, back = (tmp_path / f"{s}_p.psarc" for s in ("source", "native", "back"))
    psarc_plain.write(source, files, list(files))
    nc.main(["to-native", str(source), str(native)])
    assert native.read_bytes()[:4] == b"PSXR" and source.read_bytes()[:4] == b"PSAR"
    actual, meta = psarc_plain.read(native)
    assert meta["magic"] == b"PSXR" and actual["appid.appid"] == b"221680"
    for name in ("manifests/songs/a_lead.json", "manifests/songs/songs.hsan"):
        marker, tuning, _ = tuning_of(actual[name])
        assert marker == {"Version": 1} and list(tuning.values()) == [-1, 0, 1, 2, 3, 4, 5, 6]
    again = tmp_path / "again_p.psarc"
    nc.main(["to-native", str(native), str(again)])  # an already native package is left as it is
    assert psarc_plain.read(again)[0] == actual
    nc.main(["to-stock", str(native), str(back)])
    restored, meta = psarc_plain.read(back)
    assert meta["magic"] == b"PSAR"
    assert {n: restored[n] for n in files if not n.endswith(".sng")} == {
        n: files[n] for n in files if not n.endswith(".sng")
    }


def test_manifest_already_native_is_checked_and_the_file_frame_is_kept():
    with pytest.raises(ValueError):
        nc.manifest_to_native(manifest("Lead", {"string0": 0}, ExtendedRange={"Version": 1}))
    framed = bytes([0xEF, 0xBB, 0xBF]) + manifest("Lead", {**SIX, "Rs7String6": 0}) + b"\n"
    native = nc.manifest_to_native(framed)
    assert native[:3] == framed[:3] and native.endswith(b"}\n") and nc.manifest_to_stock(native) == framed


def test_unknown_block_section_is_not_convertible():
    stock, _ = core(0, 7)
    body = nc.to_stock(nc.to_native(fixture(0, 7), 0))
    assert nc.to_native(body, 0) == nc.to_native(fixture(0, 7), 0)  # the padding section is dropped
    for ident in (b"PAD0", b"XTRA"):
        at = body.index(b"PAD0", len(stock))
        changed = bytearray(body)
        changed[at : at + 4] = ident
        changed[at + 6] = ident == b"PAD0"  # PAD0 marked critical, or an unknown section
        with pytest.raises(ValueError, match="RS7X section"):
            nc.to_native(bytes(changed), 0)
