"""Create a tone-slot test from an author's native package.

    python tools/tone_demo.py SOURCE.psarc DEMO_rsxr_p.psarc --arrangement PERSISTENT_ID --keys KEY_A KEY_B KEY_C KEY_D KEY_E KEY_F
    python tools/tone_demo.py SOURCE.psarc DEMO_rsxr_p.psarc --arrangement PERSISTENT_ID --variants 26

Uses only the supplied package's definitions, audio and chart. A Key the arrangement lacks is copied from
another arrangement of the same package. Run outside the game folder.
The first change is at --start seconds (default 5), then every --interval seconds (default 3).
--keys retains the A..F / 20-change test. --variants N (6..26) clones a definition with existing amp
drive, bass and treble controls. It visits each generated slot once, then returns to A.
For zero-based slot i: drive = (10, 50, 90)[i % 3], bass = (0, 50, 100)[(i // 3) % 3],
treble = (0, 50, 100)[i // 9]. Drive uses Gain, Drive, Loudness1, Loudness or Volume (in that order).
These are amp knob percentages, not the tone's overall Volume. All other fields are copied unchanged.
No gear or knob keys are invented; packages without suitable controls are rejected.
Choose times that fit the supplied song. Tone Plan paging is not part of this demo.
"""

import argparse
import json
import math
from copy import deepcopy
from pathlib import Path

import native_chart as nc
import psarc_plain


def _variant_controls(tone):
    gear = tone.get("GearList")
    amp = gear.get("Amp") if isinstance(gear, dict) else None
    knobs = amp.get("KnobValues") if isinstance(amp, dict) else None
    if not isinstance(knobs, dict) or not amp.get("Key"):
        return None
    names = {
        key.rsplit("_", 1)[-1].casefold(): key
        for key, value in knobs.items()
        if type(value) in (int, float) and math.isfinite(value) and 0 <= value <= 100
    }
    drive = next((names[name] for name in ("gain", "drive", "loudness1", "loudness", "volume") if name in names), None)
    if drive and "bass" in names and "treble" in names:
        return drive, names["bass"], names["treble"]
    return None


def _variant_tones(pool, count):
    for original in pool.values():
        controls = _variant_controls(original)
        if controls:
            break
    else:
        raise ValueError("variants need existing amp drive, bass and treble controls in the range 0..100")
    definitions, rows = {}, []
    for i in range(count):
        letter = chr(65 + i)
        key = f"RsxrDemo{letter}"
        if key.casefold() in pool:
            raise ValueError(f"variant Key already exists: {key}")
        tone = deepcopy(original)
        tone.update(Key=key, Name=f"Demo {letter}")
        settings = ((10, 50, 90)[i % 3], (0, 50, 100)[(i // 3) % 3], (0, 50, 100)[i // 9])
        tone["GearList"]["Amp"]["KnobValues"].update(zip(controls, settings))
        definitions[key.casefold()] = tone
        changes = ", ".join(f"{name}={value}" for name, value in zip(controls, settings))
        rows.append(f"{letter} -> {key} -> {original['Key']}: Amp {changes}")
    return definitions, rows


def _add_definitions(attrs, keys, pool):
    definitions = attrs.setdefault("Tones", [])
    for key in keys:
        if not any(str(t.get("Key", "")).casefold() == key.casefold() for t in definitions) and key.casefold() in pool:
            definitions.append(deepcopy(pool[key.casefold()]))
        if sum(str(t.get("Key", "")).casefold() == key.casefold() for t in definitions) != 1:
            raise ValueError(f"missing or duplicate definition for {key}")


def generate(source, destination, arrangement, keys=None, start=5.0, interval=3.0, variants=None):
    if Path(source).resolve() == Path(destination).resolve():
        raise ValueError("source and destination must differ")
    if variants is not None:
        if keys is not None:
            raise ValueError("provide either six tone Keys or --variants")
        if type(variants) is not int or not 6 <= variants <= 26:
            raise ValueError("variants must be an integer from 6 to 26")
    elif keys is None or len(keys) != 6 or len({key.casefold() for key in keys}) != 6:
        raise ValueError("provide six distinct tone Keys")
    if not math.isfinite(start) or start < 0 or not math.isfinite(interval) or interval <= 0:
        raise ValueError("provide a finite nonnegative start and a finite positive interval")
    if Path(source).read_bytes()[:4] != b"PSXR":
        raise ValueError("the source must be a plain native PSXR package")
    files, meta = psarc_plain.read(source)
    pool = {}
    for name in files:
        if name.lower().startswith("manifests/") and name.lower().endswith(".json"):
            for entry in json.loads(files[name].decode("utf-8-sig")).get("Entries", {}).values():
                for tone in entry.get("Attributes", {}).get("Tones") or []:
                    pool.setdefault(str(tone.get("Key", "")).casefold(), tone)
    if variants is not None:
        definitions, rows = _variant_tones(pool, variants)
        pool.update(definitions)
        keys = [tone["Key"] for tone in definitions.values()]
    else:
        rows = [f"{chr(65 + i)} -> {key} -> existing definition (unchanged)" for i, key in enumerate(keys)]
    target, found = None, False
    for name in files:
        if not name.lower().startswith("manifests/") or not name.lower().endswith((".json", ".hsan")):
            continue
        manifest = json.loads(files[name].decode("utf-8-sig"))
        for ident, entry in manifest.get("Entries", {}).items():
            if ident.casefold() != arrangement.casefold():
                continue
            attrs = entry["Attributes"]
            if attrs.get("ExtendedRange") != {"Version": 1}:
                raise ValueError("the selected arrangement must be native")
            if name.lower().endswith(".json"):
                _add_definitions(attrs, keys, pool)
                target = str(attrs.get("SongAsset", "")).rsplit(":", 1)[-1].lower() or Path(name).stem.lower()
                found = True
            for i in range(26):
                attrs[f"Tone_{chr(65 + i)}"] = keys[i] if i < len(keys) else ""
            attrs["Tone_Base"] = keys[0]
        files[name] = json.dumps(manifest, indent=2, ensure_ascii=False).encode()
    if not found:
        raise ValueError("arrangement not found in a full manifest")
    sngs = [name for name in files if name.lower().endswith(".sng") and Path(name).stem.lower() == target]
    if len(sngs) != 1:
        raise ValueError("arrangement has no unique SNG")
    name = sngs[0]
    count = len(keys) + 1 if variants is not None else 20
    changes = [(start + i * interval, i % len(keys)) for i in range(count)]
    body = nc.with_tone_changes(nc.unpack_sng(files[name]), changes)
    files[name] = nc.pack_sng(body)
    nc._check_native_package(files)
    psarc_plain.write(destination, files, meta["names"], meta["ver"], meta["flags"], b"PSXR")
    print("Slot -> Key -> changes")
    print("\n".join(rows))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--arrangement", required=True)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--keys", nargs=6)
    mode.add_argument("--variants", type=int, choices=range(6, 27), metavar="N")
    parser.add_argument("--start", type=float, default=5.0)
    parser.add_argument("--interval", type=float, default=3.0)
    args = parser.parse_args(argv)
    try:
        generate(args.source, args.destination, args.arrangement, args.keys, args.start, args.interval, args.variants)
    except (ValueError, OSError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
