"""Reproduce issue #20 signatures from the unpacked analysis copy (read-only).

Run once from this checkout: uv run python tools/diagrams_sites.py
Writes only sites.json here. Never reads protected archives or uses keys.
"""

import json
from pathlib import Path
import pefile

REPO = Path(__file__).resolve().parents[1]


def main():
    pe = pefile.PE(str(REPO.parent / "original" / "Rocksmith2014.unpacked.exe"))
    entries = [
        ("DGM", 0x757698, 7),
        ("DGS", 0x7580DF, 7),
        ("TP13", 0x7578BC, 12),
        ("DGB", 0x55A02C, 11),
        ("X_DKEY", 0x7571F2, 5),
        ("X_DNUMBER", 0x75763B, 5),
        ("X_DSET", 0x757662, 5),
        ("X_DDROP", 0x757689, 5),
    ]
    path = REPO / "sites.json"
    raw = path.read_bytes()
    data = json.loads(raw)
    ids = {e[0] for e in entries}
    data["sites"] = [e for e in data["sites"] if e["id"] not in ids]
    for ident, va, n in entries:
        # Trailing bytes may include branch destinations inside the hook; only
        # the stolen instructions are ever overwritten. Live import stubs are
        # taken from the unpacked copy, not rebuilt analysis IAT operands.
        b = pe.get_data(va - pe.OPTIONAL_HEADER.ImageBase, 19 if ident == "TP13" else 32)
        entry = dict(
            id=ident,
            kind="callref" if ident.startswith("X_") else "jmp",
            pattern=b.hex(" ").upper(),
            offset="0x0",
            expect=b[:n].hex(" ").upper(),
            note=f"CONFIRMED 0x{va:06X}: issue #20 chord diagrams; diagrams7 gates all writes",
        )
        if not ident.startswith("X_"):
            entry["length"] = n
        data["sites"].append(entry)
    # Preserve the established compact one-record-per-line representation.
    head = json.dumps({k: v for k, v in data.items() if k != "sites"}, indent=2)[:-2]
    output = (
        head + ',\n  "sites": [\n' + ",\n".join("    " + json.dumps(e) for e in data["sites"]) + "\n  ]\n}\n"
    ).encode()
    path.write_bytes(output.replace(b"\n", b"\r\n") if b"\r\n" in raw else output)


if __name__ == "__main__":
    main()
