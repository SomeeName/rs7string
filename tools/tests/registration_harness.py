"""Read hook rows after MSVC expands their macros and generator-owned includes."""

import re

from native_harness import ROOT, preprocess_sources


def registered_rows(tmp_path):
    sources = sorted((ROOT / "src/features").glob("*/registration.cpp"))
    assert sources, "no registration sources found"
    source = preprocess_sources(sources, tmp_path)
    arrays = re.findall(r"\bconst(?:expr)? Rs7Hook \w+\[\]\s*=\s*\{(.*?)\};", source, re.S)
    assert arrays, "no hook arrays found in preprocessed registrations"
    rows = {}
    for array in arrays:
        depth, start = 0, 0
        for i, char in enumerate(array + ","):
            depth += (char == "(") - (char == ")")
            if char != "," or depth:
                continue
            row = array[start:i].strip()
            start = i + 1
            if not row:
                continue
            match = re.search(r'(?:Rs7_(?:Site|Mid|Stub|Ref)|NgHook)\(\s*((?:"\w+"\s*)+)', row)
            assert match, f"unrecognised hook row: {row}"
            sid = "".join(re.findall(r'"(\w+)"', match[1]))
            assert sid not in rows, f"duplicate registration: {sid}"
            rows[sid] = row
    assert rows
    return rows
