"""Offline MIT headstock copy/insert package generator; no game keys or archive IO.

Read the maintainer's pipeline output NIFs + provenance reports and procedural DDS files.
Default paths are read-only. Writes only data/headstock inside this worktree.
Use --templates DIR --outputs DIR --dest DIR to select alternate fixtures.
"""

from __future__ import annotations
import argparse
import difflib
import hashlib
import json
import re
import struct
from pathlib import Path

BASE = Path.home() / "Desktop" / "rocksmith-assets"
TEMPLATES = BASE / "base/dependency_guitarstandardinlinedrh.psarc/assets/gameplay/guitar_standard_inline"
TEMPLATES_LH = BASE / "base/dependency_guitarstandardinlinedlh.psarc/assets/gameplay/guitar_standard_inline"
TEMPLATES_BASS = BASE / "base/dependency_guitarbassinlinedrh.psarc/assets/gameplay/guitar_bass_inline"
TEMPLATES_BASS_LH = BASE / "base/dependency_guitarbassinlinedlh.psarc/assets/gameplay/guitar_bass_inline"
OUTPUTS = BASE / "edited/own"
DEST = Path(__file__).resolve().parents[1] / "data/headstock"
PREFIX = b"RS7H1\n"
TEXTURES = ("env", "flat_n", "hardware", "hardware_gloss", "wood", "wood_gloss")


def windows(templates):
    return {t[i : i + 16]: (j, i) for j, t in enumerate(templates) for i in range(len(t) - 15)}


def audit_runs(data, templates, index=None):
    index = windows(templates) if index is None else index
    for i in range(len(data) - 15):
        if data[i : i + 16] in index:
            raise ValueError(f"template run at {i}")


def delta(data, templates, index=None):
    """Only template references for shared >=16-byte runs; literals otherwise."""
    index = windows(templates) if index is None else index
    ops, start, i = [], 0, 0
    while i + 16 <= len(data):
        hit = index.get(data[i : i + 16])
        if hit is None:
            i += 1
            continue
        if start < i:
            ops.append((0, data[start:i]))
        tid, offset = hit
        n = 16
        t = templates[tid]
        while i + n < len(data) and offset + n < len(t) and data[i + n] == t[offset + n]:
            n += 1
        ops.append((1, tid, offset, n))
        i += n
        start = i
    if start < len(data):
        ops.append((0, data[start:]))
    return ops


def apply(ops, templates):
    return b"".join(op[1] if op[0] == 0 else templates[op[1]][op[2] : op[2] + op[3]] for op in ops)


def nif_spans(data):
    """30.1.0.3 header + raw block spans, without a converter dependency."""
    pos = data.index(b"\n") + 1
    version, endian, _, count, _ = struct.unpack_from("<IBIII", data, pos)
    if version != 0x1E010003 or endian != 1:
        raise ValueError("unsupported NIF")
    pos += 17

    def read(fmt):
        nonlocal pos
        v = struct.unpack_from("<" + fmt, data, pos)
        pos += struct.calcsize("<" + fmt)
        return v

    def string():
        nonlocal pos
        (n,) = read("I")
        v = data[pos : pos + n]
        pos += n
        return v

    types = [string().split(b"\x01")[0] for _ in range(read("H")[0])]
    indices = read("H" * count)
    sizes = read("I" * count)
    nstrings, _ = read("II")
    for _ in range(nstrings):
        string()
    (groups,) = read("I")
    read("I" * groups)
    head = pos
    blocks = []
    for ti, size in zip(indices, sizes):
        blocks.append((types[ti & 0x7FFF].decode(), pos, size))
        pos += size
    if pos > len(data):
        raise ValueError("truncated NIF")
    return head, blocks, pos


def inherited_ops(data, source, tid, offset):
    ops = []
    for tag, a, b, c, d in difflib.SequenceMatcher(None, source, data, autojunk=False).get_opcodes():
        if tag == "equal":
            ops.append((1, tid, offset + a, b - a))
        elif c < d:
            ops.append((0, data[c:d]))
    return ops


