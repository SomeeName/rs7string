"""Pin the engine texture-list layout (src/d3d/texlist.h, plan "Offline findings") in the memory snapshot.

Walks Gamebryo's global NiTexture list from [0x1360B00] with the rs7tex constants parsed out of
texlist.h, the way Rs7Tex_Walk does, and checks the stock colour ramps the Step 2 swap relies on.
Reads only the memory snapshot (memory-mapped, read-only).
"""

from __future__ import annotations

import bisect
import json
import mmap
import re
import struct
import sys
from functools import lru_cache
from pathlib import Path

import game_files
import pytest

SNAPSHOT = game_files.SNAPSHOT
TEXLIST_H = Path(__file__).resolve().parents[2] / "src" / "d3d" / "texlist.h"  # this checkout
sys.path.insert(0, str(game_files.SCRIPTS))

pytestmark = pytest.mark.skipif(not (SNAPSHOT / "memory.bin").exists(), reason="needs the memory snapshot")

LIST_HEAD = 0x1360B00  # X_THEAD (sites.json), CONFIRMED
MEM_IMAGE = 0x1000000
NOTE, ADDITIVE = "note_gradient_map.dds", "gradient_map_additive.dds"
W, H, FMT_X8R8G8B8 = 256, 128, 22


class MappedMemory:
    """The snapshot's address space, memory-mapped (no 700 MB copy)."""

    def __init__(self) -> None:
        index = json.loads((SNAPSHOT / "index.json").read_text(encoding="utf-8"))
        self.regions = sorted(index["regions"], key=lambda r: r["base"])
        self.bases = [r["base"] for r in self.regions]
        self._file = open(SNAPSHOT / "memory.bin", "rb")
        self.blob = mmap.mmap(self._file.fileno(), 0, access=mmap.ACCESS_READ)

    def read(self, va: int, n: int) -> bytes | None:
        i = bisect.bisect_right(self.bases, va) - 1
        if i < 0:
            return None
        r = self.regions[i]
        off = va - r["base"]
        if off + n > r["size"]:
            return None
        return self.blob[r["offset"] + off : r["offset"] + off + n]

    def u32(self, va: int) -> int | None:
        b = self.read(va, 4)
        return None if b is None else struct.unpack("<I", b)[0]

    def cstr(self, va: int, limit: int = 256) -> str | None:
        out = bytearray()
        while len(out) < limit:
            b = self.read(va + len(out), 1)
            if b is None:
                return None if not out else out.decode("ascii", "replace")
            if b == b"\0":
                break
            out += b
        return out.decode("ascii", "replace")


@lru_cache(maxsize=1)
def mem() -> MappedMemory:
    return MappedMemory()


@lru_cache(maxsize=1)
def consts() -> dict[str, int]:
    """constexpr values inside `namespace rs7tex { ... }` of texlist.h."""
    text = TEXLIST_H.read_text(encoding="utf-8")
    block = re.search(r"namespace rs7tex\s*\{(.*?)\n\}", text, re.S)
    assert block, "namespace rs7tex not found in texlist.h"
    found = {k: int(v, 0) for k, v in re.findall(r"\b(k\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)", block.group(1))}
    for k in (
        "kVtSource54",
        "kVtSource50",
        "kVtRendData",
        "kNext",
        "kRendData",
        "kFile",
        "kPlatFile",
        "kRdOwner",
        "kRdW",
        "kRdH",
        "kRdFmt",
        "kRdD3d",
        "kMaxNodes",
    ):
        assert k in found, f"{k} missing in texlist.h"
    return found


@lru_cache(maxsize=1)
def d3d9_range() -> tuple[int, int]:
    from resolve_imports import module_exports

    m = mem()
    for r in m.regions:
        if r["type"] != MEM_IMAGE or r["base"] % 0x10000 or m.read(r["base"], 2) != b"MZ":
            continue
        e = module_exports(m, r["base"])
        if e and e[0].lower() == "d3d9.dll":
            return r["base"], r["base"] + e[1]
    raise AssertionError("no image exporting as d3d9.dll in the snapshot")


