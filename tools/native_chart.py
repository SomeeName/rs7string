"""Reference converter for plain SNXR charts; no encryption or game files required.

    uv run python tools/native_chart.py to-native IN.psarc OUT.psarc
    uv run python tools/native_chart.py to-stock IN.psarc OUT.psarc
    uv run python tools/native_chart.py body-to-native IN.bin OUT.bin --instrument 0
    uv run python tools/native_chart.py body-to-stock IN.bin OUT.bin

A package conversion covers the charts (SNG), the Tuning of every manifest and
header file (manifests/*.json, *.hsan) and the package magic (PSXR for native).

Instrument 0 is guitar, 1 is bass. Package instruments come from arrangement
manifests (SongAsset links, with manifest basename as fallback). Vocals,
showlights, manifests and all other entries retain their exact payload bytes.
Only unencrypted PSARC tables and SNG wrapper flags 1 are supported. Output
packages are repacked, so compressed archive bytes/offsets can change.

Without STRS, to_native defaults to six guitar/four bass strings; an explicit
strings argument must agree. With STRS, both arguments are superseded by STRS.
SNXR cannot carry unknown RS7X sections or legacy name-byte chord extensions.
Unused bass columns are reconstructed with fret/finger/note -1 and empty
techniques. Padding follows NATIVE-FORMAT.md, increased if real sizes require it.
"""

import argparse
import json
import struct
import zlib
from dataclasses import dataclass, field
from pathlib import Path

import psarc_plain


OPEN_MIDI = ((40, 45, 50, 55, 59, 64, 35, 30), (28, 33, 38, 43, 23, 48))
SUPPORTED = ((6, 7, 8), (4, 5, 6))
EMPTY_TECH = b"\0" * 4 + b"\xff\xff" + b"\0" * 3


def _pack(fmt, *values):
    return struct.pack("<" + fmt, *values)


class _Reader:
    def __init__(self, data, pos=0):
        self.data, self.pos = data, pos

    def take(self, size):
        if size < 0 or size > len(self.data) - self.pos:
            raise ValueError(f"truncated input at byte {self.pos}: need {size} bytes")
        start = self.pos
        self.pos += size
        return self.data[start : self.pos]

    def unpack(self, fmt):
        return struct.unpack("<" + fmt, self.take(struct.calcsize("<" + fmt)))

    def count(self, minimum=1):
        (count,) = self.unpack("I")
        if count > (len(self.data) - self.pos) // minimum:
            raise ValueError(f"truncated array at byte {self.pos}: count {count}")
        return count

    def array(self, size):
        count = self.count(size)
        start = self.pos
        self.take(count * size)
        return [(start + i * size, start + (i + 1) * size) for i in range(count)]


@dataclass
class _Layout:
    chords: list = field(default_factory=list)
    techniques: list = field(default_factory=list)
    sections: list = field(default_factory=list)
    notes: list = field(default_factory=list)
    tuning: tuple = (0, 0)
    end: int = 0


def _walk(data, width, start=0):
    """Walk the complete little-endian SNG layout, keeping only replacement spans."""
    reader, layout = _Reader(data, start), _Layout()
    reader.array(16)  # Beats
    reader.array(44)  # Phrases
    layout.chords = reader.array(36 + 6 * width)
    layout.techniques = reader.array(396 * width)
    vocals = reader.array(60)
    if vocals:
        for size in (32, 144, 44):  # Symbols: headers, textures, definitions
            reader.array(size)
    reader.array(24)  # PhraseIterations
    reader.array(16)  # PhraseExtraInfo
    for _ in range(reader.count(8)):  # NewLinkedDifficulties
        reader.take(4)
        reader.array(4)
    for size in (260, 260, 8, 8):  # Actions, Events, Tones, DNA
        reader.array(size)
    layout.sections = reader.array(88)
    for _ in range(reader.count(36)):  # Levels
        reader.take(4)  # difficulty
        for size in (28, 12, 20, 20):  # anchors, extensions, handshapes, arpeggios
            reader.array(size)
        for _ in range(reader.count(67)):
            note = reader.pos
            reader.take(63)
            reader.array(12)  # variable BendValues
            layout.notes.append((note + 16, note + 17))
        for _ in range(3):  # average notes, counts excluding ignored, all counts
            reader.array(4)
    reader.take(79)  # MetaData up to tuning count
    start = reader.pos
    count = reader.count(2)
    if count > 16:
        raise ValueError("invalid MetaData tuning count (maximum 16)")
    reader.take(count * 2)
    layout.tuning = (start, reader.pos)
    reader.take(12)  # first note time twice, maxDifficulty
    layout.end = reader.pos
    return layout