def nif_delta(data, templates, tid, report, index):
    head, blocks, end = nif_spans(data)
    oldhead, original, oldend = nif_spans(templates[tid])
    provenance = report["block_provenance"]
    if len(provenance) != len(blocks):
        raise ValueError("block provenance count")
    # The header includes inherited shader/node strings (some shorter than 16).
    # Preserve those via references too, not just the large shared byte runs.
    ops = []
    for op in inherited_ops(data[:head], templates[tid][:oldhead], tid, 0):
        ops.extend(delta(op[1], templates, index) if op[0] == 0 else [op])
    for i, (kind, offset, size) in enumerate(blocks):
        p = provenance[i]
        if p["index"] != i or p["type"] != kind:
            raise ValueError("block provenance mismatch")
        raw = data[offset : offset + size]
        if p["provenance"] == "ours (geometry stream)" and kind == "NiDataStream":
            ops.extend(delta(raw, templates, index))
            continue
        match = re.match(r"template block (\d+) ", p["provenance"])
        if match:
            proto = original[int(match[1])]
        elif p["provenance"] == "clone of a template prototype":
            candidates = [b for b in original if b[0] == kind]
            if not candidates:
                raise ValueError("missing clone prototype")
            proto = max(
                candidates,
                key=lambda b: difflib.SequenceMatcher(
                    None, templates[tid][b[1] : b[1] + b[2]], raw, autojunk=False
                ).ratio(),
            )
        else:
            raise ValueError("unknown block provenance")
        if proto[0] != kind:
            raise ValueError("prototype type mismatch")
        for op in inherited_ops(raw, templates[tid][proto[1] : proto[1] + proto[2]], tid, proto[1]):
            ops.extend(delta(op[1], templates, index) if op[0] == 0 else [op])
    for op in inherited_ops(data[end:], templates[tid][oldend:], tid, oldend):
        ops.extend(delta(op[1], templates, index) if op[0] == 0 else [op])
    if apply(ops, templates) != data:
        raise ValueError("NIF reconstruction mismatch")
    return ops


def package(templates, artifacts):
    raw = bytearray(struct.pack("<I", 1))
    for t in templates:
        raw += hashlib.sha256(t).digest()
    raw += struct.pack("<I", len(artifacts))
    for name, data, ops in artifacts:
        name = name.encode("ascii")
        raw += struct.pack("<I", len(name)) + name + struct.pack("<I", len(data))
        raw += hashlib.sha256(data).digest() + struct.pack("<I", len(ops))
        for op in ops:
            raw.append(op[0])
            if op[0] == 0:
                raw += struct.pack("<I", len(op[1])) + op[1]
            else:
                raw += struct.pack("<III", *op[1:])
    return PREFIX + raw.hex().encode("ascii")


def unpack(data, templates):
    if not data.startswith(PREFIX):
        raise ValueError("package magic")
    raw = bytes.fromhex(data[len(PREFIX) :].decode("ascii"))
    pos = 0

    def take(n):
        nonlocal pos
        if pos + n > len(raw):
            raise ValueError("truncated package")
        v = raw[pos : pos + n]
        pos += n
        return v

    def u32():
        return struct.unpack("<I", take(4))[0]

    if u32() != 1:
        raise ValueError("package version")
    for t in templates:
        if take(32) != hashlib.sha256(t).digest():
            raise ValueError("template hash mismatch")
    outputs = {}
    for _ in range(u32()):
        name = take(u32()).decode("ascii")
        size, digest = u32(), take(32)
        result = bytearray()
        for _ in range(u32()):
            tag = take(1)[0]
            if tag == 0:
                result += take(u32())
            elif tag == 1:
                tid, offset, n = u32(), u32(), u32()
                if tid >= len(templates) or offset + n > len(templates[tid]):
                    raise ValueError("copy range")
                result += templates[tid][offset : offset + n]
            else:
                raise ValueError("instruction tag")
        if len(result) != size or hashlib.sha256(result).digest() != digest:
            raise ValueError("output hash")
        outputs[name] = bytes(result)
    if pos != len(raw):
        raise ValueError("trailing bytes")
    return outputs


def nif_artifact(name, templates, tid, outputs_dir, index):
    report = json.loads((outputs_dir / (name + ".json")).read_text(encoding="utf-8"))
    if report["template_sha256"] != hashlib.sha256(templates[tid]).hexdigest():
        raise ValueError("provenance template hash mismatch")
    data = (outputs_dir / name).read_bytes()
    if len(data) != report["bytes"]:
        raise ValueError("provenance output size mismatch")
    ops = nif_delta(data, templates, tid, report, index)
    for op in ops:
        if op[0] == 0:
            audit_runs(op[1], templates, index)
    return (
        data,
        ops,
        dict(
            bytes=len(data),
            sha256=hashlib.sha256(data).hexdigest(),
            copies=sum(op[0] == 1 for op in ops),
            literal_bytes=sum(len(op[1]) for op in ops if op[0] == 0),
        ),
    )


