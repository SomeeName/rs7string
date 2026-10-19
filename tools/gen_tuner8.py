#!/usr/bin/env python3
"""Confirmed GuitarStrings readers in tuner/StringAction; no session count publish.

Emit checked relocated copies using the existing fncopy format. The full native
tuner still needs session storage and sequence ports (docs/ARCHITECTURE.md).
uv run python tools/gen_tuner8.py
"""

from pathlib import Path
import gen_fncopy as fc

OUT = Path(__file__).resolve().parents[1] / "src/features/tuner/tuner8_specs.generated.h"
FNS = [
    fc.Fn(
        "TU8H",
        0x5B0770,
        0x5B080F,
        count="active",
        arrays=False,
        edits={
            0x5B0791: ("bytes", "8B 4C 01 04", "8B 4C 01 40"),
            0x5B07AD: ("bytes", "8B 44 08 04", "8B 44 08 40"),
            0x5B07DC: ("bytes", "8B 4C 01 04", "8B 4C 01 40"),
            0x5B07F9: ("bytes", "8B 44 08 04", "8B 44 08 40"),
        },
    ),
    fc.Fn(
        "TU8C",
        0x5B0810,
        0x5B0875,
        count="active",
        arrays=False,
        edits={
            0x5B082C: ("bytes", "8B 4C 01 04", "8B 4C 01 40"),
            0x5B0848: ("bytes", "8B 44 08 04", "8B 44 08 40"),
        },
    ),
    fc.Fn(
        "TU8A",
        0x7E7140,
        0x7E77F5,
        count="active",
        arrays=False,
        edits={
            # PUBEDGE (feat/publish, 0x7E72D0..DC) lives inside this range: the copy calls the helper that
            # does the stock expression on the active count, or publish's physical edge when native_publish
            # is on (it replaces the former `mov al,[eax+0Bh]` -> [eax+40h] edit at 0x7E72D8).
            0x7E72D0: ("call", "g_rs7_callin_PubEdge", 13),
            0x7E7465: ("bytes", "8B 44 86 04", "8B 44 86 40"),
            0x7E749C: ("bytes", "8B 44 8E 04", "8B 44 8E 40"),
            # Stock signed JL accepts -1 and later narrows it to 255. Unsigned JB
            # rejects negative and oversized message indices before any child read.
            0x7E7418: ("bytes", "7C 29", "72 29"),
        },
    ),
]


def render(text, base):
    results = [(fn, *fc.build(fn, text, base)) for fn in FNS]
    return fc.render(results).replace("tools/gen_fncopy.py", "tools/gen_tuner8.py")


if __name__ == "__main__":
    text, base = fc.load_text()
    OUT.write_text(render(text, base), encoding="utf-8", newline="\n")
    print(f"{OUT}: {len(FNS)} checked function copies")
