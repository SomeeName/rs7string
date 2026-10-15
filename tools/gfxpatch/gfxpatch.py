"""Patch user-supplied unencrypted GFX copies; no archives or game access.

CLI is documented in README.md. All FFDec intermediates contain game assets
and stay under ignored .work/. Only the selected DoInitAction is transplanted.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import zlib

CLASS = "__Packages/rs/controls/RSChordDisplay"
STOCK_SHA = "54082732b6c24bbb96632e97d6e59ab5920f4b4935be8609056c08b671978ad2"
TARGETS = {"libs/commonlib.gfx": 1103, "views/contentpanel.gfx": 28, "views/ge_game.gfx": 26}


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


@dataclass
class Tag:
    code: int
    payload: bytes
    raw: bytes


@dataclass
class Movie:
    signature: bytes
    version: int
    frame_header: bytes
    tags: list[Tag]

    def encode(self) -> bytes:
        body = self.frame_header + b"".join(t.raw for t in self.tags)
        header = self.signature + bytes([self.version]) + struct.pack("<I", len(body) + 8)
        return header + (zlib.compress(body) if self.signature in (b"CFX", b"CWS") else body)


def parse_movie(data: bytes) -> Movie:
    if len(data) < 9 or data[:3] not in (b"GFX", b"CFX", b"FWS", b"CWS"):
        raise ValueError("Expected unencrypted GFX/SWF, optionally zlib compressed")
    body = zlib.decompress(data[8:]) if data[:3] in (b"CFX", b"CWS") else data[8:]
    if len(body) + 8 != struct.unpack_from("<I", data, 4)[0]:
        raise ValueError("Movie length mismatch")
    if not body:
        raise ValueError("Missing frame header")
    rect_bytes = (5 + 4 * (body[0] >> 3) + 7) // 8
    offset = rect_bytes + 4
    if body[0] >> 3 == 0 or offset > len(body):
        raise ValueError("Invalid frame header")
    frame_header = body[:offset]
    tags = []
    while offset < len(body):
        start = offset
        if offset + 2 > len(body):
            raise ValueError("Truncated tag header")
        word = struct.unpack_from("<H", body, offset)[0]
        offset += 2
        length = word & 63
        if length == 63:
            if offset + 4 > len(body):
                raise ValueError("Truncated long tag header")
            length = struct.unpack_from("<I", body, offset)[0]
            offset += 4
        end = offset + length
        if end > len(body):
            raise ValueError("Truncated tag")
        tags.append(Tag(word >> 6, body[offset:end], body[start:end]))
        offset = end
    if not tags or tags[-1].code != 0 or tags[-1].payload or any(t.code == 0 for t in tags[:-1]):
        raise ValueError("Invalid or missing End tag")
    return Movie(data[:3], data[3], frame_header, tags)


def action_index(movie: Movie, sprite_id: int) -> int:
    matches = [
        i
        for i, t in enumerate(movie.tags)
        if t.code == 59 and len(t.payload) >= 2 and struct.unpack_from("<H", t.payload)[0] == sprite_id
    ]
    if len(matches) != 1:
        raise ValueError(f"Expected exactly one DoInitAction for {sprite_id}, got {len(matches)}")
    return matches[0]


def transplant(original: bytes, compiled: bytes, sprite_id: int) -> bytes:
    old, new = parse_movie(original), parse_movie(compiled)
    old.tags[action_index(old, sprite_id)] = new.tags[action_index(new, sprite_id)]
    return old.encode()


def method_span(source: str, name: str) -> tuple[int, int]:
    match = re.search(r"\bfunction " + re.escape(name) + r"\([^)]*\)\s*\{", source)
    if not match:
        raise ValueError(f"Missing method {name}")
    start = match.end() - 1
    depth = 1
    # This reviewed class has no braces inside strings/comments in these methods.
    for end in range(start + 1, len(source)):
        depth += (source[end] == "{") - (source[end] == "}")
        if depth == 0:
            return start, end
    raise ValueError("Unbalanced method")


def append_method(source: str, name: str, extra: str) -> str:
    _, end = method_span(source, name)
    return source[:end] + extra + "\n   " + source[end:]


def patch_source(source: str, eighth_color: int) -> str:
    # Hash the exact UTF-8 export: never apply textual surgery to an unknown build.
    if sha(source.encode("utf-8")) != STOCK_SHA:
        raise ValueError("Unrecognized RSChordDisplay source; expected FFDec 26.3.0 stock export")
    if not 0 <= eighth_color <= 0xFFFFFF:
        raise ValueError("Colour must be 24-bit RGB")
    if source.count("(5 - stringNdx)") != 2:
        raise ValueError("Unexpected inversion expressions")
    if source.count("34 * stringNdx;") != 2:
        raise ValueError("Unexpected row expressions")
    # Row order: the low strings (native 0, 6, 7) always sit together. Inverted (high e on top)
    # puts the added low B / F# strings BELOW the low E; normal order puts them above it.
    source = source.replace("(5 - stringNdx)", "this.GfxPatchRank(stringNdx,true)")
    source = source.replace("34 * stringNdx;", "34 * this.GfxPatchRank(stringNdx,false);")
    # Keep the six stock calls and their baked-in marker colours unmodified.
    for name in ("InitImages", "UpdateImages"):
        source = append_method(source, name, "\n      this.GfxPatchExtraRows();\n      this.GfxPatchLayout();")
    for name in ("InitTextFields", "UpdateTextFields", "AdjustImagesLayout"):
        source = append_method(source, name, "\n      this.GfxPatchLayout();")
    # Stock compares bare names although actual keys have Selected/NoSelected
    # suffixes. Match that behavior for new rows: all strips scale together.
    anchor = '_loc2_ == "HighEString"'
    if source.count(anchor) != 1:
        raise ValueError("Unexpected scaling guard")
    source = source.replace(anchor, anchor + ' || _loc2_ == "LowBString" || _loc2_ == "LowFsString"')
    extension = (
        Path(__file__)
        .with_name("extension.as")
        .read_text(encoding="utf-8")
        .replace("GFXPATCH_EIGHTH_COLOR", str(eighth_color))
    )
    end = source.rfind("}")
    return source[:end] + extension + source[end:]


def validate_paths(source: Path, output: Path) -> None:
    source, output = source.resolve(), output.resolve()
    for p in (source, output):
        s = p.as_posix().lower()
        if "/rocksmith2014" in s or re.search(r"/rs7string(?:-[^/]*)?(?:/|$)", s):
            raise ValueError(f"Forbidden location: {p}")
    if source == output or source in output.parents or output in source.parents:
        raise ValueError("Input and output roots must be disjoint")


def safe_child(root: Path, relative: str) -> Path:
    root = root.resolve()
    target = (root / relative).resolve()
    if root not in target.parents:
        raise ValueError(f"Output junction/symlink escapes root: {target}")
    return target


def ffdec(java: str, jar: Path, work: Path, *args: str) -> None:
    run = subprocess.run(
        [java, "-Xmx1024m", "-Djava.awt.headless=true", "-jar", str(jar), *map(str, args)],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=240,
    )
    with (work / "ffdec.log").open("a", encoding="utf-8") as log:
        log.write("COMMAND " + json.dumps(list(map(str, args))) + "\n" + run.stdout + run.stderr + "\n")
    if run.returncode or "Error" in run.stderr:
        raise RuntimeError(f"FFDec failed; see {work / 'ffdec.log'}")


def export_class(java: str, jar: Path, work: Path, movie: Path, destination: Path) -> Path:
    ffdec(java, jar, work, "-onerror", "abort", "-export", "script", destination, movie)
    matches = [p for p in destination.rglob("RSChordDisplay.as") if p.as_posix().endswith(CLASS + ".as")]
    if len(matches) != 1:
        raise ValueError(f"Expected one exported class under {destination}")
    return matches[0]


def patch_movie(
    source: Path, output: Path, sprite_id: int, java: str, jar: Path, work: Path, eighth_color: int
) -> dict:
    original = source.read_bytes()
    before = parse_movie(original)
    index = action_index(before, sprite_id)
    exported = export_class(java, jar, work, source, work / "before")
    # Read with newline='' because the known export hash includes CRLF.
    with exported.open(encoding="utf-8", newline="") as f:
        stock = f.read()
    patched = patch_source(stock, eighth_color)
    patch_as = work / "patched-class.as"
    patch_as.write_text(patched, encoding="utf-8", newline="")
    compiled = work / "compiled.gfx"
    ffdec(java, jar, work, "-replace", source, compiled, "\\" + CLASS.replace("/", "\\"), patch_as)
    result = transplant(original, compiled.read_bytes(), sprite_id)
    candidate = work / "candidate.gfx"
    candidate.write_bytes(result)
    verify_as = export_class(java, jar, work, candidate, work / "after")
    verification = verify_as.read_text(encoding="utf-8")
    required = [
        "GfxPatchCount",
        "GfxPatchRank",
        "GfxPatchExtraRows",
        "GfxPatchLayout",
        "LowBFret",
        "LowFsFret",
        "LowBString",
        "LowFsString",
        "8421504",
        str(eighth_color),
    ]
    for token in required:
        if token not in verification:
            raise ValueError(f"Re-decompile missing {token}")
    after = parse_movie(result)
    if before.frame_header != after.frame_header or len(before.tags) != len(after.tags):
        raise ValueError("Movie structure changed")
    if any(a.raw != b.raw for i, (a, b) in enumerate(zip(before.tags, after.tags)) if i != index):
        raise ValueError("Non-target tag changed")
    if sha(source.read_bytes()) != sha(original):
        raise ValueError("Input changed during patch")
    output.parent.mkdir(parents=True, exist_ok=True)
    # No replacement of existing outputs without explicit CLI flag upstream.
    output.write_bytes(result)
    return {
        "input": str(source),
        "output": str(output),
        "input_sha256": sha(original),
        "output_sha256": sha(result),
        "class_sprite_id": sprite_id,
        "changed_tag_index": index,
        "unchanged_tags": len(before.tags) - 1,
        "recompiled_class": str(verify_as),
        "original_class": str(exported),
        "patched_source": str(patch_as),
        "source_sha256": sha(stock.encode()),
        "verified": True,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True, help="gfxassets root containing libs/ and views/")
    parser.add_argument("--output", type=Path, required=True, help="Separate output gfx root")
    parser.add_argument("--ffdec", type=Path, required=True, help="FFDec 26.3.0 ffdec.jar")
    parser.add_argument("--java", default="java")
    parser.add_argument("--eighth-color", type=lambda s: int(s.removeprefix("#"), 16), default=0x00FFFF)
    parser.add_argument("--overwrite", action="store_true", help="Replace existing edited copies")
    args = parser.parse_args()
    validate_paths(args.input, args.output)
    args.input, args.output, args.ffdec = args.input.resolve(), args.output.resolve(), args.ffdec.resolve()
    if not args.ffdec.is_file():
        parser.error("FFDec jar missing")
    for relative in TARGETS:
        if not (args.input / relative).is_file():
            parser.error(f"Missing input: {relative}")
        validate_paths((args.input / relative).resolve(), safe_child(args.output, relative))
        if safe_child(args.output, relative).exists() and not args.overwrite:
            parser.error(f"Output exists: {relative}; use --overwrite")
    safe_child(args.output, "gfxpatch-report.json")
    root = Path(__file__).resolve().parent / ".work"
    root.mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="patch-", dir=root))
    report = {"work": str(work), "eighth_default_rgb": args.eighth_color, "movies": []}
    # Verify all three candidates before publishing any of the output movies.
    for relative, sprite_id in TARGETS.items():
        local = work / Path(relative).stem
        local.mkdir()
        print(f"Compiling and verifying {relative} ...", flush=True)
        report["movies"].append(
            patch_movie(
                args.input / relative,
                work / "staged" / relative,
                sprite_id,
                args.java,
                args.ffdec,
                local,
                args.eighth_color,
            )
        )
    for item, relative in zip(report["movies"], TARGETS):
        target = safe_child(args.output, relative)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(item["output"], target)
        item["output"] = str(target)
    args.output.mkdir(parents=True, exist_ok=True)
    safe_child(args.output, "gfxpatch-report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    (work / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"Verified 3 movies. Evidence: {work / 'report.json'}", flush=True)


if __name__ == "__main__":
    main()
