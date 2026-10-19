"""CALL_WRAP (emu_harness.py) against the C++ registration: every Rs7_WrapRow of src/features/*/registration.cpp has an
entry with the same frame (argument dwords, bytes the callee pops, scratch) and callbacks, and every entry has a row.
The frames are read from their `inline constexpr Rs7Mid::Wrap kRs7Wrap<ID>{...}` declarations in the features'
hooks.h. No DLL or game needed."""

from __future__ import annotations

import re
from pathlib import Path

from emu_harness import CALL_WRAP, MID_CALLBACKS

REPO = Path(__file__).resolve().parents[2]
FEATURES = REPO / "src" / "features"
ROW = re.compile(
    r'Rs7_WrapRow\(\s*(?:RS7_\w+\()?\{?\s*"(\w+)".*?(kRs7Wrap\w+),\s*(&\w+|nullptr),\s*(&\w+|nullptr)', re.S
)
FRAME = re.compile(r"inline constexpr Rs7Mid::Wrap (kRs7Wrap\w+)\{(\d+), Rs7Mid::Conv::(\w+)(?:, (\d+))?\}")


def callback(token: str) -> str | None:
    return None if token == "nullptr" else token.lstrip("&")


def registered() -> dict[str, tuple]:
    frames = {}
    for header in FEATURES.glob("*/hooks.h"):
        for name, args, conv, locals_ in FRAME.findall(header.read_text(encoding="utf-8")):
            assert conv in ("Stdcall", "Cdecl"), (name, conv)
            frames[name] = (int(args), 0 if conv == "Cdecl" else 4 * int(args), int(locals_ or 0))
    rows = {}
    for source in FEATURES.glob("*/registration.cpp"):
        for sid, frame, before, after in ROW.findall(source.read_text(encoding="utf-8")):
            assert frame in frames, f"{sid}: {frame} is not declared in a feature's hooks.h"
            rows[sid] = (*frames[frame], callback(before), callback(after))
    return rows


def test_every_wrap_row_matches_call_wrap():
    rows = registered()
    assert rows, "no Rs7_WrapRow found: the pattern no longer matches the registration sources"
    assert set(rows) == set(CALL_WRAP), set(rows) ^ set(CALL_WRAP)
    for sid, row in rows.items():
        assert CALL_WRAP[sid] == row, (sid, CALL_WRAP[sid], row)


def test_mid_callbacks_name_the_after_or_the_only_callback():
    for sid, (_, _, _, before, after) in CALL_WRAP.items():
        assert MID_CALLBACKS[sid] == (after or before), sid