def family(name: str | None) -> str | None:
    if not name:
        return None
    base = re.split(r"[\\/]", name)[-1].lower()
    return base if base in (NOTE, ADDITIVE) else None


@lru_cache(maxsize=1)
def walk() -> list[dict]:
    """Name-matched source textures: obj, rd, d3d, family, w, h, fmt, path."""
    c, m = consts(), mem()
    found, seen = [], set()
    node = m.u32(LIST_HEAD)
    while node:
        assert node not in seen, "cycle in the texture list"
        assert len(seen) < c["kMaxNodes"], "texture list longer than kMaxNodes"
        seen.add(node)
        vt = m.u32(node)
        assert vt is not None, f"unreadable node {node:08X}"
        if vt in (c["kVtSource54"], c["kVtSource50"]):
            names = [m.cstr(p, 120) if p else None for p in (m.u32(node + c["kFile"]), m.u32(node + c["kPlatFile"]))]
            fam = family(names[0]) or family(names[1])
            if fam:
                rd = m.u32(node + c["kRendData"])
                found.append(
                    {
                        "obj": node,
                        "rd": rd,
                        "family": fam,
                        "path": names[1] or names[0],
                        "rd_vt": m.u32(rd),
                        "owner": m.u32(rd + c["kRdOwner"]),
                        "w": m.u32(rd + c["kRdW"]),
                        "h": m.u32(rd + c["kRdH"]),
                        "fmt": m.u32(rd + c["kRdFmt"]),
                        "d3d": m.u32(rd + c["kRdD3d"]),
                    }
                )
        node = m.u32(node + c["kNext"])
    return found


def test_texture_list_layout_in_snapshot():
    c = consts()
    lo, hi = d3d9_range()
    found = walk()
    notes = [f for f in found if f["family"] == NOTE]
    adds = [f for f in found if f["family"] == ADDITIVE]
    paths = [f["path"].replace("\\", "/").lower() for f in notes]
    assert len(notes) >= 4, paths
    assert any("gameplay/note/" in p for p in paths), paths
    assert any("gameplay/tail/" in p for p in paths), paths
    assert len(adds) >= 1
    for f in found:
        assert f["rd_vt"] == c["kVtRendData"], f
        assert f["owner"] == f["obj"], f
        assert (f["w"], f["h"], f["fmt"]) == (W, H, FMT_X8R8G8B8), f
        assert f["d3d"], f
        assert lo <= mem().u32(f["d3d"]) < hi, f
    assert len({f["d3d"] for f in found}) == len(found), "every ramp file has its own d3d texture"


def level0(f: dict) -> bytes:
    """System-memory copy of level 0: d3d object +0x90 (observed, not used by the DLL), pitch 1024."""
    bits = mem().u32(f["d3d"] + 0x90)
    data = mem().read(bits, W * H * 4) if bits else None
    assert data, f
    return data


def test_snapshot_note_ramps_share_content_and_rows_are_uniform():
    found = walk()
    for fam in (NOTE, ADDITIVE):
        contents = {level0(f) for f in found if f["family"] == fam}
        assert len(contents) == 1, f"{fam}: {len(contents)} different contents"
    data = level0(next(f for f in found if f["family"] == NOTE))
    pitch, lines = W * 4, H // 16
    for row in range(16):
        first = data[row * lines * pitch : (row * lines + 1) * pitch]
        for y in range(row * lines, (row + 1) * lines):
            assert data[y * pitch : (y + 1) * pitch] == first, f"row {row} line {y} differs"
    for row in (7, 15):
        line = data[row * lines * pitch : (row * lines + 1) * pitch]
        for x in range(W):
            b, g, r, a = line[4 * x : 4 * x + 4]
            assert r == g == b, f"row {row} x {x}: {r:02X}{g:02X}{b:02X}"
            assert a == 0, f"row {row} x {x}: alpha {a:02X}"
