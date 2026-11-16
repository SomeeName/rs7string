"""Add the 7-string key to the arrangement manifests of a plain (unencrypted) PSARC.

    uv run python rs7string/tools/manifest_key.py IN.psarc OUT.psarc [--t6 N] [--arr NAME]

The pre-song tuner opens before song setup and reads the tuning from the manifest `Tuning` object
(docs/ARCHITECTURE.md), not from the SNG. rs7string reads the 7th string's offset
from the key `Rs7String6` there. The key must NOT be `string6`: the song-list reader 0x56d0b0
writes every `string%d` key into a 6-int stack array and `string6` overwrites the /GS cookie, which
crashes the stock game. Another name only raises the child count; its `string6` lookup misses.

Adds `"Rs7String6": t6` after `"string5"` in every `Tuning` object of the arrangement .json
manifests and the song .hsan (both, so either source carries it). Bass and vocals manifests are
skipped (no `Tuning` with string5 / ArrangementName Bass). Text-level edit: the rest of each file
stays byte for byte.
"""

import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import psarc_plain  # noqa: E402

KEY = "Rs7String6"
_STRING5 = re.compile(r'"string5"\s*:\s*-?\d+(?:\.\d+)?')
_TOKEN = re.compile(r'"(?:[^"\\]|\\.)*"|[{}]')
_TUNING = re.compile(r'"Tuning"\s*:\s*(\{[^{}]*\})')


def add_key(text: str, t6: int, arr: str | None = None) -> tuple[str, int]:
    """Returns (new text, number of Tuning objects changed). Idempotent: an existing key is kept."""
    stack, edits = [], []
    for token in _TOKEN.finditer(text):
        if token.group() == "{":
            stack.append(token.start())
            continue
        if token.group() != "}":
            continue
        start = stack.pop()
        block = json.loads(text[start : token.end()])
        tuning = block.get("Tuning")
        arrangement = block.get("ArrangementName", "").lower()
        if not isinstance(tuning, dict) or KEY in tuning or "string5" not in tuning:
            continue
        if arrangement in ("bass", "vocals") or (arr and arrangement != arr.lower()):
            continue
        match = _TUNING.search(text, start, token.end())
        m = _STRING5.search(text, match.start(1), match.end(1))
        line_start = text.rfind("\n", 0, m.start()) + 1
        indent = re.match(r"[ \t]*", text[line_start:]).group(0)
        nl = "\r\n" if "\r\n" in text[match.start(1) : match.end(1)] else "\n"
        edits.append((m.end(), f',{nl}{indent}"{KEY}": {t6}'))
    for pos, inserted in sorted(edits, reverse=True):
        text = text[:pos] + inserted + text[pos:]
    return text, len(edits)


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    src, dst = argv[0], argv[1]
    t6 = int(argv[argv.index("--t6") + 1]) if "--t6" in argv else 0
    arr = argv[argv.index("--arr") + 1].lower() if "--arr" in argv else None
    files, meta = psarc_plain.read(src)
    total = 0
    for name in meta["names"]:
        low = name.lower()
        if not low.startswith("manifests/") or not (low.endswith(".json") or low.endswith(".hsan")):
            continue
        if low.endswith(".json") and arr and not low.endswith(f"_{arr}.json"):
            continue
        text = files[name].decode("utf-8")
        new, n = add_key(text, t6, arr)
        if n:
            files[name] = new.encode("utf-8")
            print(f"{name}: {n} Tuning object(s) +{KEY}={t6}")
            total += n
    if not total:
        sys.exit("no Tuning object changed (key present already, or no guitar manifest)")
    psarc_plain.write(dst, files, meta["names"], meta["ver"], meta["flags"])
    print("wrote", dst)


if __name__ == "__main__":
    main(sys.argv[1:])
