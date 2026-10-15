"""Load and validate rs7string/sites.json (shared by gen_sites.py and sitecheck.py)."""

from __future__ import annotations

import json
import re
from dataclasses import dataclass
from pathlib import Path

KINDS = {"jmp", "jmp_atomic8", "rel32", "call", "ref", "callref", "bytes"}
# Never written: "ref" binds the dword at the site, "callref" the destination of the E8 call there.
REF_KINDS = {"ref", "callref"}


class SiteError(ValueError):
    pass


@dataclass(frozen=True)
class Site:
    id: str
    pattern: str
    offset: int
    expect: bytes
    kind: str
    length: int
    target: tuple[int, int, int] | None  # (rel_at, rel_size, insn_end), relative to the site
    need: str
    note: str
    write: bytes = b""  # kind "bytes": the bytes written over `expect` (same length)
    strings: str = ""  # "native": written only with the native GuitarStrings
    fretgrid: str = ""  # "native": written only with [neck] native_fretgrid=1
    pitch: str = ""  # "native": written only with [neck] native_pitch=1 (issue #18 stage 5 area 3)
    nextgrid: str = ""  # "native": written only with the native next-note grid
    publish: str = ""  # "native": request + all five native layouts
    tuner: str = ""  # "native": written only with the native tuner (core/tuner8.h)
    group: str = ""  # "psarc": the plain-PSARC unit (core/plainpsarc.h), patched early on its own, not in kRs7Sites


def parse_pattern(text: str) -> list[int | None]:
    out: list[int | None] = []
    for tok in text.split():
        if tok == "??":
            out.append(None)
        elif re.fullmatch(r"[0-9A-Fa-f]{2}", tok):
            out.append(int(tok, 16))
        else:
            raise SiteError(f"bad pattern token {tok!r}")
    if not out:
        raise SiteError("empty pattern")
    return out


def _int(v) -> int:
    return int(v, 0) if isinstance(v, str) else int(v)


def load(path) -> tuple[int, list[Site]]:
    doc = json.loads(Path(path).read_text(encoding="utf-8"))
    checksum = _int(doc["pe_checksum"])
    sites: list[Site] = []
    seen: set[str] = set()
    for raw in doc["sites"]:
        sid = raw["id"]
        if sid in seen:
            raise SiteError(f"duplicate id {sid}")
        seen.add(sid)
        kind = raw["kind"]
        if kind not in KINDS:
            raise SiteError(f"{sid}: unknown kind {kind!r}")
        parse_pattern(raw["pattern"])
        expect = bytes.fromhex(raw["expect"])
        if kind == "rel32":
            length, target = 4, (0, 4, 4)
        elif kind == "ref":
            # data reference: never written; the resolved value is the dword at the site
            length, target = 4, None
            if raw.get("target"):
                raise SiteError(f"{sid}: a ref site has no branch target")
        elif kind == "bytes":
            # in-place byte patch (an immediate, a displacement, a loop bound): `write` replaces `expect`
            write = bytes.fromhex(raw.get("write", ""))
            if not write or len(write) != len(expect):
                raise SiteError(f"{sid}: a bytes site needs `write` with as many bytes as `expect`")
            if write == expect:
                raise SiteError(f"{sid}: `write` equals `expect`")
            length, target = len(write), None
        elif kind in ("call", "callref"):
            if expect[:1] != b"\xe8":
                raise SiteError(f"{sid}: a {kind} site must start with E8")
            length, target = 5, (1, 4, 5)
        else:
            length = _int(raw["length"])
            if not 5 <= length <= 16:
                raise SiteError(f"{sid}: length {length} out of range 5..16")
            t = raw.get("target")
            target = (_int(t["rel_at"]), _int(t["rel_size"]), _int(t["insn_end"])) if t else None
            if target and target[1] not in (1, 4):
                raise SiteError(f"{sid}: rel_size must be 1 or 4")
        if len(expect) < length:
            raise SiteError(f"{sid}: expect has {len(expect)} bytes, needs at least {length}")
        strings = raw.get("strings", "")
        if strings not in ("", "native"):
            raise SiteError(f"{sid}: strings must be native (the S7 object went in 1.0)")
        fretgrid = raw.get("fretgrid", "")
        if fretgrid not in ("", "native"):
            raise SiteError(f"{sid}: fretgrid must be native")
        pitch = raw.get("pitch", "")
        if pitch not in ("", "native"):
            raise SiteError(f"{sid}: pitch must be native")
        nextgrid = raw.get("nextgrid", "")
        publish = raw.get("publish", "")
        if publish not in ("", "native"):
            raise SiteError(f"{sid}: publish must be native")
        if nextgrid not in ("", "native"):
            raise SiteError(f"{sid}: nextgrid must be native (R7b / N9 went in 1.0)")
        tuner = raw.get("tuner", "")
        if tuner not in ("", "native"):
            raise SiteError(f"{sid}: tuner must be native (the legacy tuner went in 1.0)")
        group = raw.get("group", "")
        if group not in ("", "psarc"):
            raise SiteError(f"{sid}: group must be psarc")
        sites.append(
            Site(
                sid,
                raw["pattern"],
                _int(raw["offset"]),
                expect,
                kind,
                length,
                target,
                raw.get("need", "M"),
                raw.get("note", ""),
                bytes.fromhex(raw["write"]) if kind == "bytes" else b"",
                strings,
                fretgrid,
                pitch,
                nextgrid,
                publish,
                tuner,
                group,
            )
        )
    return checksum, sites
