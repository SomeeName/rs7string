#!/usr/bin/env python3
"""Resolve every site of sites.json against an exe image (default: the analysis exe).

Every pattern must match exactly once inside .text and the bytes at the site must equal
`expect`. A jmp-kind site (a jmp over several instructions) must cover whole instructions and no direct
branch anywhere in .text may target a byte inside it other than its start (issue #18: PSL2 once
covered `mov byte [ebx+8],1; mov eax,[ebp+8]` and 0x59C587 jumps to the second instruction - into
the middle of the patched jmp). Run before every in-game test:  uv run python rs7string/tools/sitecheck.py
"""

from __future__ import annotations

import re
import sys
import itertools
from pathlib import Path

import capstone
import game_files
import pefile

from sitetable import REF_KINDS, Site, load, parse_pattern

# [neck] switches that choose between site variants (overlap check: every combination). A site
# field "native" means written only when the switch is 1, "" always (the "legacy" variants went in 1.0).
# native_colours has no site field: the CM* sites are written with 1 only.
SWITCH_NAMES = ("strings", "fretgrid", "colours", "pitch", "nextgrid", "publish", "chordloops", "tuner", "clite")
# clite ([neck] clite, features/clite/clite.h): the GK* sites; GKC replaces PSQ1 (it writes the peg word itself).
# publish (native_publish) is effective only with all five layouts on: PUBLISH_LAYOUTS.
PUBLISH_LAYOUTS = ("strings", "fretgrid", "colours", "pitch", "nextgrid")
CHORDLOOPS_OFF = frozenset({"N22", "N23", "NF2", "NF3", *[f"CT{i}" for i in range(4, 12)]})
# tuner (native_tuner, features/tuner/hooks.h): effective only with strings, fretgrid, colours.


def written_under(site: Site, state: dict[str, bool]) -> bool:
    publish = state.get("publish", False) and all(state.get(k, False) for k in PUBLISH_LAYOUTS)
    if site.publish == "native" and not publish:
        return False
    clite = state.get("clite", False)
    if site.id.startswith("GK") and not clite:
        return False
    if site.id == "PSQ1" and clite:
        return False
    if site.id == "FGRB" and publish:
        return False
    if site.id in {"NN1", "NN3", "NN4"} and publish:
        return False
    chordloops = state.get("chordloops", False) and state["strings"]
    if site.id.startswith("CL") and not chordloops:
        return False
    if site.id in CHORDLOOPS_OFF and chordloops:
        return False
    tuner = state.get("tuner", False) and state["strings"] and state["fretgrid"] and state["colours"]
    if site.tuner == "native" and not tuner:
        return False
    for field in ("strings", "fretgrid", "pitch", "nextgrid"):
        v = getattr(site, field)
        if v == "native" and not state[field]:
            return False
    if site.id.startswith("CM") and not state["colours"]:
        return False
    return True


DEFAULT_SITES = Path(__file__).resolve().parents[1] / "sites.json"  # this checkout
DEFAULT_EXE = game_files.ANALYSIS_EXE


def find_all(data: bytes, pattern: list[int | None]) -> list[int]:
    rx = b"".join(b"." if b is None else re.escape(bytes([b])) for b in pattern)
    return [m.start() for m in re.finditer(b"(?=" + rx + b")", data, re.S)]


def check_site(text: bytes, text_va: int, site: Site):
    hits = find_all(text, parse_pattern(site.pattern))
    if len(hits) != 1:
        return False, f"{len(hits)} matches", None, None
    off = hits[0] + site.offset
    actual = text[off : off + len(site.expect)]
    if actual != site.expect:
        return False, f"expected {site.expect.hex(' ')} found {actual.hex(' ')}", text_va + off, None
    target = None
    if site.target:
        rel_at, rel_size, insn_end = site.target
        rel = int.from_bytes(text[off + rel_at : off + rel_at + rel_size], "little", signed=True)
        target = text_va + off + insn_end + rel
    if site.kind == "ref":
        target = int.from_bytes(text[off : off + 4], "little")
    return True, "ok", text_va + off, target


JMP_KINDS = {"jmp", "jmp_atomic8"}


def instruction_span_end(text: bytes, text_va: int, va: int, length: int) -> int:
    """End of the last instruction (decoded from `va`) that the `length` bytes at `va` touch."""
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    off = va - text_va
    end = va
    for insn in md.disasm(text[off : off + length + 15], va):
        end = insn.address + insn.size
        if end >= va + length:
            break
    return end


def _instruction_start(text: bytes, text_va: int, va: int, md) -> bool:
    """Decodes forward from the end of the last two int3 padding runs before `va`: True when either
    decode lands on `va` (a real instruction there), False when both step over it (a misaligned
    byte pattern). No padding within 64 KB: True (report rather than miss)."""
    off = va - text_va
    starts, cut = [], off
    while len(starts) < 2:
        p = text.rfind(bytes((0xCC, 0xCC)), max(0, off - 0x10000), cut)
        if p < 0:
            break
        q = p + 2
        while q < off and text[q] == 0xCC:
            q += 1
        starts.append(q)
        cut = p
    if not starts:
        return True
    for q in starts:
        pos = q
        while pos < off:
            insn = next(md.disasm(text[pos : pos + 16], text_va + pos, 1), None)
            pos += insn.size if insn else 1
        if pos == off:
            return True
    return False