def _slots(instrument, strings):
    if instrument not in (0, 1):
        raise ValueError("invalid instrument: expected 0 (guitar) or 1 (bass)")
    if strings not in SUPPORTED[instrument]:
        raise ValueError(f"unsupported string count {strings} for instrument {instrument}")
    return sorted(range(strings), key=lambda slot: OPEN_MIDI[instrument][slot])


def _tuning(data, layout):
    start, end = layout.tuning
    return list(struct.unpack("<" + "h" * ((end - start - 4) // 2), data[start + 4 : end]))


def _split_template(raw, width):
    frets = list(struct.unpack_from(f"<{width}b", raw, 4))
    fingers = list(struct.unpack_from(f"<{width}b", raw, 4 + width))
    notes = list(struct.unpack_from(f"<{width}i", raw, 4 + 2 * width))
    return frets, fingers, notes


def _template(raw, width, frets, fingers, notes):
    n = len(frets)
    return raw[:4] + _pack(f"{n}b", *frets) + _pack(f"{n}b", *fingers) + _pack(f"{n}i", *notes) + raw[4 + 6 * width :]


def _tech_columns(raw, width):
    """Keep full BendData32 bytes for stock columns, including unused bend slots."""
    result = []
    for i in range(width):
        bend = raw[4 * width + 388 * i : 4 * width + 388 * (i + 1)]
        (count,) = struct.unpack_from("<I", bend, 384)
        if count > 32:
            raise ValueError("ChordNotes bend count exceeds 32")
        result.append(
            (
                raw[4 * i : 4 * i + 4],
                bend,
                raw[392 * width + i : 392 * width + i + 1],
                raw[393 * width + i : 393 * width + i + 1],
                raw[394 * width + 2 * i : 394 * width + 2 * i + 2],
            )
        )
    return result


def _join_tech(columns):
    return b"".join(b"".join(column[field] for column in columns) for field in range(5))


def _expand_tech(tech):
    count = tech[8]
    return (tech[:4], tech[9:] + bytes(12 * (32 - count)) + _pack("I", count), tech[4:5], tech[5:6], tech[6:8])


def _compact_tech(column):
    mask, bend, slide, unpitched, vibrato = column
    (count,) = struct.unpack_from("<I", bend, 384)
    return mask + slide + unpitched + vibrato + bytes((count,)) + bend[: 12 * count]


def _section(ident, payload, flags=0):
    return _pack("4sHHI", ident, 1, flags, len(payload)) + payload


def _block(instrument, strings, templates, techniques, padding=None):
    extra = list(range(6, strings)) if instrument == 0 else []
    parts = [_section(b"STRS", bytes((instrument, strings, len(extra), 0, *extra)), 1)]
    if extra:
        parts.append(_section(b"CHDT", _pack("I", len(templates)) + b"".join(templates), 1))
        parts.append(_section(b"CHNT", _pack("I", len(techniques)) + b"".join(techniques)))
    if padding is not None:
        parts.append(_section(b"PAD0", bytes(padding)))
    payload = b"".join(parts)
    return _pack("4sHHII", b"RS7X", 1, 16, len(payload) + 16, len(parts)) + payload


def _read_sections(tail):
    if not tail or not any(tail):
        return {}
    reader = _Reader(tail)
    magic, version, size, total, count = reader.unpack("4sHHII")
    if magic != b"RS7X" or version != 1:
        raise ValueError("invalid RS7X magic/version after MetaData")
    if total != len(tail) or not 16 <= size <= total or total > 16 * 1024 * 1024:
        raise ValueError("invalid RS7X block/header size")
    reader.take(size - 16)
    result, seen = {}, set()
    for i in range(count):
        ident, version, flags, length = reader.unpack("4sHHI")
        if ident in seen or flags & ~1 or (i == 0 and ident != b"STRS"):
            raise ValueError("invalid RS7X section order, duplicate id or flags")
        seen.add(ident)
        payload = reader.take(length)
        if ident == b"PAD0" and not flags & 1:
            continue  # padding the mod's in-place conversion writes; carries nothing
        if version != 1 or ident not in (b"STRS", b"CHDT", b"CHNT"):
            raise ValueError("unknown RS7X section or version: the native format has no place for it")
        result[ident] = payload
    if reader.pos != len(tail) or b"STRS" not in result:
        raise ValueError("RS7X sections do not tile block or STRS missing")
    return result


def _read_extension(data, layout):
    sections = _read_sections(data[layout.end :])
    if not sections:
        return None
    strs = _Reader(sections[b"STRS"])
    instrument, strings, columns, reserved = strs.unpack("BBBB")
    _slots(instrument, strings)
    expected = max(0, strings - 6) if instrument == 0 else 0
    if (
        reserved
        or columns != expected
        or strs.take(columns) != bytes(range(6, 6 + columns))
        or strs.pos != len(strs.data)
        or strings == (6 if instrument == 0 else 4)
    ):
        raise ValueError("invalid RS7X STRS payload")
    if len(_tuning(data, layout)) != (strings if instrument == 0 else 6):
        raise ValueError("RS7X STRS and MetaData tuning count disagree")
    if instrument == 1 and (b"CHDT" in sections or b"CHNT" in sections):
        raise ValueError("bass RS7X must omit CHDT and CHNT")
    templates = _read_frets(sections.get(b"CHDT"), len(layout.chords), columns)
    techniques = _read_techniques(sections.get(b"CHNT"), len(layout.techniques), columns)
    return instrument, strings, templates, techniques


def _read_frets(payload, count, columns):
    if payload is None:
        return [[(-1, -1)] * columns for _ in range(count)]
    reader = _Reader(payload)
    if reader.unpack("I")[0] != count or len(payload) != 4 + 2 * count * columns:
        raise ValueError("RS7X CHDT count/size mismatch")
    result = []
    for _ in range(count):
        row = []
        for _ in range(columns):
            fret, finger = reader.unpack("bb")
            if not -1 <= fret <= 24 or not -1 <= finger <= 4 or (fret == -1 and finger != -1):
                raise ValueError("invalid RS7X CHDT fret/finger")
            row.append((fret, finger))
        result.append(row)
    return result


def _read_techniques(payload, count, columns):
    if payload is None:
        return [[EMPTY_TECH] * columns for _ in range(count)]
    reader = _Reader(payload)
    if reader.unpack("I")[0] != count:
        raise ValueError("RS7X CHNT count mismatch")
    result = []
    for _ in range(count):
        row = []
        for _ in range(columns):
            prefix = reader.take(9)
            slide, unpitched = struct.unpack_from("bb", prefix, 4)
            bends = prefix[8]
            if bends > 32 or not -1 <= slide <= 24 or not -1 <= unpitched <= 24:
                raise ValueError("invalid RS7X CHNT slide/bend count")
            row.append(prefix + reader.take(bends * 12))
        result.append(row)
    if reader.pos != len(payload):
        raise ValueError("RS7X CHNT trailing bytes")
    return result


def _replace(data, end, edits, start=0):
    parts, pos = [], start
    for left, right, replacement in sorted(edits):
        parts.extend((data[pos:left], replacement))
        pos = right
    parts.append(data[pos:end])
    return b"".join(parts)


def _common_edits(data, layout, mapping):
    edits = []
    for start, end in layout.notes:
        string = data[start]
        if string == 255:
            continue
        if string not in mapping:
            raise ValueError(f"invalid note string index {string}")
        edits.append((start, end, bytes((mapping[string],))))
    for start, end in layout.sections:
        masks = []
        for mask in data[start + 52 : end]:
            if mask & ~sum(1 << s for s in mapping):
                raise ValueError("section string mask refers to an unavailable string")
            masks.append(sum(1 << dest for source, dest in mapping.items() if mask & (1 << source)))
        edits.append((start + 52, end, bytes(masks)))
    return edits


def to_native(stock_body: bytes, instrument: int, strings: int | None = None) -> bytes:
    """Convert stock SNG sections plus optional valid RS7X to a padded SNXR body."""
    layout = _walk(stock_body, 6)
    extension = _read_extension(stock_body, layout)
    if extension:
        instrument, strings, extra_frets, extra_tech = extension
    else:
        if instrument not in (0, 1):
            raise ValueError("invalid instrument: expected 0 or 1")
        default = 6 if instrument == 0 else 4
        if strings is not None and strings != default:
            raise ValueError("string count without RS7X must be guitar 6 or bass 4")
        strings = default
        extra_frets, extra_tech = [], []
    slots = _slots(instrument, strings)
    tuning = _tuning(stock_body, layout)
    if len(tuning) != (strings if instrument == 0 else 6):
        raise ValueError("invalid stock MetaData tuning count")
    edits = _common_edits(stock_body, layout, {s: i for i, s in enumerate(slots)})
    for r, (start, end) in enumerate(layout.chords):
        raw = stock_body[start:end]
        frets, fingers, notes = _split_template(raw, 6)
        if instrument == 0 and strings > 6:
            for slot, (fret, finger) in enumerate(extra_frets[r], 6):
                frets.append(fret)
                fingers.append(finger)
                notes.append(OPEN_MIDI[0][slot] + tuning[slot] + fret if fret >= 0 else -1)
        edits.append(
            (
                start,
                end,
                _template(raw, 6, [frets[s] for s in slots], [fingers[s] for s in slots], [notes[s] for s in slots]),
            )
        )
    for r, (start, end) in enumerate(layout.techniques):
        columns = _tech_columns(stock_body[start:end], 6)
        if instrument == 0 and strings > 6:
            columns.extend(_expand_tech(t) for t in extra_tech[r])
        edits.append((start, end, _join_tech([columns[s] for s in slots])))
    start, end = layout.tuning
    edits.append((start, end, _pack("I", strings) + _pack(f"{strings}h", *(tuning[s] for s in slots))))
    header = _pack("4sBBBB", b"SNXR", 1, instrument, strings, 0)
    native = header + _replace(stock_body, layout.end, edits)
    padding = 256 + max(0, 6 - strings) * (6 * len(layout.chords) + 396 * len(layout.techniques) + 2)
    # An input block can carry large technique payloads; keep the actual fit check authoritative.
    return native + bytes(padding)


def _native_header(data):
    magic, version, instrument, strings, reserved = _Reader(data).unpack("4sBBBB")
    if magic != b"SNXR":
        raise ValueError("invalid SNXR magic")
    if version != 1:
        raise ValueError(f"unsupported SNXR version {version}")
    slots = _slots(instrument, strings)
    if reserved:
        raise ValueError("SNXR reserved field must be zero")
    return instrument, strings, slots, 8


def _stock_chords(data, layout, slots, width):
    edits, extras = [], []
    for start, end in layout.chords:
        raw = data[start:end]
        frets, fingers, notes = _split_template(raw, width)
        arrays = [[-1] * 6 for _ in range(3)]
        for i, slot in enumerate(slots):
            if slot < 6:
                for dest, source in zip(arrays, (frets, fingers, notes)):
                    dest[slot] = source[i]
        extras.append(b"".join(_pack("bb", frets[slots.index(s)], fingers[slots.index(s)]) for s in range(6, width)))
        edits.append((start, end, _template(raw, width, *arrays)))
    return edits, extras


def _stock_techniques(data, layout, slots, instrument, width):
    edits, extras = [], []
    for start, end in layout.techniques:
        columns = _tech_columns(data[start:end], width)
        stock = [_expand_tech(EMPTY_TECH)] * 6
        for i, slot in enumerate(slots):
            if slot < 6:
                stock[slot] = columns[i]
        extras.append(b"".join(_compact_tech(columns[slots.index(s)]) for s in range(6, width) if instrument == 0))
        edits.append((start, end, _join_tech(stock)))
    return edits, extras


def to_stock(native_body: bytes) -> bytes:
    """Convert SNXR to stock sections plus RS7X/PAD0, retaining the exact input length."""
    instrument, strings, slots, start = _native_header(native_body)
    layout = _walk(native_body, strings, start)
    if any(native_body[layout.end :]):
        raise ValueError("SNXR padding must contain only zero bytes")
    tuning = _tuning(native_body, layout)
    if len(tuning) != strings:
        raise ValueError("SNXR MetaData tuning count must equal header string count")
    edits = _common_edits(native_body, layout, dict(enumerate(slots)))
    chord_edits, templates = _stock_chords(native_body, layout, slots, strings)
    tech_edits, techniques = _stock_techniques(native_body, layout, slots, instrument, strings)
    edits.extend(chord_edits + tech_edits)
    stock_tuning = [0] * (strings if instrument == 0 else 6)
    for i, slot in enumerate(slots):
        stock_tuning[slot] = tuning[i]
    left, right = layout.tuning
    edits.append((left, right, _pack("I", len(stock_tuning)) + _pack(f"{len(stock_tuning)}h", *stock_tuning)))
    stock = _replace(native_body, layout.end, edits, start)
    extended = strings != (6 if instrument == 0 else 4)
    block = _block(instrument, strings, templates, techniques, 0) if extended else b""
    remaining = len(native_body) - len(stock) - len(block)
    if remaining < 0:
        raise ValueError(f"converted form does not fit: missing {-remaining} bytes of padding")
    if extended:
        return stock + _block(instrument, strings, templates, techniques, remaining)
    return stock + bytes(remaining)


def unpack_sng(data):
    """Read only a plain flags-1 SNG wrapper; never decrypt."""
    if len(data) < 84:
        raise ValueError("truncated SNG wrapper")
    magic, flags = struct.unpack_from("<II", data)
    if magic != 0x4A:
        raise ValueError("invalid SNG wrapper magic")
    if flags != 1:
        raise ValueError(f"encrypted or unsupported SNG (flags {flags}); only plain flags 1 accepted")
    if any(data[8:24]) or any(data[-56:]):
        raise ValueError("plain SNG requires zero IV and signature slots")
    (size,) = struct.unpack_from("<I", data, 24)
    try:
        decoder = zlib.decompressobj()
        body = decoder.decompress(data[28:-56], size + 1)
    except zlib.error as error:
        raise ValueError(f"invalid SNG zlib stream: {error}") from None
    if not decoder.eof:
        raise ValueError("truncated SNG zlib stream or declared size too small")
    if len(body) != size or decoder.unused_data or decoder.unconsumed_tail:
        raise ValueError("SNG body does not match declared size/stream boundary")
    return body


def pack_sng(body):
    return _pack("II", 0x4A, 1) + bytes(16) + _pack("I", len(body)) + zlib.compress(body, 9) + bytes(56)


def instrument_arrangements(files):
    """Resolve SNG entries from manifests; never infer bass from an SNG filename."""
    sngs = {Path(name).stem.lower(): name for name in files if name.lower().endswith(".sng")}
    result = {}
    for name, data in files.items():
        if not name.lower().startswith("manifests/") or not name.lower().endswith(".json"):
            continue
        try:
            manifest = json.loads(data.decode("utf-8-sig"))
            for entry in manifest.get("Entries", {}).values():
                attributes = entry.get("Attributes", {})
                arrangement = attributes.get("ArrangementName", "").lower()
                if arrangement in ("vocals", "jvocals", "showlights"):
                    continue
                properties = attributes.get("ArrangementProperties", {})
                if arrangement not in ("lead", "rhythm", "bass", "combo") and not any(
                    properties.get(key) for key in ("pathLead", "pathRhythm", "pathBass")
                ):
                    continue
                instrument = int(arrangement == "bass" or bool(properties.get("pathBass")))
                asset = attributes.get("SongAsset", "").rsplit(":", 1)[-1].lower()
                key = asset or Path(name).stem.lower()
                if key not in sngs:
                    raise ValueError("instrument manifest has no matching SNG entry")
                target = sngs[key]
                if target in result and result[target] != instrument:
                    raise ValueError("conflicting instrument manifests for SNG")
                result[target] = instrument
        except (UnicodeError, json.JSONDecodeError, AttributeError, TypeError) as error:
            raise ValueError(f"invalid arrangement manifest: {error}") from None
    return result


NATIVE_KEY = "ExtendedRange"
EXTRA_KEYS = (("Rs7String6", "Rs7String7"), ("Rs7String4", "Rs7String5"))  # guitar, bass


def _attribute_sets(manifest):
    """Every arrangement attribute object of a manifest .json or .hsan that has a Tuning object."""
    for entry in manifest.get("Entries", {}).values():
        attributes = entry.get("Attributes") if isinstance(entry, dict) else None
        if isinstance(attributes, dict) and isinstance(attributes.get("Tuning"), dict):
            yield attributes


def _named_bass(attributes):
    """A native arrangement is a bass when its ArrangementName is "Bass": the only instrument field every manifest
    and header-file entry has, and what the mod reads."""
    return attributes.get("ArrangementName") == "Bass"


def _is_bass(attributes):
    properties = attributes.get("ArrangementProperties")
    return str(attributes.get("ArrangementName", "")).lower() == "bass" or bool(
        isinstance(properties, dict) and properties.get("pathBass")
    )


def _with_tuning(attributes, tuning, marker):
    """The attributes with a new Tuning, and the native marker placed right before it (or removed)."""
    result = {}
    for key, value in attributes.items():
        if key == NATIVE_KEY:
            continue
        if key == "Tuning":
            if marker:
                result[NATIVE_KEY] = marker
            value = tuning
        result[key] = value
    return result


def _offset(value):
    if type(value) is not int or not -24 <= value <= 24:
        raise ValueError(f"manifest tuning offset {value!r} is not a whole number from -24 to 24")
    return value


def _edit_manifest(data, change):
    bom = b"\xef\xbb\xbf" if data.startswith(b"\xef\xbb\xbf") else b""
    text = data.decode("utf-8-sig")
    manifest = json.loads(text)
    changed = False
    for entry in manifest.get("Entries", {}).values():
        if isinstance(entry, dict) and isinstance(entry.get("Attributes"), dict):
            attributes = entry["Attributes"]
            if isinstance(attributes.get("Tuning"), dict):
                entry["Attributes"] = change(attributes)
                changed = changed or entry["Attributes"] is not attributes
    if not changed:
        return data  # nothing to convert: the file keeps its exact bytes
    newline = "\r\n" if "\r\n" in text else "\n"
    result = json.dumps(manifest, indent=2, ensure_ascii=False) + ("\n" if text.endswith("\n") else "")
    return bom + result.replace("\n", newline).encode("utf-8")


def _native_attributes(attributes):
    if NATIVE_KEY in attributes:
        _stock_attributes(attributes)  # already native: it must be a valid one
        return attributes
    bass = _is_bass(attributes)
    tuning = attributes["Tuning"]
    extra = sum(key in tuning for key in EXTRA_KEYS[bass])
    if not extra:
        return attributes  # a six-string guitar or four-string bass arrangement stays stock
    if bass != _named_bass(attributes):
        raise ValueError('a bass arrangement must have ArrangementName "Bass" (and only a bass)')
    if extra == 1 and EXTRA_KEYS[bass][0] not in tuning:
        raise ValueError(f"manifest Tuning has {EXTRA_KEYS[bass][1]} without {EXTRA_KEYS[bass][0]}")
    if any(f"string{i}" not in tuning for i in range(6)):
        raise ValueError("manifest Tuning lacks string0..string5")
    values = [_offset(tuning[f"string{i}"]) for i in range(6)]
    if not bass:
        values += [_offset(tuning[key]) for key in EXTRA_KEYS[0][:extra]]
    elif any(tuning[key] != values[4 + i] for i, key in enumerate(EXTRA_KEYS[1][:extra])):
        raise ValueError("bass manifest Tuning: Rs7String4/Rs7String5 differ from string4/string5")
    slots = _slots(int(bass), (4 if bass else 6) + extra)
    native = {f"string{i}": values[slot] for i, slot in enumerate(slots)}
    return _with_tuning(attributes, native, {"Version": 1})


def _stock_attributes(attributes):
    marker = attributes.get(NATIVE_KEY)
    if marker is None:
        return attributes
    if not isinstance(marker, dict) or marker.get("Version") != 1:
        raise ValueError("unsupported ExtendedRange marker in a manifest")
    bass = _named_bass(attributes)
    native = attributes["Tuning"]
    count = len(native)
    if count not in SUPPORTED[bass] or any(f"string{i}" not in native for i in range(count)):
        raise ValueError("native manifest Tuning is not string0..stringN-1 for a supported string count")
    values = [0] * 8
    for i, slot in enumerate(_slots(int(bass), count)):
        values[slot] = _offset(native[f"string{i}"])
    tuning = {f"string{i}": values[i] for i in range(6)}
    extra = count - (4 if bass else 6)
    for key, slot in list(zip(EXTRA_KEYS[bass], (4, 5) if bass else (6, 7)))[:extra]:
        tuning[key] = values[slot]
    return _with_tuning(attributes, tuning, None)


def manifest_to_native(data: bytes) -> bytes:
    """A manifest .json or .hsan with every instrument arrangement's Tuning in the native form (thickest first)."""
    return _edit_manifest(data, _native_attributes)


def manifest_to_stock(data: bytes) -> bytes:
    """The inverse of manifest_to_native: string0-string5 in slot order plus the Rs7String keys."""
    return _edit_manifest(data, _stock_attributes)


def _check_native_package(files):
    """Every instrument chart and its manifest and header-file entries name the same instrument and string count,
    or are all stock (a six-string guitar or four-string bass arrangement). True: the package has a native chart."""
    bodies = {}
    for name in instrument_arrangements(files):
        body = unpack_sng(files[name])
        bodies[Path(name).stem.lower()] = _native_header(body)[:2] if body.startswith(b"SNXR") else None
    shapes, charts = {}, {}
    for name, data in files.items():
        lower = name.lower()
        if not lower.startswith("manifests/") or not lower.endswith((".json", ".hsan")):
            continue
        for key, entry in json.loads(data.decode("utf-8-sig")).get("Entries", {}).items():
            attributes = entry.get("Attributes") if isinstance(entry, dict) else None
            if not isinstance(attributes, dict):
                continue
            if lower.endswith(".json"):
                asset = str(attributes.get("SongAsset", "")).rsplit(":", 1)[-1].lower() or Path(name).stem.lower()
                if asset in bodies:
                    charts[key] = asset
            marker, tuning = attributes.get(NATIVE_KEY), attributes.get("Tuning")
            native = isinstance(marker, dict) and isinstance(tuning, dict)
            shapes.setdefault(key, []).append((int(_named_bass(attributes)), len(tuning)) if native else None)
    for key, asset in charts.items():
        if any(shape != bodies[asset] for shape in shapes[key]):
            raise ValueError(f"chart {asset} and its manifests disagree on the instrument or the string count")
    return any(bodies.values())


def convert_package(source, destination, native):
    with Path(source).open("rb") as stream:
        header = stream.read(32)
    if len(header) != 32 or header[:4] not in psarc_plain.MAGICS:
        raise ValueError("invalid or truncated PSARC header")
    if int.from_bytes(header[28:32], "big") & 4:
        raise ValueError("encrypted PSARC table; only plain packages accepted")
    try:
        files, meta = psarc_plain.read(source)
    except (AssertionError, struct.error, IndexError) as error:
        raise ValueError(f"malformed plain PSARC: {error}") from None
    for name, instrument in instrument_arrangements(files).items():
        body = unpack_sng(files[name])
        if body.startswith(b"SNXR"):
            if native:
                to_stock(body)  # already native: it must be a valid chart
            else:
                files[name] = pack_sng(to_stock(body))
        elif native:
            converted = to_native(body, instrument)
            if _native_header(converted)[1] != SUPPORTED[instrument][0]:
                files[name] = pack_sng(converted)  # six-string guitar and four-string bass charts stay stock
    for name in files:
        if name.lower().startswith("manifests/") and name.lower().endswith((".json", ".hsan")):
            files[name] = manifest_to_native(files[name]) if native else manifest_to_stock(files[name])
    magic = b"PSXR" if native and _check_native_package(files) else b"PSAR"
    psarc_plain.write(destination, files, meta["names"], meta["ver"], meta["flags"], magic)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    for command in ("to-native", "to-stock", "body-to-native", "body-to-stock"):
        sub = commands.add_parser(command)
        sub.add_argument("source", type=Path)
        sub.add_argument("destination", type=Path)
        if command == "body-to-native":
            sub.add_argument("--instrument", type=int, choices=(0, 1), required=True)
    args = parser.parse_args(argv)
    try:
        if args.command.startswith("body-"):
            body = args.source.read_bytes()
            converted = to_native(body, args.instrument) if args.command == "body-to-native" else to_stock(body)
            args.destination.write_bytes(converted)
        else:
            convert_package(args.source, args.destination, args.command == "to-native")
    except (ValueError, OSError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
