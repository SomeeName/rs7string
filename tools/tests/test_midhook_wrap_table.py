"""CALL_WRAP (emu_harness.py) against the C++ registration: every Rsxr_WrapRow of src/features/*/registration.cpp has an
entry with the same frame (argument dwords, bytes the callee pops, scratch) and callbacks, and every entry has a row.
The frames are read from their `inline constexpr RsxrMid::Wrap kRsxrWrap<ID>{...}` declarations in the features'
hooks.h, or inline aggregate initializers in a row (omitted cleanup defaults to Stdcall). No DLL or game needed."""

from __future__ import annotations

import re
from pathlib import Path

from emu_harness import CALL_WRAP, MID_CALLBACKS

REPO = Path(__file__).resolve().parents[2]
FEATURES = REPO / "src" / "features"
ROW = re.compile(
    r'Rsxr_WrapRow\(\s*Rsxr_Site\(\s*"(\w+)"[^)]*\)(?:\s*\.\w+\([^)]*\))*,\s*'
    r"(kRsxrWrap\w+|\{[^}]*\}),\s*(&\w+|nullptr),\s*(&\w+|nullptr)",
    re.S,
)
FRAME = re.compile(r"inline constexpr RsxrMid::Wrap (kRsxrWrap\w+)\{(\d+), RsxrMid::Conv::(\w+)(?:, (\d+))?\}")
INLINE_FRAME = re.compile(r"\{\s*(\d+)(?:,\s*RsxrMid::Conv::(Stdcall|Cdecl)(?:,\s*(\d+))?)?\s*\}")


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
            if frame.startswith("{"):
                inline = INLINE_FRAME.fullmatch(frame)
                assert inline, f"{sid}: unrecognised inline frame {frame}"
                args, conv, locals_ = inline.groups()
                layout = (int(args), 0 if conv == "Cdecl" else 4 * int(args), int(locals_ or 0))
            else:
                assert frame in frames, f"{sid}: {frame} is not declared in a feature's hooks.h"
                layout = frames[frame]
            rows[sid] = (*layout, callback(before), callback(after))
    return rows


def test_every_wrap_row_matches_call_wrap():
    rows = registered()
    assert rows, "no Rsxr_WrapRow found: the pattern no longer matches the registration sources"
    assert set(rows) == set(CALL_WRAP), set(rows) ^ set(CALL_WRAP)
    for sid, row in rows.items():
        assert CALL_WRAP[sid] == row, (sid, CALL_WRAP[sid], row)


def test_mid_callbacks_name_the_after_or_the_only_callback():
    assert CALL_WRAP
    for sid, (_, _, _, before, after) in CALL_WRAP.items():
        assert MID_CALLBACKS[sid] == (after or before), sid