def inbound_branches(text: bytes, text_va: int, ranges: list[tuple[int, int, str]]):
    """[(site id, branch va, target va)]: direct branches (E8/E9 rel32, 0F 80-8F rel32, 70-7F/EB
    rel8) whose target lies strictly inside one of `ranges` (start, end, id), each confirmed to be
    a real instruction by _instruction_start."""
    import numpy as np

    a = np.frombuffer(text, np.uint8)
    n = len(a)
    found = []
    b = np.zeros(n + 8, np.uint8)
    b[:n] = a

    def rel32(idx):
        v = (
            b[idx].astype(np.uint32)
            | b[idx + 1].astype(np.uint32) << 8
            | b[idx + 2].astype(np.uint32) << 16
            | b[idx + 3].astype(np.uint32) << 24
        )
        return v.view(np.int32).astype(np.int64)

    i1 = np.nonzero((a == 0xE8) | (a == 0xE9))[0]
    found.append((i1, i1 + 5 + rel32(i1 + 1)))
    i2 = np.nonzero((a[:-1] == 0x0F) & ((a[1:] & 0xF0) == 0x80))[0]
    found.append((i2, i2 + 6 + rel32(i2 + 2)))
    i3 = np.nonzero(((a & 0xF0) == 0x70) | (a == 0xEB))[0]
    found.append((i3, i3 + 2 + b[i3 + 1].view(np.int8).astype(np.int64)))
    src = np.concatenate([f[0] for f in found]) + text_va
    tgt = np.concatenate([f[1] for f in found]) + text_va
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    hits = []
    for r0, r1, sid in ranges:
        # Branches that start inside the range are themselves replaced by the stub (e.g. PUBEDGE's
        # test/je/xor/jmp/mov/dec block); only branches from outside can land mid-patch.
        for k in np.nonzero((tgt > r0) & (tgt < r1) & ((src < r0) | (src >= r1)))[0]:
            if _instruction_start(text, text_va, int(src[k]), md):
                hits.append((sid, int(src[k]), int(tgt[k])))
    return hits


def main(argv: list[str]) -> int:
    sites_path = Path(argv[0]) if argv else DEFAULT_SITES
    exe_path = Path(argv[1]) if len(argv) > 1 else DEFAULT_EXE
    checksum, sites = load(sites_path)
    pe = pefile.PE(str(exe_path), fast_load=True)
    text_sec = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
    text = text_sec.get_data()[: text_sec.Misc_VirtualSize]
    text_va = pe.OPTIONAL_HEADER.ImageBase + text_sec.VirtualAddress
    failures = 0
    if pe.OPTIONAL_HEADER.CheckSum != checksum:
        print(f"PE checksum {pe.OPTIONAL_HEADER.CheckSum:#010x} != sites.json {checksum:#010x}")
        failures += 1
    written: list[tuple[int, int, str, Site]] = []
    jumps: list[tuple[int, int, str]] = []
    for s in sites:
        ok, msg, va, tgt = check_site(text, text_va, s)
        failures += not ok
        if ok and s.kind not in REF_KINDS:
            written.append((va, va + s.length, s.id, s))
        if ok and s.kind in JMP_KINDS:
            # jmp: NOP-filled to `length`, which must end on an instruction boundary. jmp_atomic8
            # writes the 5 jmp bytes only, the rest of the cut instruction stays as dead bytes.
            span = instruction_span_end(text, text_va, va, s.length)
            jumps.append((va, span, s.id))
            if s.kind == "jmp" and span != va + s.length:
                print(f"FAIL {s.id} {va:08X}: length {s.length} ends inside an instruction")
                failures += 1
        where = f"{va:08X}" if va else "--------"
        label = "->" if s.kind in REF_KINDS else "target"
        print(
            f"{'ok  ' if ok else 'FAIL'} {s.id:7} {where} {label} {tgt:08X}"
            if tgt
            else f"{'ok  ' if ok else 'FAIL'} {s.id:7} {where} {msg if not ok else ''}"
        )
    # Overlaps must be absent in every combination of the [neck] switches (SWITCH_NAMES): each
    # switch on alone, all off, and every mix.
    for bits in itertools.product((False, True), repeat=len(SWITCH_NAMES)):
        state = dict(zip(SWITCH_NAMES, bits))
        ws = sorted((a, b, i) for a, b, i, s in written if written_under(s, state))
        desc = ", ".join(f"native_{k}={int(v)}" for k, v in state.items())
        for (a0, a1, i0), (b0, b1, i1) in zip(ws, ws[1:]):
            if b0 < a1:
                print(f"FAIL {i0} {a0:08X}..{a1:08X} overlaps {i1} {b0:08X}..{b1:08X} ({desc})")
                failures += 1
    for sid, src, tgt in inbound_branches(text, text_va, jumps):
        print(f"FAIL {sid}: branch at {src:08X} targets {tgt:08X}, inside the patched jmp")
        failures += 1
    print(f"{len(sites)} sites, {failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