LH_NAMES = ("epi_whilshire_cherry_lh.nif", "epi_whilshire_peg.nif")
# The packages built from one template pair besides the right-handed guitar one (hsbuild_io.h Rs7HsKit): template 0 =
# the frame, template 1 = its peg, both from the user's own archive; the textures are the right-handed package's.
KITS = {
    "lh": dict(
        templates=TEMPLATES_LH,
        names=LH_NAMES,
        nifs=("rs7_inline7_lh.nif", "rs7_inline8_lh.nif"),
        bin="rs7string_headstock_lh.bin",
        audit="audit_lh.json",
    ),
    "bass": dict(
        templates=TEMPLATES_BASS,
        names=("bass_thunder.nif", "peg_2clove.nif"),
        nifs=("rs7_bass5.nif", "rs7_bass6.nif"),
        bin="rs7string_headstock_bass.bin",
        audit="audit_bass.json",
    ),
    "bass_lh": dict(
        templates=TEMPLATES_BASS_LH,
        names=("bass_thunder_lh.nif", "peg_2clove.nif"),
        nifs=("rs7_bass5_lh.nif", "rs7_bass6_lh.nif"),
        bin="rs7string_headstock_bass_lh.bin",
        audit="audit_bass_lh.json",
    ),
}


def generate_kit(kit, templates_dir, outputs_dir, dest):
    """One template-pair package (KITS): same v1 format as the right-handed one, its models as copy/insert artifacts."""
    k = KITS[kit]
    templates = [(templates_dir / n).read_bytes() for n in k["names"]]
    index = windows(templates)
    artifacts, info_all = [], {}
    for name in k["nifs"]:
        data, ops, info = nif_artifact(name, templates, 0, outputs_dir, index)
        artifacts.append((name, data, ops))
        info_all[name] = info
    encoded = package(templates, artifacts)
    audit_runs(encoded, templates, index)
    assert unpack(encoded, templates) == {n: d for n, d, _ in artifacts}
    dest.mkdir(parents=True, exist_ok=True)
    (dest / k["bin"]).write_bytes(encoded)
    audit = {
        **info_all,
        "package": dict(
            bytes=len(encoded),
            sha256=hashlib.sha256(encoded).hexdigest(),
            templates=list(k["names"]),
            template_16_byte_runs=0,
            nif_literal_template_16_byte_runs=0,
        ),
    }
    (dest / k["audit"]).write_text(json.dumps(audit, indent=2) + "\n", newline="\n")
    print(json.dumps(audit, indent=2))


def generate(templates_dir, outputs_dir, dest):
    templates = [(templates_dir / n).read_bytes() for n in ("epi_wilshire_cherry.nif", "epi_whilshire_peg.nif")]
    index = windows(templates)
    artifacts, audit = [], {}
    for model, tid in (("inline7", 0), ("inline8", 0), ("peg", 1)):
        name = f"rs7_{model}.nif"
        data, ops, info = nif_artifact(name, templates, tid, outputs_dir, index)
        artifacts.append((name, data, ops))
        audit[name] = info
    for texture in TEXTURES:
        name = f"rs7hs_{texture}.dds"
        data = (outputs_dir / name).read_bytes()
        artifacts.append((name, data, [(0, data)]))
        audit[name] = dict(
            bytes=len(data), sha256=hashlib.sha256(data).hexdigest(), provenance="our procedural texture"
        )
    encoded = package(templates, artifacts)
    audit_runs(encoded, templates, index)
    assert unpack(encoded, templates) == {n: d for n, d, _ in artifacts}
    dest.mkdir(parents=True, exist_ok=True)
    (dest / "rs7string_headstock.bin").write_bytes(encoded)
    for n, d, _ in artifacts:
        if n.endswith(".dds"):
            (dest / n).write_bytes(d)
    audit["package"] = dict(
        bytes=len(encoded),
        sha256=hashlib.sha256(encoded).hexdigest(),
        template_16_byte_runs=0,
        nif_literal_template_16_byte_runs=0,
    )
    (dest / "audit.json").write_text(json.dumps(audit, indent=2) + "\n", newline="\n")
    print(json.dumps(audit, indent=2))


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--templates", type=Path, default=TEMPLATES)
    ap.add_argument("--templates-lh", type=Path, default=TEMPLATES_LH)
    ap.add_argument("--only", choices=("rh", *KITS), help="build just one package (default: all)")
    ap.add_argument("--outputs", type=Path, default=OUTPUTS)
    ap.add_argument("--dest", type=Path, default=DEST)
    args = ap.parse_args()
    if args.only in (None, "rh"):
        generate(args.templates, args.outputs, args.dest)
    for kit, k in KITS.items():
        if args.only in (None, kit):
            generate_kit(kit, args.templates_lh if kit == "lh" else k["templates"], args.outputs, args.dest)
